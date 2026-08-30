#include "LocalSandboxSessionAdapter.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "ActionCommands.hpp"
#include "ClientLifecycle.hpp"
#include "MapLayout.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/client/MatchMode.hpp"
#include "basilisk/client/PlayerProfile.hpp"
#include "basilisk/client/SandboxConfiguration.hpp"
#include "basilisk/client/ai/AiPolicy.hpp"
#include "basilisk/client/ai/AiKnowledgeState.hpp"
#include "basilisk/client/ai/AiTurnScheduler.hpp"
#include "basilisk/systems/MatchCoordinator.hpp"
#include "basilisk/systems/PublicMatchMetadataSystem.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/world/MapGenerator.hpp"

namespace basilisk::game {
namespace {

std::uint64_t mixSeed(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
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
                static_cast<TunnelId>(index + 1), connections[index], false});
        }
        map.caves.push_back(std::move(view));
    }
    return map;
}

PlayerFixedMapGeometry geometryFor(const MatchState& state,
    const PlayerMapLayout& layout, const PlayerRoundSnapshot& snapshot) {
    PlayerFixedMapGeometry geometry;
    geometry.fullBounds = layout.positionedBounds();
    for (const auto& cave : snapshot.map.caves) {
        if (const auto point = layout.cavePosition(cave.cave))
            geometry.discoveredCaves.emplace(cave.cave, *point);
        if (!state.world.contains(cave.cave)) continue;
        const auto& physical = state.world.cave(cave.cave).connections;
        for (const auto& exit : cave.exits) {
            if (exit.destination || exit.id == 0 || exit.id > physical.size()) continue;
            if (const auto point = layout.cavePosition(physical[exit.id - 1]))
                geometry.unknownExitEndpoints.emplace(
                    MapExitKey{cave.cave, exit.id}, *point);
        }
    }
    for (const CaveId cave : snapshot.temporarilyRevealedPitCaves) {
        if (const auto point = layout.cavePosition(cave))
            geometry.temporarilyRevealedCaves.emplace(cave, *point);
    }
    return geometry;
}

struct SandboxAgent {
    PlayerId player{};
    client::ai::AiConfig config;
    client::ai::RuntimeAiPolicy policy;
    client::ai::AiKnowledgeState knowledge;
    client::ai::AiTurnScheduler scheduler;
    std::optional<RoundNumber> decisionRound;

    SandboxAgent(client::ai::AiConfig value,
        const client::ai::RuntimeAiPolicyConfig& policyConfig)
        : player(value.player), config(value), policy(policyConfig) {}
};

class LocalSandboxMatchState {
public:
    LocalSandboxMatchState(MatchState state, PlayerId human,
        std::vector<client::ai::AiConfig> configs,
        client::ai::RuntimeAiPolicyConfig policy)
        : state_(std::move(state)), coordinator_(state_), human_(human),
          policyConfig_(policy) {
        agents_.reserve(configs.size());
        for (auto& config : configs)
            agents_.emplace_back(config, policy);
        const PlayerMapView physical = fullPhysicalMap(state_);
        layout_.update(physical);
        layout_.finalizeFullLayout(physical);
    }

    void attach(ClientSessionController& controller) {
        controller_ = &controller;
        publish({});
        scheduleActions();
    }

    bool submit(const PlayerAction& action) {
        return action.player == human_ && coordinator_.submitAction(action);
    }

    bool lock(PlayerId player) {
        if (player != human_) return false;
        return lockCoordinator(player);
    }

    bool submitClash(PlayerId player, ClashId clash, std::string response) {
        if (player != human_) return false;
        const RoundNumber priorRound = state_.round;
        const auto result = coordinator_.submitClashResponse(
            player, clash, std::move(response));
        if (result == ClashSubmissionResult::Rejected) return false;
        if (result == ClashSubmissionResult::Resolved) afterMutation(priorRound);
        return true;
    }

