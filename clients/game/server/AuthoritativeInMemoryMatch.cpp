#include "AuthoritativeInMemoryMatch.hpp"

#include <algorithm>
#include <map>
#include <set>
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

class AuthoritativeInMemoryMatchState {
public:
    AuthoritativeInMemoryMatchState(
        MatchState match,
        std::vector<client::PublicPlayerProfile> profiles,
        std::optional<TrophyScoringContext> trophyScoring)
        : match_(std::move(match)),
          coordinator_(match_),
          metadata_(PublicMatchMetadataSystem::build(match_)),
          profiles_(std::move(profiles)),
          trophyScoring_(std::move(trophyScoring)) {

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
        return true;
    }

    [[nodiscard]] bool receive(
        PlayerId authenticatedPlayer,
        std::span<const std::uint8_t> bytes,
        std::string& error) {

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

    void advanceTime(std::uint64_t elapsedMs) {
        const RoundNumber priorRound = match_.round;
        coordinator_.advanceTime(elapsedMs);
        if (match_.round != priorRound) ++resolvedRoundCount_;
        recordTrophyEvents(coordinator_.authoritativeEvents());
        refreshContexts();
        publishAll(coordinator_.authoritativeEvents());
    }

private:
    [[nodiscard]] bool handle(
        PlayerId authenticatedPlayer,
        const network::SubmitActionCommand& command,
        std::string& error) {

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

        if (command.player != authenticatedPlayer) {
            error = "Authenticated player does not match action lock.";
            return false;
        }
        const RoundNumber priorRound = match_.round;
        if (!coordinator_.lockAction(authenticatedPlayer)) {
            error = "Coordinator rejected action lock.";
            return false;
        }
        recordTrophyEvents(coordinator_.authoritativeEvents());
        if (match_.round != priorRound) {
            ++resolvedRoundCount_;
            refreshContexts();
            publishAll(coordinator_.authoritativeEvents());
        }
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
        coordinator_.disconnect(authenticatedPlayer);
        recordTrophyEvents(coordinator_.authoritativeEvents());
        refreshContexts();
        publishAll(coordinator_.authoritativeEvents());
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
            if (context.mode == client::ClientViewMode::Spectating) continue;
            context.localPlayer = localPlayer;
            context.viewedPlayer = localPlayer;
            context.mode = client::ClientViewMode::Defeated;
            context.spectatablePlayer.reset();
            if (match_.result.status != MatchStatus::Active) continue;
            const auto survivor = std::find_if(
                match_.players.begin(), match_.players.end(),
                [localPlayer](const PlayerState& player) {
                    return player.id != localPlayer && player.alive;
                });
            if (survivor != match_.players.end())
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
    PlayerMapLayout fullLayout_;
    std::map<PlayerId, client::ClientViewContext> viewContexts_;
    std::map<PlayerId, std::int64_t> trophyTotals_;
    std::map<PlayerId, std::weak_ptr<InMemoryMatchEndpoint>> endpoints_;
    std::size_t resolvedRoundCount_{0};
    std::optional<TrophyScoringContext> trophyScoring_;
    std::vector<GameEvent> trophyEvents_;
    bool trophyScoringAttempted_{false};
    std::optional<std::string> trophyScoringError_;
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
           state_->receive(authenticatedPlayer_, bytes, error);
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
    std::optional<TrophyScoringContext> trophyScoring) {

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
        std::move(match), std::move(profiles), std::move(trophyScoring));
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

void AuthoritativeInMemoryMatch::advanceTime(std::uint64_t elapsedMs) {
    state_->advanceTime(elapsedMs);
}

} // namespace basilisk::game::server
