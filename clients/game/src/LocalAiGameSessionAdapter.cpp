#include "LocalAiGameSessionAdapter.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

#include "ActionCommands.hpp"
#include "ClientLifecycle.hpp"
#include "MapLayout.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/client/MatchMode.hpp"
#include "basilisk/client/PlayerProfile.hpp"
#include "basilisk/client/ai/AiTurnScheduler.hpp"
#include "basilisk/client/ai/AiKnowledgeState.hpp"
#include "basilisk/systems/MatchCoordinator.hpp"
#include "basilisk/systems/PublicMatchMetadataSystem.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/world/MapGenerator.hpp"

namespace basilisk::game {
namespace {

PlayerMapView fullPhysicalMap(const MatchState& state) {
    PlayerMapView map;
    if (!state.players.empty()) map.currentCave = state.players.front().cave;
    for (const CaveId cave : state.world.caveIds()) {
        DiscoveredCaveView view; view.cave = cave;
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
    PlayerFixedMapGeometry geometry; geometry.fullBounds = layout.positionedBounds();
    for (const auto& cave : snapshot.map.caves) {
        if (const auto point = layout.cavePosition(cave.cave))
            geometry.discoveredCaves.emplace(cave.cave, *point);
        if (!state.world.contains(cave.cave)) continue;
        const auto& physical = state.world.cave(cave.cave).connections;
        for (const auto& exit : cave.exits) {
            if (exit.destination || exit.id == 0 || exit.id > physical.size()) continue;
            if (const auto point = layout.cavePosition(physical[exit.id - 1]))
                geometry.unknownExitEndpoints.emplace(MapExitKey{cave.cave, exit.id}, *point);
        }
    }
    for (const CaveId cave : snapshot.temporarilyRevealedPitCaves)
        if (const auto point = layout.cavePosition(cave))
            geometry.temporarilyRevealedCaves.emplace(cave, *point);
    return geometry;
}

#if defined(BASILISK_GAME_DEBUG_BUILD)
std::string debugActionName(const AvailableAction& action) {
    switch (action.type) {
        case ActionType::Move:
            return "MOVE " + std::to_string(action.targetCave.value_or(0));
        case ActionType::Search: return "SEARCH";
        case ActionType::Shoot:
            return "SHOOT " + std::to_string(action.targetCave.value_or(0));
        case ActionType::UseItem: return "ITEM";
        case ActionType::Contextual: return "CONTEXT";
    }
    return "ACTION";
}
#endif

class LocalAiMatchState {
public:
    LocalAiMatchState(MatchState state, PlayerId human, PlayerId ai,
        client::ai::AiConfig config)
        : state_(std::move(state)), coordinator_(state_), human_(human), ai_(ai),
          config_(config) {
        const PlayerMapView physical = fullPhysicalMap(state_);
        layout_.update(physical); layout_.finalizeFullLayout(physical);
    }

    void attach(ClientSessionController& controller) {
        controller_ = &controller;
        publish({});
        scheduleAiAction();
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
        const auto result = coordinator_.submitClashResponse(player, clash, response);
        if (result == ClashSubmissionResult::Rejected) return false;
        if (result == ClashSubmissionResult::Resolved) afterMutation(priorRound);
        return true;
    }

    void advance(std::uint64_t elapsedMs) {
        nowMs_ += elapsedMs;
        const RoundNumber priorRound = state_.round;
        const bool hadClash = coordinator_.activeClash() != nullptr;
        coordinator_.advanceTime(elapsedMs);
        if (state_.round != priorRound ||
            hadClash != (coordinator_.activeClash() != nullptr)) afterMutation(priorRound);

        if (const auto response = scheduler_.takeDueClash(nowMs_)) {
            const RoundNumber before = state_.round;
            if (coordinator_.activeClash() != nullptr &&
                coordinator_.activeClash()->id == response->clash) {
                (void)coordinator_.submitClashResponse(ai_, response->clash, response->response);
                afterMutation(before);
            }
        }
        if (coordinator_.activeClash() == nullptr) {
            scheduleAiAction();
            if (const auto action = scheduler_.takeDueAction(nowMs_)) {
                PlayerAction command = makePlayerAction(*action, ai_);
                if (coordinator_.submitAction(command)) {
#if defined(BASILISK_GAME_DEBUG_BUILD)
                    lastAiAction_ = debugActionName(*action);
#endif
                    knowledge_.recordDecision(*action);
                    (void)lockCoordinator(ai_);
                }
            }
        }
    }

#if defined(BASILISK_GAME_DEBUG_BUILD)
    [[nodiscard]] debug::DebugMapTruth debugMapTruth() const {
        return debug::buildDebugMapTruth(state_, layout_);
    }
    [[nodiscard]] debug::DebugGameplayTruth debugGameplayTruth() const {
        const std::array labels{debug::DebugHunterLabel{ai_, "AI"}};
        auto truth = debug::buildDebugGameplayTruth(state_, labels);
        truth.aiDecisionTrace = aiDecisionTrace_;
        truth.aiDecisionTrace.insert(truth.aiDecisionTrace.begin(),
            "AI LAST  " + lastAiAction_);
        return truth;
    }
    [[nodiscard]] bool forceBasiliskBehavior(BasiliskBehavior behavior) {
        state_.basilisk.behavior = behavior;
        state_.basilisk.roundsSinceMove = 0;
        publish({});
        return true;
    }
    [[nodiscard]] bool grantItem(ItemType item) {
        const auto player = std::find_if(state_.players.begin(), state_.players.end(),
            [&](const PlayerState& candidate) { return candidate.id == human_; });
        if (player == state_.players.end() || !player->alive ||
            !player->inventory.add(ItemInstance{item}, state_.rules.maxInventoryItems))
            return false;
        publish({});
        return true;
    }
    [[nodiscard]] std::vector<debug::DebugParticipant> debugParticipants() const {
        std::vector<debug::DebugParticipant> result;
        for (const auto [player, label] :
             {std::pair{human_, std::string{"HOST"}},
              std::pair{ai_, std::string{"AI"}}}) {
            const auto state = std::find_if(state_.players.begin(), state_.players.end(),
                [&](const PlayerState& candidate) { return candidate.id == player; });
            result.push_back({player, label, state != state_.players.end() && state->alive});
        }
        return result;
    }
    [[nodiscard]] bool killPlayer(debug::DebugKillTarget target) {
        const PlayerId victim = target == debug::DebugKillTarget::Host ? human_ : ai_;
        const auto player = std::find_if(state_.players.begin(), state_.players.end(),
            [&](const PlayerState& candidate) { return candidate.id == victim; });
        if (player == state_.players.end() || !player->alive ||
            state_.result.status != MatchStatus::Active) return false;
        coordinator_.forfeit(victim);
        if (victim == ai_) scheduler_.clear();
        publish(coordinator_.authoritativeEvents());
        scheduleAiAction();
        return true;
    }
#endif

private:
    bool lockCoordinator(PlayerId player) {
        const RoundNumber priorRound = state_.round;
        if (!coordinator_.lockAction(player)) return false;
        afterMutation(priorRound);
        return true;
    }

    void afterMutation(RoundNumber priorRound) {
        const ActiveClash* clash = coordinator_.activeClash();
        if (clash != nullptr) {
            scheduler_.clearAction();
            if (std::find(clash->participants.begin(), clash->participants.end(), ai_) !=
                    clash->participants.end() && !scheduler_.clashDeadline()) {
                scheduler_.scheduleClash(
                    clash->id, clash->challengeWord, nowMs_, config_);
            }
        } else {
            scheduler_.clearClash();
        }
        if (state_.round != priorRound || clash != nullptr)
            publish(coordinator_.authoritativeEvents());
        if (state_.round != priorRound) scheduleAiAction();
    }

    void scheduleAiAction() {
        const auto player = std::find_if(state_.players.begin(), state_.players.end(),
            [&](const PlayerState& candidate) { return candidate.id == ai_; });
        if (player == state_.players.end() || !player->alive ||
            state_.result.status != MatchStatus::Active) return;
        if (coordinator_.activeClash() != nullptr || decisionRound_ == state_.round) return;
        if (controller_ == nullptr) return;
        const PlayerRoundSnapshot* snapshot = controller_->snapshotFor(ai_);
        if (snapshot == nullptr || snapshot->round != state_.round) return;
        decisionRound_ = state_.round;
        knowledge_.observe(*snapshot);
        const auto evaluation = engine_.evaluate(*snapshot, config_, knowledge_);
#if defined(BASILISK_GAME_DEBUG_BUILD)
        aiDecisionTrace_.clear();
        aiDecisionTrace_.push_back(
            "BASILISK  " + std::to_string(evaluation.basiliskCandidates) +
            (evaluation.basiliskAdjacentEvidence ? " ADJ" :
             evaluation.basiliskDistantEvidence ? " DISTANT" : " NONE"));
        aiDecisionTrace_.push_back(std::string{"SIGIL  "} +
            (evaluation.sigilRecoverable ? "RECOVERABLE" : "NONE"));
        std::vector<client::ai::AiActionUtility> ranked = evaluation.actions;
        std::stable_sort(ranked.begin(), ranked.end(),
            [](const auto& left, const auto& right) {
                return left.utility > right.utility;
            });
        std::ostringstream scores;
        scores << "TOP";
        for (std::size_t index = 0; index < std::min<std::size_t>(3, ranked.size()); ++index)
            scores << "  " << debugActionName(ranked[index].action) << " "
                   << static_cast<int>(ranked[index].utility);
        aiDecisionTrace_.push_back(scores.str());
#endif
        if (!evaluation.actions.empty()) {
            scheduler_.scheduleAction(evaluation.actions[evaluation.chosenIndex].action,
                nowMs_, config_, state_.round);
        }
    }

    void refreshView() {
        if (controller_ == nullptr) return;
        auto context = controller_->viewContext();
        const auto human = std::find_if(state_.players.begin(), state_.players.end(),
            [&](const PlayerState& player) { return player.id == human_; });
        const auto ai = std::find_if(state_.players.begin(), state_.players.end(),
            [&](const PlayerState& player) { return player.id == ai_; });
        if (human != state_.players.end() && !human->alive &&
            context.mode == client::ClientViewMode::Playing) {
            context.mode = client::ClientViewMode::Defeated;
            context.viewedPlayer = human_;
            context.spectatablePlayer = ai != state_.players.end() && ai->alive &&
                state_.result.status == MatchStatus::Active
                ? std::optional<PlayerId>{ai_} : std::nullopt;
            controller_->setViewContext(context);
        }
    }

    void publish(const std::vector<GameEvent>& events) {
        if (controller_ == nullptr) return;
        refreshView();
        for (const PlayerState& player : state_.players) {
            PlayerRoundSnapshot snapshot = SnapshotSystem::buildForPlayer(
                state_, player.id, events);
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
    PlayerId ai_{};
    client::ai::AiConfig config_;
    client::ai::AiDecisionEngine engine_;
    client::ai::AiKnowledgeState knowledge_;
    client::ai::AiTurnScheduler scheduler_;
    std::optional<RoundNumber> decisionRound_;
    std::uint64_t nowMs_{};
    ClientSessionController* controller_{nullptr};
#if defined(BASILISK_GAME_DEBUG_BUILD)
    std::vector<std::string> aiDecisionTrace_;
    std::string lastAiAction_{"NONE"};
#endif
};

class LocalAiActionSink final : public ActionCommandSink {
public:
    explicit LocalAiActionSink(std::shared_ptr<LocalAiMatchState> state)
        : state_(std::move(state)) {}
    bool submitAction(const PlayerAction& action) override { return state_->submit(action); }
    bool lockAction(PlayerId player) override { return state_->lock(player); }
    bool submitClashResponse(PlayerId player, ClashId clash, std::string response) override {
        return state_->submitClash(player, clash, std::move(response));
    }
private:
    std::shared_ptr<LocalAiMatchState> state_;
};

class LocalAiSessionCommands final : public ClientSessionCommandSink {
public:
    bool quitGame(PlayerId) override { return true; }
};

class Driver final : public LocalAiSessionDriver {
public:
    explicit Driver(std::shared_ptr<LocalAiMatchState> state) : state_(std::move(state)) {}
    void advance(std::uint64_t elapsedMs) override { state_->advance(elapsedMs); }
private:
    std::shared_ptr<LocalAiMatchState> state_;
};

} // namespace

LocalAiSession LocalAiGameSessionAdapter::create(MapSeed mapSeed, MatchSeed matchSeed,
    client::ai::AiDifficulty difficulty, client::ai::AiBehavior behavior,
    client::ai::AiSeed aiSeed) {
    MatchState match = MapGenerator::generate(mapSeed, matchSeed);
    if (match.players.size() < 2) return {};
    const PlayerId human = match.players[0].id;
    const PlayerId ai = match.players[1].id;
    const auto resolved = client::ai::resolveBehavior(behavior, aiSeed);
    const client::ai::AiConfig config{difficulty, resolved, ai, aiSeed};
    PublicMatchMetadata metadata = PublicMatchMetadataSystem::build(match);
    std::vector<client::PublicPlayerProfile> profiles{
        {human, "Local Hunter", {"arrow-right-black"}, {"circle-black"}},
        {ai, "BASILISK AI", {"honeycomb-flag-black"}, {"circle-green"}},
    };
    const client::ClientViewContext view{
        human, human, client::ClientViewMode::Playing, std::nullopt};
    auto state = std::make_shared<LocalAiMatchState>(
        std::move(match), human, ai, config);
    auto session = std::make_unique<ClientSessionController>(
        std::move(metadata), std::move(profiles), view,
        std::make_unique<LocalAiActionSink>(state),
        std::make_unique<LocalAiSessionCommands>());
    session->setMatchMode(client::MatchMode::AI);
    session->setParticipantSubtitle(ai,
        std::string{client::ai::difficultyName(difficulty)} + " \xC2\xB7 " +
        client::ai::behaviorName(resolved));
    state->attach(*session);
    LocalAiSession result;
    result.session = std::move(session);
    result.driver = std::make_unique<Driver>(state);
    result.resolvedBehavior = resolved;
#if defined(BASILISK_GAME_DEBUG_BUILD)
    result.mapProvider = std::make_unique<debug::DebugMapProvider>(
        state->debugMapTruth(),
        [state] { return state->debugGameplayTruth(); },
        [state](BasiliskBehavior next) {
            return state->forceBasiliskBehavior(next);
        },
        [state, human](PlayerId player, ItemType item) {
            return player == human && state->grantItem(item);
        },
        [state, human, ai](PlayerId player) {
            if (player == human) return state->killPlayer(debug::DebugKillTarget::Host);
            if (player == ai) return state->killPlayer(debug::DebugKillTarget::Ai);
            return false;
        },
        [state] { return state->debugParticipants(); });
#endif
    return result;
}

} // namespace basilisk::game