    void advance(std::uint64_t elapsedMs) {
        nowMs_ += elapsedMs;
        const RoundNumber priorRound = state_.round;
        const ClashId priorClash = activeClashId();
        coordinator_.advanceTime(elapsedMs);
        if (state_.round != priorRound || activeClashId() != priorClash)
            afterMutation(priorRound);

        if (coordinator_.activeClash() != nullptr) {
            scheduleClashResponses();
            submitNextDueClashResponse();
            return;
        }

        scheduleActions();
        submitDueActions();
    }

#if defined(BASILISK_GAME_DEBUG_BUILD)
    [[nodiscard]] debug::DebugMapTruth debugMapTruth() const {
        return debug::buildDebugMapTruth(state_, layout_);
    }
    [[nodiscard]] debug::DebugGameplayTruth debugGameplayTruth() const {
        std::vector<debug::DebugHunterLabel> labels;
        labels.push_back({human_, "HOST"});
        for (const auto& agent : agents_)
            labels.push_back({agent.player, "AI " + std::to_string(agent.player)});
        return debug::buildDebugGameplayTruth(state_, labels);
    }
    [[nodiscard]] bool forceBasiliskBehavior(BasiliskBehavior behavior) {
        state_.basilisk.behavior = behavior;
        state_.basilisk.roundsSinceMove = 0;
        publish({});
        return true;
    }
    [[nodiscard]] bool grantItem(PlayerId target, ItemType item) {
        const auto player = std::find_if(state_.players.begin(), state_.players.end(),
            [&](const PlayerState& candidate) { return candidate.id == target; });
        if (player == state_.players.end() || !player->alive ||
            !player->inventory.add(ItemInstance{item},
                state_.rules.maxInventoryItems)) return false;
        publish({});
        return true;
    }
    [[nodiscard]] bool killPlayer(PlayerId victim) {
        const auto player = std::find_if(state_.players.begin(), state_.players.end(),
            [&](const PlayerState& candidate) { return candidate.id == victim; });
        if (player == state_.players.end() || !player->alive ||
            state_.result.status != MatchStatus::Active) return false;
        coordinator_.forfeit(victim);
        for (auto& agent : agents_) {
            if (agent.player == victim) agent.scheduler.clear();
        }
        publish(coordinator_.authoritativeEvents());
        scheduleActions();
        return true;
    }
    [[nodiscard]] std::vector<debug::DebugParticipant> debugParticipants() const {
        std::vector<debug::DebugParticipant> result;
        result.reserve(state_.players.size());
        for (std::size_t index = 0; index < state_.players.size(); ++index) {
            const PlayerState& player = state_.players[index];
            result.push_back({player.id, index == 0 ? "HOST" :
                "AI " + std::to_string(index + 1), player.alive});
        }
        return result;
    }
#endif

private:
    [[nodiscard]] ClashId activeClashId() const noexcept {
        return coordinator_.activeClash() == nullptr
            ? ClashId{} : coordinator_.activeClash()->id;
    }

    bool lockCoordinator(PlayerId player) {
        const RoundNumber priorRound = state_.round;
        if (!coordinator_.lockAction(player)) return false;
        afterMutation(priorRound);
        return true;
    }

    void afterMutation(RoundNumber priorRound) {
        if (coordinator_.activeClash() != nullptr) {
            for (auto& agent : agents_) agent.scheduler.clearAction();
            scheduleClashResponses();
        } else {
            for (auto& agent : agents_) agent.scheduler.clearClash();
        }
        if (state_.round != priorRound || coordinator_.activeClash() != nullptr)
            publish(coordinator_.authoritativeEvents());
        if (state_.round != priorRound) scheduleActions();
    }

    [[nodiscard]] bool agentAlive(PlayerId id) const {
        const auto player = std::find_if(state_.players.begin(), state_.players.end(),
            [id](const PlayerState& candidate) { return candidate.id == id; });
        return player != state_.players.end() && player->alive;
    }

