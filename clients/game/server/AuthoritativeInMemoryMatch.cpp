#include "AuthoritativeInMemoryMatch.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "MapLayout.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/PublicMatchMetadata.hpp"
#include "basilisk/systems/MatchCoordinator.hpp"
#include "basilisk/systems/PublicMatchMetadataSystem.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/world/MapGenerator.hpp"
#include "basilisk/client/SandboxConfiguration.hpp"
#include "basilisk/client/ai/AiKnowledgeState.hpp"
#include "basilisk/client/ai/AiTurnScheduler.hpp"

namespace basilisk::game::server {
namespace {

bool sameAction(const AvailableAction& available, const PlayerAction& submitted) {
    return available.type == submitted.type &&
           available.targetCave == submitted.targetCave &&
           available.targetTunnel == submitted.targetTunnel &&
           available.targetItem == submitted.targetItem &&
           available.contextualAction == submitted.contextualAction;
}

const PlayerState* findPlayer(const MatchState& state, PlayerId player) {
    const auto found = std::find_if(
        state.players.begin(), state.players.end(),
        [player](const PlayerState& candidate) {
            return candidate.id == player;
        });
    return found == state.players.end() ? nullptr : &*found;
}

PlayerMapView fullPhysicalMap(const MatchState& state) {
    PlayerMapView map;
    if (!state.players.empty()) map.currentCave = state.players.front().cave;
    for (const CaveId cave : state.world.caveIds()) {
        DiscoveredCaveView view;
        view.cave = cave;
        const auto& connections = state.world.cave(cave).connections;
        for (std::size_t index = 0; index < connections.size(); ++index) {
            view.exits.push_back(TunnelView{
                static_cast<TunnelId>(index + 1),
                connections[index],
                false,
            });
        }
        map.caves.push_back(std::move(view));
    }
    return map;
}

PlayerFixedMapGeometry playerGeometry(
    const MatchState& state,
    const PlayerMapLayout& fullLayout,
    const PlayerRoundSnapshot& snapshot) {

    PlayerFixedMapGeometry geometry;
    geometry.fullBounds = fullLayout.positionedBounds();
    for (const DiscoveredCaveView& cave : snapshot.map.caves) {
        if (const auto position = fullLayout.cavePosition(cave.cave))
            geometry.discoveredCaves.emplace(cave.cave, *position);
        if (!state.world.contains(cave.cave)) continue;
        const auto& physicalExits = state.world.cave(cave.cave).connections;
        for (const TunnelView& exit : cave.exits) {
            if (exit.destination.has_value() || exit.id == 0 ||
                exit.id > physicalExits.size()) continue;
            const CaveId hiddenDestination =
                physicalExits[static_cast<std::size_t>(exit.id - 1)];
            if (const auto position = fullLayout.cavePosition(hiddenDestination)) {
                geometry.unknownExitEndpoints.emplace(
                    MapExitKey{cave.cave, exit.id}, *position);
            }
        }
    }
    for (const CaveId cave : snapshot.temporarilyRevealedPitCaves) {
        if (const auto position = fullLayout.cavePosition(cave))
            geometry.temporarilyRevealedCaves.emplace(cave, *position);
    }
    return geometry;
}

} // namespace

struct NetworkAiAgent {
    client::ai::AiConfig config;
    client::ai::AiDecisionEngine engine;
    client::ai::AiKnowledgeState knowledge;
    std::optional<RoundNumber> decisionRound;
    client::ai::AiTurnScheduler scheduler;
    std::optional<ClashId> scheduledClash;
};

class AuthoritativeInMemoryMatchState {
public:
    AuthoritativeInMemoryMatchState(
        MatchState match,
        std::vector<client::PublicPlayerProfile> profiles,
        std::optional<TrophyScoringContext> trophyScoring,
        std::shared_ptr<PublicTrophyReadModel> leaderboard,
        client::MatchMode mode,
        std::vector<client::ai::AiConfig> aiConfigs = {})
        : match_(std::move(match)),
          coordinator_(match_),
          metadata_(PublicMatchMetadataSystem::build(match_)),
          profiles_(std::move(profiles)),
          mode_(mode),
          trophyScoring_(std::move(trophyScoring)),
          leaderboard_(std::move(leaderboard)) {

        for (auto& config : aiConfigs)
            aiAgents_.push_back({
                std::move(config), {}, {}, std::nullopt, {}, std::nullopt});

        const PlayerMapView physicalMap = fullPhysicalMap(match_);
        fullLayout_.update(physicalMap);
        fullLayout_.finalizeFullLayout(physicalMap);
        for (const PlayerState& player : match_.players) {
            viewContexts_.emplace(player.id, client::ClientViewContext{
                player.id,
                player.id,
                client::ClientViewMode::Playing,
                std::nullopt,
            });
        }
        refreshAiSnapshots({});
        driveAi();
    }