    void scheduleActions() {
        if (controller_ == nullptr || coordinator_.activeClash() != nullptr ||
            state_.result.status != MatchStatus::Active) return;
        for (auto& agent : agents_) {
            if (!agentAlive(agent.player)) {
                agent.scheduler.clear();
                continue;
            }
            if (agent.decisionRound == state_.round) continue;
            const auto snapshotEntry = aiSnapshots_.find(agent.player);
            if (snapshotEntry == aiSnapshots_.end() ||
                snapshotEntry->second.round != state_.round ||
                snapshotEntry->second.availableActions.empty()) continue;
            const PlayerRoundSnapshot* snapshot = &snapshotEntry->second;
            agent.decisionRound = state_.round;
            agent.knowledge.observe(*snapshot);
            const auto observation = client::ai::makePolicyObservation(
                *snapshot, agent.knowledge, agent.config);
            if (observation.legalActions.empty()) continue;
            const auto selection = agent.policy.select(observation, agent.config);
            const auto& action = client::ai::resolvePolicyDecision(
                observation, selection.authoritative, agent.config).action;
            agent.scheduler.scheduleAction(action, nowMs_, agent.config, state_.round);
        }
    }

    void submitDueActions() {
        std::vector<SandboxAgent*> due;
        for (auto& agent : agents_) {
            if (agent.scheduler.actionDeadline().has_value() &&
                *agent.scheduler.actionDeadline() <= nowMs_) due.push_back(&agent);
        }
        std::stable_sort(due.begin(), due.end(), [](const auto* left, const auto* right) {
            if (left->scheduler.actionDeadline() != right->scheduler.actionDeadline())
                return left->scheduler.actionDeadline() < right->scheduler.actionDeadline();
            return left->player < right->player;
        });
        for (SandboxAgent* agent : due) {
            if (coordinator_.activeClash() != nullptr ||
                state_.result.status != MatchStatus::Active) break;
            const auto action = agent->scheduler.takeDueAction(nowMs_);
            if (!action.has_value()) continue;
            PlayerAction command = makePlayerAction(*action, agent->player);
            if (coordinator_.submitAction(command)) {
                agent->knowledge.recordDecision(*action);
                (void)lockCoordinator(agent->player);
            }
        }
    }

    void scheduleClashResponses() {
        const ActiveClash* clash = coordinator_.activeClash();
        if (clash == nullptr) return;
        for (auto& agent : agents_) {
            const bool participates = std::find(clash->participants.begin(),
                clash->participants.end(), agent.player) != clash->participants.end();
            if (!participates) {
                agent.scheduler.clearClash();
                continue;
            }
            if (!agent.scheduler.clashDeadline().has_value()) {
                agent.scheduler.scheduleClash(clash->id, clash->challengeWord,
                    nowMs_, agent.config);
            }
        }
    }

    void submitNextDueClashResponse() {
        const ActiveClash* clash = coordinator_.activeClash();
        if (clash == nullptr) return;
        SandboxAgent* selected = nullptr;
        for (auto& agent : agents_) {
            if (!agent.scheduler.clashDeadline().has_value() ||
                *agent.scheduler.clashDeadline() > nowMs_) continue;
            if (selected == nullptr || agent.scheduler.clashDeadline() <
                    selected->scheduler.clashDeadline() ||
                (agent.scheduler.clashDeadline() == selected->scheduler.clashDeadline() &&
                 agent.player < selected->player)) selected = &agent;
        }
        if (selected == nullptr) return;
        const auto response = selected->scheduler.takeDueClash(nowMs_);
        if (!response.has_value() || response->clash != clash->id) return;
        const RoundNumber priorRound = state_.round;
        const auto result = coordinator_.submitClashResponse(selected->player,
            response->clash, response->response);
        if (result == ClashSubmissionResult::Resolved) afterMutation(priorRound);
    }