    [[nodiscard]] bool containsPlayer(PlayerId player) const {
        return findPlayer(match_, player) != nullptr;
    }

    [[nodiscard]] bool hasEndpoint(PlayerId player) const {
        const auto found = endpoints_.find(player);
        return found != endpoints_.end() && !found->second.expired();
    }

    void attach(
        PlayerId player,
        const std::shared_ptr<InMemoryMatchEndpoint>& endpoint) {
        endpoints_.insert_or_assign(player, endpoint);
    }

    [[nodiscard]] bool enqueueBootstrap(
        PlayerId player,
        InMemoryMatchEndpoint& endpoint,
        std::string& error) {

        const client::ClientViewContext& context = viewContexts_.at(player);
        PlayerRoundSnapshot snapshot =
            SnapshotSystem::buildForPlayer(match_, context.viewedPlayer, {});
        network::ServerBootstrap bootstrap;
        bootstrap.matchMode = mode_;
        bootstrap.matchMetadata = metadata_;
        bootstrap.profiles = profiles_;
        bootstrap.viewContext = context;
        bootstrap.initialMapGeometry =
            playerGeometry(match_, fullLayout_, snapshot);
        bootstrap.initialSnapshot = std::move(snapshot);
        refreshTrophyTotal(player);
        bootstrap.trophyTotal = trophyTotals_.at(player);
        network::WireBytes frame;
        if (!network::encodeWire(bootstrap, frame, error)) return false;
        endpoint.enqueue(std::move(frame));
        if (const ActiveClash* clash = coordinator_.activeClash(); clash != nullptr &&
            std::find(clash->participants.begin(), clash->participants.end(), player) != clash->participants.end())
            enqueueClashStarted(*clash, endpoint);
        return true;
    }

    void enqueueClashStarted(const ActiveClash& clash, InMemoryMatchEndpoint& endpoint) {
        network::ClashStarted message;
        message.clash = clash.id;
        message.participants = clash.participants;
        message.challengeWord = clash.challengeWord;
        message.remainingMs = clash.remainingMs;
        network::WireBytes frame; std::string error;
        if (network::encodeWire(message, frame, error)) endpoint.enqueue(std::move(frame));
    }

    void publishClashStarted(const ActiveClash& clash) {
        for (PlayerId player : clash.participants) {
            const auto it = endpoints_.find(player);
            if (it != endpoints_.end()) if (auto endpoint = it->second.lock()) enqueueClashStarted(clash, *endpoint);
        }
    }

    void publishClashResolved(ClashId id, PlayerId winner, const std::vector<PlayerId>& participants) {
        network::ClashResolved message; message.clash = id; message.winner = winner;
        for (PlayerId player : participants) if (player != winner) message.losers.push_back(player);
        network::WireBytes frame; std::string error;
        if (!network::encodeWire(message, frame, error)) return;
        for (PlayerId player : participants) {
            const auto it = endpoints_.find(player);
            if (it != endpoints_.end()) if (auto endpoint = it->second.lock()) endpoint->enqueue(frame);
        }
    }

    [[nodiscard]] bool receive(
        PlayerId authenticatedPlayer,
        const InMemoryMatchEndpoint* sender,
        std::span<const std::uint8_t> bytes,
        std::string& error) {

        const auto attached = endpoints_.find(authenticatedPlayer);
        const auto endpoint = attached == endpoints_.end()
            ? std::shared_ptr<InMemoryMatchEndpoint>{}
            : attached->second.lock();
        if (endpoint == nullptr || endpoint.get() != sender) {
            error = "Match endpoint no longer owns this player session.";
            return false;
        }

        network::ClientCommand command;
        if (!network::decodeClientCommand(bytes, command, error)) return false;
        return std::visit([&](const auto& payload) {
            return handle(authenticatedPlayer, payload, error);
        }, command.payload);
    }

    [[nodiscard]] RoundNumber round() const noexcept { return match_.round; }
    [[nodiscard]] std::size_t resolvedRoundCount() const noexcept {
        return resolvedRoundCount_;
    }
    [[nodiscard]] std::optional<std::string> trophyScoringError() const {
        return trophyScoringError_;
    }
    [[nodiscard]] std::uint64_t disconnectGraceMs() const noexcept {
        return match_.rules.disconnectGraceMs;
    }

    [[nodiscard]] bool reconnect(
        PlayerId player,
        const std::shared_ptr<InMemoryMatchEndpoint>& endpoint,
        std::string& error) {
        if (!containsPlayer(player) || hasEndpoint(player)) {
            error = "Player cannot reclaim this match session.";
            return false;
        }
        const RoundNumber priorRound = match_.round;
        const auto priorClash = activeClashCopy();
        if (!coordinator_.reconnect(player)) {
            error = "Player reconnect grace has expired.";
            return false;
        }
        publishCoordinatorOutcome(priorRound, priorClash);
        attach(player, endpoint);
        const bool bootstrapped = enqueueBootstrap(player, *endpoint, error);
        if (bootstrapped) driveAi();
        return bootstrapped;
    }

    void advanceTime(std::uint64_t elapsedMs) {
        nowMs_ += elapsedMs;
        const RoundNumber priorRound = match_.round;
        const auto priorClash = activeClashCopy();
        coordinator_.advanceTime(elapsedMs);
        publishCoordinatorOutcome(priorRound, priorClash);
        driveAi();
    }

    void disconnect(PlayerId player, const InMemoryMatchEndpoint* sender) {
        const auto attached = endpoints_.find(player);
        const auto endpoint = attached == endpoints_.end()
            ? std::shared_ptr<InMemoryMatchEndpoint>{}
            : attached->second.lock();
        if (endpoint == nullptr || endpoint.get() != sender) return;
        endpoints_.erase(attached);
        const RoundNumber priorRound = match_.round;
        const auto priorClash = activeClashCopy();
        coordinator_.disconnect(player);
        publishCoordinatorOutcome(priorRound, priorClash);
    }

private:
    [[nodiscard]] std::optional<ActiveClash> activeClashCopy() const {
        const ActiveClash* active = coordinator_.activeClash();
        return active == nullptr
            ? std::nullopt
            : std::optional<ActiveClash>{*active};
    }

    void publishCoordinatorOutcome(
        RoundNumber priorRound,
        const std::optional<ActiveClash>& priorClash,
        std::optional<PlayerId> clashWinner = std::nullopt) {

        if (match_.round != priorRound) ++resolvedRoundCount_;
        const auto& events = coordinator_.authoritativeEvents();
        recordTrophyEvents(events);

        const ActiveClash* active = coordinator_.activeClash();
        const bool resolvedClash = priorClash.has_value() &&
            (active == nullptr || active->id != priorClash->id);
        const bool startedClash = active != nullptr &&
            (!priorClash.has_value() || active->id != priorClash->id);
        const bool publishSnapshot =
            match_.round != priorRound || !events.empty();

        if (!resolvedClash && !startedClash && !publishSnapshot) return;

        refreshContexts();
        refreshAiSnapshots(events);
        if (resolvedClash) {
            publishClashResolved(
                priorClash->id,
                clashWinner.value_or(PlayerId{}),
                priorClash->participants);
        }
        if (publishSnapshot) publishAll(events);
        if (startedClash) publishClashStarted(*active);
    }

    void refreshAiSnapshots(const std::vector<GameEvent>& events) {
        for (const auto& agent : aiAgents_) {
            aiSnapshots_.insert_or_assign(
                agent.config.player,
                SnapshotSystem::buildForPlayer(
                    match_, agent.config.player, events));
        }
    }