    void refreshView() {
        if (controller_ == nullptr) return;
        auto context = controller_->viewContext();
        const auto human = std::find_if(state_.players.begin(), state_.players.end(),
            [&](const PlayerState& player) { return player.id == human_; });
        if (human == state_.players.end() || human->alive) return;
        std::vector<PlayerId> survivors;
        for (const PlayerState& player : state_.players) {
            if (player.alive && player.id != human_) survivors.push_back(player.id);
        }
        const std::optional<PlayerId> firstSurvivor = survivors.empty()
            ? std::nullopt : std::optional<PlayerId>{survivors.front()};
        if (context.mode == client::ClientViewMode::Playing) {
            context.mode = client::ClientViewMode::Defeated;
            context.viewedPlayer = human_;
            context.spectatablePlayer = state_.result.status == MatchStatus::Active
                ? firstSurvivor : std::nullopt;
            controller_->setViewContext(context);
            return;
        }
        if (context.mode == client::ClientViewMode::Spectating) {
            const bool viewedAlive = std::find(
                survivors.begin(), survivors.end(), context.viewedPlayer) != survivors.end();
            if (!viewedAlive && firstSurvivor.has_value())
                context.viewedPlayer = *firstSurvivor;
            context.spectatablePlayer = state_.result.status == MatchStatus::Active
                ? firstSurvivor : std::nullopt;
            controller_->setViewContext(context);
        }
    }

    void publish(const std::vector<GameEvent>& events) {
        if (controller_ == nullptr) return;
        if (!outcomeReported_ && state_.result.status == MatchStatus::Completed &&
            policyConfig_.mode == client::ai::RuntimeAiPolicyMode::Shadow &&
            policyConfig_.telemetry != nullptr) {
            policyConfig_.telemetry->recordOutcome(policyConfig_.context,
                state_.result.outcome, state_.result.winner);
            outcomeReported_ = true;
        }
        refreshView();
        for (const PlayerState& player : state_.players) {
            PlayerRoundSnapshot snapshot = SnapshotSystem::buildForPlayer(
                state_, player.id, events);
            if (player.id != human_) aiSnapshots_[player.id] = snapshot;
            auto geometry = geometryFor(state_, layout_, snapshot);
            (void)controller_->ingestSnapshot(std::move(snapshot), std::move(geometry));
        }
        const ActiveClash* clash = coordinator_.activeClash();
        controller_->setActiveClash(clash == nullptr
            ? std::nullopt : std::optional<ActiveClash>{*clash});
    }

    MatchState state_;
    MatchCoordinator coordinator_;
    PlayerMapLayout layout_;
    PlayerId human_{};
    client::ai::RuntimeAiPolicyConfig policyConfig_;
    std::vector<SandboxAgent> agents_;
    std::map<PlayerId, PlayerRoundSnapshot> aiSnapshots_;
    std::uint64_t nowMs_{};
    bool outcomeReported_{};
    ClientSessionController* controller_{nullptr};
};

class SandboxActionSink final : public ActionCommandSink {
public:
    explicit SandboxActionSink(std::shared_ptr<LocalSandboxMatchState> state)
        : state_(std::move(state)) {}
    bool submitAction(const PlayerAction& action) override {
        return state_->submit(action);
    }
    bool lockAction(PlayerId player) override { return state_->lock(player); }
    bool submitClashResponse(PlayerId player, ClashId clash,
        std::string response) override {
        return state_->submitClash(player, clash, std::move(response));
    }
private:
    std::shared_ptr<LocalSandboxMatchState> state_;
};

class SandboxSessionCommands final : public ClientSessionCommandSink {
public:
    bool quitGame(PlayerId) override { return true; }
};