    void driveAi() {
        constexpr int kMaximumSteps = 64;
        for (int step = 0; step < kMaximumSteps; ++step) {
            if (match_.result.status != MatchStatus::Active) return;
            if (const ActiveClash* clash = coordinator_.activeClash()) {
                NetworkAiAgent* dueAgent = nullptr;
                std::uint64_t dueDeadline = std::numeric_limits<std::uint64_t>::max();
                for (auto& candidate : aiAgents_) {
                    const bool participant = std::find(
                        clash->participants.begin(),
                        clash->participants.end(),
                        candidate.config.player) != clash->participants.end();
                    if (!participant) {
                        if (candidate.scheduledClash.has_value()) {
                            candidate.scheduler.clearClash();
                            candidate.scheduledClash.reset();
                        }
                        continue;
                    }
                    if (candidate.scheduledClash != clash->id) {
                        candidate.scheduler.clearClash();
                        candidate.scheduler.scheduleClash(
                            clash->id,
                            clash->challengeWord,
                            nowMs_,
                            candidate.config);
                        candidate.scheduledClash = clash->id;
                    }
                    const auto deadline = candidate.scheduler.clashDeadline();
                    if (deadline.has_value() && *deadline <= nowMs_ &&
                        *deadline < dueDeadline) {
                        dueDeadline = *deadline;
                        dueAgent = &candidate;
                    }
                }
                if (dueAgent == nullptr) return;

                const auto scheduled =
                    dueAgent->scheduler.takeDueClash(nowMs_);
                if (!scheduled.has_value()) return;
                const auto priorClash = activeClashCopy();
                const RoundNumber priorRound = match_.round;
                const ClashSubmissionResult result =
                    coordinator_.submitClashResponse(
                        dueAgent->config.player,
                        scheduled->clash,
                        scheduled->response);
                for (auto& candidate : aiAgents_) {
                    if (candidate.scheduledClash == priorClash->id) {
                        candidate.scheduler.clearClash();
                        candidate.scheduledClash.reset();
                    }
                }
                if (result != ClashSubmissionResult::Resolved) return;
                publishCoordinatorOutcome(
                    priorRound, priorClash, dueAgent->config.player);
                continue;
            }

            for (auto& candidate : aiAgents_) {
                if (candidate.scheduledClash.has_value()) {
                    candidate.scheduler.clearClash();
                    candidate.scheduledClash.reset();
                }
            }

            auto agent = std::find_if(aiAgents_.begin(), aiAgents_.end(),
                [&](const NetworkAiAgent& candidate) {
                    const PlayerState* player = findPlayer(
                        match_, candidate.config.player);
                    return player != nullptr && player->alive &&
                        candidate.decisionRound != match_.round;
                });
            if (agent == aiAgents_.end()) return;

            const auto snapshotEntry =
                aiSnapshots_.find(agent->config.player);
            if (snapshotEntry == aiSnapshots_.end() ||
                snapshotEntry->second.round != match_.round) {
                continue;
            }
            const PlayerRoundSnapshot& snapshot = snapshotEntry->second;
            agent->decisionRound = match_.round;
            if (snapshot.availableActions.empty()) continue;
            agent->knowledge.observe(snapshot);
            const auto evaluation = agent->engine.evaluate(
                snapshot, agent->config, agent->knowledge);
            if (evaluation.actions.empty()) continue;
            const auto& selected =
                evaluation.actions[evaluation.chosenIndex].action;

            PlayerAction action;
            action.player = agent->config.player;
            action.type = selected.type;
            action.targetCave = selected.targetCave;
            action.targetTunnel = selected.targetTunnel;
            action.targetItem = selected.targetItem;
            action.contextualAction = selected.contextualAction;
            if (!coordinator_.submitAction(action)) continue;

            agent->knowledge.recordDecision(selected);
            const RoundNumber priorRound = match_.round;
            const auto priorClash = activeClashCopy();
            if (!coordinator_.lockAction(agent->config.player)) return;
            publishCoordinatorOutcome(priorRound, priorClash);
        }
    }

    [[nodiscard]] bool handle(
        PlayerId authenticatedPlayer,
        const network::SubmitActionCommand& command,
        std::string& error) {

        if (command.round != match_.round) {
            error = "Action command does not match the authoritative round.";
            return false;
        }

        if (command.action.player != authenticatedPlayer) {
            error = "Authenticated player does not match submitted action.";
            return false;
        }
        const PlayerRoundSnapshot current = SnapshotSystem::buildForPlayer(
            match_, authenticatedPlayer, {});
        const bool legal = std::any_of(
            current.availableActions.begin(),
            current.availableActions.end(),
            [&](const AvailableAction& action) {
                return sameAction(action, command.action);
            });
        if (!legal) {
            error = "Submitted action is not currently available.";
            return false;
        }
        if (!coordinator_.submitAction(command.action)) {
            error = "Coordinator rejected submitted action.";
            return false;
        }
        error.clear();
        return true;
    }

    [[nodiscard]] bool handle(
        PlayerId authenticatedPlayer,
        const network::LockActionCommand& command,
        std::string& error) {

        if (command.round != match_.round) {
            error = "Action command does not match the authoritative round.";
            return false;
        }

        if (command.player != authenticatedPlayer) {
            error = "Authenticated player does not match action lock.";
            return false;
        }
        const RoundNumber priorRound = match_.round;
        const auto priorClash = activeClashCopy();
        if (!coordinator_.lockAction(authenticatedPlayer)) {
            error = "Coordinator rejected action lock.";
            return false;
        }
        publishCoordinatorOutcome(priorRound, priorClash);
        driveAi();
        error.clear();
        return true;
    }

    [[nodiscard]] bool handle(
        PlayerId authenticatedPlayer,
        const network::SubmitClashResponse& command,
        std::string& error) {
        const ActiveClash* active = coordinator_.activeClash();
        if (active == nullptr || active->id != command.clash) {
            error = "Clash response is stale or invalid.";
            return false;
        }
        const auto priorClash = activeClashCopy();
        const RoundNumber priorRound = match_.round;
        const ClashSubmissionResult result = coordinator_.submitClashResponse(
            authenticatedPlayer, command.clash, command.response);
        if (result == ClashSubmissionResult::Rejected) {
            error = "Clash response was rejected.";
            return false;
        }
        if (result == ClashSubmissionResult::Incorrect) {
            error.clear();
            return true;
        }
        publishCoordinatorOutcome(
            priorRound, priorClash, authenticatedPlayer);
        driveAi();
        error.clear();
        return true;
    }

    [[nodiscard]] bool handle(
        PlayerId authenticatedPlayer,
        const network::WatchRemainingHunterCommand& command,
        std::string& error) {

        if (command.localPlayer != authenticatedPlayer) {
            error = "Authenticated player does not match watch request.";
            return false;
        }
        auto& context = viewContexts_.at(authenticatedPlayer);
        if (context.mode != client::ClientViewMode::Defeated ||
            context.spectatablePlayer != command.viewedPlayer) {
            error = "Requested player is not available to spectate.";
            return false;
        }
        context.mode = client::ClientViewMode::Spectating;
        context.viewedPlayer = command.viewedPlayer;
        publishOne(authenticatedPlayer, {}, true);
        error.clear();
        return true;
    }

    [[nodiscard]] bool handle(
        PlayerId authenticatedPlayer,
        const network::QuitCommand& command,
        std::string& error) {

        if (command.player != authenticatedPlayer) {
            error = "Authenticated player does not match quit request.";
            return false;
        }
        const RoundNumber priorRound = match_.round;
        const auto priorClash = activeClashCopy();
        coordinator_.forfeit(authenticatedPlayer);
        publishCoordinatorOutcome(priorRound, priorClash);
        driveAi();
        error.clear();
        return true;
    }

    [[nodiscard]] bool handle(
        PlayerId authenticatedPlayer,
        const network::LeaderboardPageRequest& command,
        std::string& error) {

        if (leaderboard_ == nullptr) {
            error = "Public leaderboard is not configured.";
            return false;
        }
        std::vector<PublicTrophyLeaderboardEntry> entries;
        if (!leaderboard_->leaderboardPage(
                command.offset, command.limit, entries, error)) return false;
        network::LeaderboardPageResponse response;
        response.offset = command.offset;
        response.entries = std::move(entries);
        network::WireBytes frame;
        if (!network::encodeWire(response, frame, error)) return false;
        const auto endpointIt = endpoints_.find(authenticatedPlayer);
        if (endpointIt == endpoints_.end()) {
            error = "Authenticated leaderboard endpoint is unavailable.";
            return false;
        }
        const auto endpoint = endpointIt->second.lock();
        if (endpoint == nullptr) {
            error = "Authenticated leaderboard endpoint is unavailable.";
            return false;
        }
        endpoint->enqueue(std::move(frame));
        error.clear();
        return true;
    }