class SandboxDriver final : public LocalSandboxSessionDriver {
public:
    explicit SandboxDriver(std::shared_ptr<LocalSandboxMatchState> state)
        : state_(std::move(state)) {}
    void advance(std::uint64_t elapsedMs) override { state_->advance(elapsedMs); }
private:
    std::shared_ptr<LocalSandboxMatchState> state_;
};

} // namespace

LocalSandboxSession LocalSandboxSessionAdapter::create(
    const client::SandboxSessionConfig& config,
    client::ai::RuntimeAiPolicyConfig policy) {
    if (validateSandboxSessionConfig(config).has_value() ||
        config.humanPlayerCount != 1)
        return {};
    const std::size_t hunterCount = config.hunterCount;
    std::vector<PlayerId> roster;
    roster.reserve(hunterCount);
    for (std::size_t index = 0; index < hunterCount; ++index)
        roster.push_back(static_cast<PlayerId>(index + 1));
    std::optional<MatchState> generated;
    try {
        generated = MapGenerator::generate(config.mapSeed, config.matchSeed,
            roster, client::sandboxRules(config), client::sandboxMapConfig(config));
    } catch (const std::runtime_error&) {
        return {};
    }
    MatchState match = std::move(*generated);
    if (match.players.size() != hunterCount) return {};
    const PlayerId human = roster.front();

    std::vector<client::ai::AiConfig> configs;
    std::vector<client::ai::AiBehavior> resolvedBehaviors;
    std::vector<client::PublicPlayerProfile> profiles;
    profiles.reserve(hunterCount);
    profiles.push_back({human, "Local Hunter", {"arrow-right-black"}, {"circle-black"}});
    for (std::size_t index = 1; index < roster.size(); ++index) {
        const auto seed = client::ai::AiSeed{
            mixSeed(config.aiSeed ^ static_cast<std::uint64_t>(roster[index]))};
        const auto resolved = client::ai::resolveBehavior(config.aiBehavior, seed);
        configs.push_back({config.aiDifficulty, resolved, roster[index], seed});
        resolvedBehaviors.push_back(resolved);
        profiles.push_back({roster[index], "BASILISK AI " + std::to_string(index + 1),
            {"honeycomb-flag-black"}, {"circle-green"}});
    }

    PublicMatchMetadata metadata = PublicMatchMetadataSystem::build(match);
    const client::ClientViewContext view{
        human, human, client::ClientViewMode::Playing, std::nullopt};
    policy.context = "local-sandbox-" + std::to_string(config.mapSeed) + "-" +
        std::to_string(config.matchSeed);
    auto state = std::make_shared<LocalSandboxMatchState>(
        std::move(match), human, configs, std::move(policy));
    auto session = std::make_unique<ClientSessionController>(
        std::move(metadata), std::move(profiles), view,
        std::make_unique<SandboxActionSink>(state),
        std::make_unique<SandboxSessionCommands>());
    session->setMatchMode(client::MatchMode::Sandbox);
    for (std::size_t index = 0; index < configs.size(); ++index) {
        session->setParticipantSubtitle(configs[index].player,
            std::string{client::ai::difficultyName(config.aiDifficulty)} + " \xC2\xB7 " +
            client::ai::behaviorName(configs[index].behavior));
    }
    state->attach(*session);

    LocalSandboxSession result;
    result.session = std::move(session);
    result.driver = std::make_unique<SandboxDriver>(state);
    result.resolvedBehaviors = std::move(resolvedBehaviors);
#if defined(BASILISK_GAME_DEBUG_BUILD)
    result.mapProvider = std::make_unique<debug::DebugMapProvider>(
        state->debugMapTruth(),
        [state] { return state->debugGameplayTruth(); },
        [state](BasiliskBehavior next) {
            return state->forceBasiliskBehavior(next);
        },
        [state](PlayerId player, ItemType item) {
            return state->grantItem(player, item);
        },
        [state](PlayerId player) { return state->killPlayer(player); },
        [state] { return state->debugParticipants(); });
#endif
    return result;
}

} // namespace basilisk::game