    void refreshContexts() {
        for (auto& [localPlayer, context] : viewContexts_) {
            const PlayerState* local = findPlayer(match_, localPlayer);
            if (local == nullptr) continue;
            if (local->alive) {
                context = client::ClientViewContext{
                    localPlayer,
                    localPlayer,
                    client::ClientViewMode::Playing,
                    std::nullopt,
                };
                continue;
            }
            const auto survivor = std::find_if(
                match_.players.begin(), match_.players.end(),
                [localPlayer](const PlayerState& player) {
                    return player.id != localPlayer && player.alive;
                });
            if (context.mode == client::ClientViewMode::Spectating) {
                const PlayerState* viewed = findPlayer(match_, context.viewedPlayer);
                if (match_.result.status == MatchStatus::Active &&
                    (viewed == nullptr || !viewed->alive) &&
                    survivor != match_.players.end()) {
                    context.viewedPlayer = survivor->id;
                    context.spectatablePlayer = survivor->id;
                }
                continue;
            }
            context.localPlayer = localPlayer;
            context.viewedPlayer = localPlayer;
            context.mode = client::ClientViewMode::Defeated;
            context.spectatablePlayer.reset();
            if (match_.result.status == MatchStatus::Active &&
                survivor != match_.players.end())
                context.spectatablePlayer = survivor->id;
        }
    }

    void recordTrophyEvents(const std::vector<GameEvent>& events) {
        if (!trophyScoring_.has_value()) return;
        trophyEvents_.insert(trophyEvents_.end(), events.begin(), events.end());
        if (match_.result.status != MatchStatus::Completed ||
            trophyScoringAttempted_) return;
        trophyScoringAttempted_ = true;
        std::string error;
        const TrophyScoreResult result = trophyScoring_->ledger->scoreMatch(
            trophyScoring_->match,
            trophyScoring_->accounts,
            match_.result,
            trophyEvents_,
            &error);
        if (result == TrophyScoreResult::PersistenceError) {
            trophyScoringError_ = error.empty()
                ? "Unable to persist trophy awards."
                : "Unable to persist trophy awards: " + error;
        }
    }

    void refreshTrophyTotal(PlayerId player) {
        auto& total = trophyTotals_[player];
        if (!trophyScoring_.has_value()) return;
        const auto account = trophyScoring_->accounts.find(player);
        if (account == trophyScoring_->accounts.end()) return;
        std::string error;
        std::int64_t refreshed{};
        if (trophyScoring_->ledger->trophyTotal(
                account->second, refreshed, error)) {
            total = refreshed;
            return;
        }
        if (!trophyScoringError_.has_value()) {
            trophyScoringError_ = error.empty()
                ? "Unable to read persisted trophy total."
                : "Unable to read persisted trophy total: " + error;
        }
    }

    void publishAll(const std::vector<GameEvent>& events) {
        for (auto it = endpoints_.begin(); it != endpoints_.end();) {
            if (auto endpoint = it->second.lock()) {
                publishOne(it->first, events, true);
                ++it;
            } else {
                it = endpoints_.erase(it);
            }
        }
    }

    void publishOne(
        PlayerId localPlayer,
        const std::vector<GameEvent>& events,
        bool includeContext) {

        const auto endpointIt = endpoints_.find(localPlayer);
        if (endpointIt == endpoints_.end()) return;
        const auto endpoint = endpointIt->second.lock();
        if (endpoint == nullptr) return;
        const client::ClientViewContext& context = viewContexts_.at(localPlayer);
        const PlayerId viewer = context.mode == client::ClientViewMode::Spectating
            ? context.viewedPlayer
            : context.localPlayer;
        PlayerRoundSnapshot snapshot =
            SnapshotSystem::buildForPlayer(match_, viewer, events);
        network::ServerUpdate update;
        update.mapGeometry = playerGeometry(match_, fullLayout_, snapshot);
        update.snapshot = std::move(snapshot);
        if (includeContext) update.viewContext = context;
        refreshTrophyTotal(localPlayer);
        update.trophyTotal = trophyTotals_.at(localPlayer);
        network::WireBytes frame;
        std::string error;
        if (network::encodeWire(update, frame, error))
            endpoint->enqueue(std::move(frame));
    }

    MatchState match_;
    MatchCoordinator coordinator_;
    PublicMatchMetadata metadata_;
    std::vector<client::PublicPlayerProfile> profiles_;
    client::MatchMode mode_{client::MatchMode::Online};
    PlayerMapLayout fullLayout_;
    std::map<PlayerId, client::ClientViewContext> viewContexts_;
    std::map<PlayerId, std::int64_t> trophyTotals_;
    std::map<PlayerId, std::weak_ptr<InMemoryMatchEndpoint>> endpoints_;
    std::size_t resolvedRoundCount_{0};
    std::optional<TrophyScoringContext> trophyScoring_;
    std::shared_ptr<PublicTrophyReadModel> leaderboard_;
    std::vector<GameEvent> trophyEvents_;
    bool trophyScoringAttempted_{false};
    std::optional<std::string> trophyScoringError_;
    std::vector<NetworkAiAgent> aiAgents_;
    std::map<PlayerId, PlayerRoundSnapshot> aiSnapshots_;
    std::uint64_t nowMs_{0};
};

InMemoryMatchEndpoint::InMemoryMatchEndpoint(
    std::shared_ptr<AuthoritativeInMemoryMatchState> state,
    PlayerId authenticatedPlayer)
    : state_(std::move(state)),
      authenticatedPlayer_(authenticatedPlayer) {}

bool InMemoryMatchEndpoint::send(const network::ClientCommand& command) {
    network::WireBytes bytes;
    std::string error;
    return network::encodeWire(command, bytes, error) &&
           sendBytes(bytes, error);
}

bool InMemoryMatchEndpoint::sendBytes(
    std::span<const std::uint8_t> bytes,
    std::string& error) {
    return state_ != nullptr &&
           state_->receive(authenticatedPlayer_, this, bytes, error);
}

PlayerId InMemoryMatchEndpoint::authenticatedPlayer() const noexcept {
    return authenticatedPlayer_;
}

std::optional<network::WireBytes>
InMemoryMatchEndpoint::takeNextServerFrame() {
    if (serverFrames_.empty()) return std::nullopt;
    network::WireBytes frame = std::move(serverFrames_.front());
    serverFrames_.erase(serverFrames_.begin());
    return frame;
}

void InMemoryMatchEndpoint::disconnect() {
    if (state_ != nullptr) state_->disconnect(authenticatedPlayer_, this);
}

void InMemoryMatchEndpoint::enqueue(network::WireBytes frame) {
    serverFrames_.push_back(std::move(frame));
}

AuthoritativeInMemoryMatch::AuthoritativeInMemoryMatch(
    std::shared_ptr<AuthoritativeInMemoryMatchState> state)
    : state_(std::move(state)) {}

std::unique_ptr<AuthoritativeInMemoryMatch>
AuthoritativeInMemoryMatch::create(
    MapSeed mapSeed,
    MatchSeed matchSeed,
    std::vector<client::PublicPlayerProfile> profiles,
    std::string& error,
    std::optional<TrophyScoringContext> trophyScoring,
    std::shared_ptr<PublicTrophyReadModel> leaderboard,
    client::MatchMode mode) {

    error.clear();
    MatchState match = MapGenerator::generate(mapSeed, matchSeed);
    const std::set<PlayerId> players = [&] {
        std::set<PlayerId> result;
        for (const PlayerState& player : match.players) result.insert(player.id);
        return result;
    }();
    std::set<PlayerId> profilePlayers;
    for (const client::PublicPlayerProfile& profile : profiles) {
        if (!players.contains(profile.player) ||
            !profilePlayers.insert(profile.player).second) {
            error = "Profiles must uniquely match authoritative players.";
            return nullptr;
        }
    }
    if (profilePlayers != players) {
        error = "One public profile is required for each player.";
        return nullptr;
    }
    if (trophyScoring.has_value() && !client::trophyEligible(mode)) {
        error = "Trophy scoring is available only for Online matches.";
        return nullptr;
    }
    if (trophyScoring.has_value()) {
        if (trophyScoring->match.value.empty() ||
            trophyScoring->ledger == nullptr ||
            trophyScoring->accounts.size() != players.size()) {
            error = "Trophy scoring requires a match ID, ledger, and one account per player.";
            return nullptr;
        }
        std::set<AccountIdentity> uniqueAccounts;
        for (const auto& [player, account] : trophyScoring->accounts) {
            if (!players.contains(player) || account.value.empty() ||
                !uniqueAccounts.insert(account).second) {
                error = "Trophy accounts must uniquely match authoritative players.";
                return nullptr;
            }
        }
    }
    auto state = std::make_shared<AuthoritativeInMemoryMatchState>(
        std::move(match), std::move(profiles), std::move(trophyScoring),
        std::move(leaderboard), mode);
    return std::unique_ptr<AuthoritativeInMemoryMatch>(
        new AuthoritativeInMemoryMatch(std::move(state)));
}

std::unique_ptr<AuthoritativeInMemoryMatch>
AuthoritativeInMemoryMatch::createSandbox(
    const client::SandboxSessionConfig& config,
    std::vector<client::PublicPlayerProfile> profiles,
    std::vector<client::ai::AiConfig> aiPlayers,
    std::string& error) {
    if (const auto invalid = client::validateOnlineSandboxSessionConfig(config)) {
        error = std::string{*invalid};
        return nullptr;
    }
    std::vector<PlayerId> roster;
    for (std::size_t index = 0; index < config.hunterCount; ++index)
        roster.push_back(static_cast<PlayerId>(index + 1));
    MatchState match;
    try {
        match = MapGenerator::generate(config.mapSeed, config.matchSeed, roster,
            client::sandboxRules(config), client::sandboxMapConfig(config));
    } catch (const std::runtime_error& exception) {
        error = exception.what();
        return nullptr;
    }
    std::set<PlayerId> expected(roster.begin(), roster.end());
    std::set<PlayerId> profilePlayers;
    for (const auto& profile : profiles) profilePlayers.insert(profile.player);
    if (profiles.size() != roster.size() || profilePlayers != expected) {
        error = "One public profile is required for each Sandbox slot.";
        return nullptr;
    }
    std::set<PlayerId> expectedAi;
    for (std::size_t slot = config.humanPlayerCount + 1;
         slot <= config.hunterCount; ++slot)
        expectedAi.insert(static_cast<PlayerId>(slot));
    std::set<PlayerId> configuredAi;
    for (const auto& ai : aiPlayers) configuredAi.insert(ai.player);
    if (configuredAi != expectedAi || configuredAi.size() != aiPlayers.size()) {
        error = "AI policies must uniquely match the configured Sandbox AI slots.";
        return nullptr;
    }
    auto state = std::make_shared<AuthoritativeInMemoryMatchState>(
        std::move(match), std::move(profiles), std::nullopt, nullptr,
        client::MatchMode::Sandbox, std::move(aiPlayers));
    error.clear();
    return std::unique_ptr<AuthoritativeInMemoryMatch>(
        new AuthoritativeInMemoryMatch(std::move(state)));
}

std::shared_ptr<InMemoryMatchEndpoint>
AuthoritativeInMemoryMatch::connect(
    PlayerId authenticatedPlayer,
    std::string& error) {

    error.clear();
    if (!state_->containsPlayer(authenticatedPlayer)) {
        error = "Cannot authenticate an unknown player.";
        return nullptr;
    }
    if (state_->hasEndpoint(authenticatedPlayer)) {
        error = "Player already has an attached endpoint.";
        return nullptr;
    }
    auto endpoint = std::shared_ptr<InMemoryMatchEndpoint>(
        new InMemoryMatchEndpoint(state_, authenticatedPlayer));
    state_->attach(authenticatedPlayer, endpoint);
    if (!state_->enqueueBootstrap(authenticatedPlayer, *endpoint, error))
        return nullptr;
    return endpoint;
}

std::shared_ptr<InMemoryMatchEndpoint>
AuthoritativeInMemoryMatch::reconnect(
    PlayerId authenticatedPlayer,
    std::string& error) {
    error.clear();
    auto endpoint = std::shared_ptr<InMemoryMatchEndpoint>(
        new InMemoryMatchEndpoint(state_, authenticatedPlayer));
    if (!state_->reconnect(authenticatedPlayer, endpoint, error)) return nullptr;
    return endpoint;
}

RoundNumber AuthoritativeInMemoryMatch::authoritativeRound() const noexcept {
    return state_->round();
}

std::size_t AuthoritativeInMemoryMatch::resolvedRoundCount() const noexcept {
    return state_->resolvedRoundCount();
}

std::optional<std::string>
AuthoritativeInMemoryMatch::trophyScoringError() const {
    return state_->trophyScoringError();
}

std::uint64_t AuthoritativeInMemoryMatch::disconnectGraceMs() const noexcept {
    return state_->disconnectGraceMs();
}

void AuthoritativeInMemoryMatch::advanceTime(std::uint64_t elapsedMs) {
    state_->advanceTime(elapsedMs);
}

} // namespace basilisk::game::server
