#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "ActionCommands.hpp"
#include "ActionPresentation.hpp"
#include "ActionSelection.hpp"
#include "ClientLifecycle.hpp"
#include "ClientSessionController.hpp"
#include "LocalGameSessionAdapter.hpp"
#include "MapActionMenu.hpp"
#include "SnapshotPresentation.hpp"
#include "basilisk/systems/RoundController.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"

using namespace basilisk;
using namespace basilisk::game;

namespace {

client::ClientViewContext playingView() {
    return client::ClientViewContext{
        PlayerId{1},
        PlayerId{1},
        client::ClientViewMode::Playing,
        std::nullopt,
    };
}

client::ClientViewContext spectatorView() {
    return client::ClientViewContext{
        PlayerId{1},
        PlayerId{2},
        client::ClientViewMode::Spectating,
        PlayerId{2},
    };
}

AvailableAction actionWithShape(ActionType type) {
    AvailableAction action;
    action.type = type;
    return action;
}

class RecordingSink final : public ActionCommandSink {
public:
    bool submitSucceeds{true};
    bool lockSucceeds{true};
    int submits{0};
    int locks{0};
    std::optional<PlayerAction> submitted;
    std::optional<PlayerId> lockedPlayer;

    bool submitAction(const PlayerAction& action) override {
        ++submits;
        submitted = action;
        return submitSucceeds;
    }

    bool lockAction(PlayerId player) override {
        ++locks;
        lockedPlayer = player;
        return lockSucceeds;
    }
};

class RecordingSessionSink final : public ClientSessionCommandSink {
public:
    int quits{0};
    std::optional<PlayerId> quitPlayer;

    bool quitGame(PlayerId player) override {
        ++quits;
        quitPlayer = player;
        return true;
    }
};

class CoreRoundSink final : public ActionCommandSink {
public:
    explicit CoreRoundSink(MatchState& state) : state_(state) {}

    void attach(ClientSessionController& session) noexcept {
        session_ = &session;
    }

    bool submitAction(const PlayerAction& action) override {
        submitted = action;
        pending_ = action;
        return true;
    }

    bool lockAction(PlayerId player) override {
        if (session_ == nullptr || !pending_.has_value() ||
            pending_->player != player) {
            return false;
        }
        locked = *pending_;
        pending_.reset();
        RoundController controller;
        const auto events = controller.resolve(state_, {*locked});
        return session_->ingestSnapshot(
            SnapshotSystem::buildForPlayer(state_, player, events));
    }

    std::optional<PlayerAction> submitted;
    std::optional<PlayerAction> locked;

private:
    MatchState& state_;
    ClientSessionController* session_{nullptr};
    std::optional<PlayerAction> pending_;
};

void ingestSnapshotForView(
    ClientSessionController& session,
    const client::ClientViewContext& view,
    RoundNumber round = RoundNumber{1}) {
    PlayerRoundSnapshot snapshot;
    snapshot.player = view.viewedPlayer;
    snapshot.round = round;
    snapshot.alive = true;
    assert(session.ingestSnapshot(std::move(snapshot)));
}

void assertExactCopy(const AvailableAction& available) {
    const PlayerAction action = makePlayerAction(available, PlayerId{42});
    assert(action.player == PlayerId{42});
    assert(action.type == available.type);
    assert(action.targetCave == available.targetCave);
    assert(action.targetTunnel == available.targetTunnel);
    assert(action.targetItem == available.targetItem);
    assert(action.contextualAction == available.contextualAction);
}

void everyActionShapeCopiesExactly() {
    AvailableAction moveKnown = actionWithShape(ActionType::Move);
    moveKnown.targetCave = CaveId{12};
    assertExactCopy(moveKnown);

    AvailableAction moveUnknown = actionWithShape(ActionType::Move);
    moveUnknown.targetTunnel = TunnelId{6};
    assertExactCopy(moveUnknown);

    assertExactCopy(actionWithShape(ActionType::Search));

    AvailableAction shoot = actionWithShape(ActionType::Shoot);
    shoot.targetCave = CaveId{16};
    assertExactCopy(shoot);

    AvailableAction useItem = actionWithShape(ActionType::UseItem);
    useItem.targetItem = ItemType::OldHuntersMap;
    assertExactCopy(useItem);

    AvailableAction escape = actionWithShape(ActionType::Contextual);
    escape.contextualAction = ContextualActionType::Escape;
    assertExactCopy(escape);

    AvailableAction allFields = actionWithShape(ActionType::Contextual);
    allFields.targetCave = CaveId{8};
    allFields.targetTunnel = TunnelId{9};
    allFields.targetItem = ItemType::BloodBait;
    allFields.contextualAction = ContextualActionType::Escape;
    assertExactCopy(allFields);
}

void rowsComeDirectlyFromEveryAvailableAction() {
    std::vector<AvailableAction> actions;
    for (int index = 0; index < 11; ++index) {
        AvailableAction action = actionWithShape(ActionType::Move);
        action.targetCave = static_cast<CaveId>(100 + index);
        actions.push_back(action);
    }
    const auto rows = presentAvailableActions(actions);
    assert(rows.size() == actions.size());
    assert(rows.front().title == "Move");
    assert(rows.front().detail == "Cave 100 · Known tunnel");
    assert(rows.back().title == "Move");
    assert(rows.back().detail == "Cave 110 · Known tunnel");

    ActionSelectionState selection;
    selection.synchronize(RoundNumber{1}, actions.size(), playingView());
    selection.scrollRows(100, actions.size(), 3);
    assert(selection.scrollOffset() == 8);
    assert(selection.select(10, actions, playingView()));
    selection.ensureVisible(10, 3);
    assert(selection.scrollOffset() == 8);
}

void actionPresentationUsesUnifiedCopyAndTargetDetails() {
    AvailableAction knownMove = actionWithShape(ActionType::Move);
    knownMove.targetCave = CaveId{12};
    const PresentedAction knownMoveRow = presentAvailableAction(knownMove);
    assert(knownMoveRow.title == "Move");
    assert(knownMoveRow.detail == "Cave 12 · Known tunnel");

    AvailableAction unknownMove = actionWithShape(ActionType::Move);
    unknownMove.targetTunnel = TunnelId{6};
    const PresentedAction unknownMoveRow = presentAvailableAction(unknownMove);
    assert(unknownMoveRow.title == "Move");
    assert(unknownMoveRow.detail == "Tunnel 6 · Destination unknown");

    AvailableAction targetedShoot = actionWithShape(ActionType::Shoot);
    targetedShoot.targetCave = CaveId{16};
    const PresentedAction targetedShootRow =
        presentAvailableAction(targetedShoot);
    assert(targetedShootRow.title == "Fire Arrow");
    assert(targetedShootRow.detail == "Cave 16 · Uses 1 arrow");

    const PresentedAction untargetedShootRow =
        presentAvailableAction(actionWithShape(ActionType::Shoot));
    assert(untargetedShootRow.title == "Fire Arrow");
    assert(untargetedShootRow.detail == "Uses 1 arrow");

    AvailableAction survey = actionWithShape(ActionType::UseItem);
    survey.targetItem = ItemType::SurveyFragment;
    survey.targetTunnel = TunnelId{6};
    const PresentedAction surveyRow = presentAvailableAction(survey);
    assert(surveyRow.title == "Use Survey Fragment");
    assert(surveyRow.detail == "Tunnel 6 · Destination unknown");

    const std::array actions{knownMove, targetedShoot};
    assert(mapActionMenuChoiceTitle(
               {MapActionMenuChoiceKind::GameplayAction, 0}, actions) ==
           "Move");
    assert(mapActionMenuChoiceTitle(
               {MapActionMenuChoiceKind::GameplayAction, 1}, actions) ==
           "Fire Arrow");
    assert(mapActionMenuChoiceTitle(
               {MapActionMenuChoiceKind::MarkDestination, 0}, actions) ==
           "Set Destination");
    assert(mapActionMenuChoiceTitle(
               {MapActionMenuChoiceKind::ClearDestination, 0}, actions) ==
           "Clear Destination");
}

void changingSelectionReplacesDraft() {
    const std::array actions{
        actionWithShape(ActionType::Search),
        actionWithShape(ActionType::Shoot),
    };
    ActionSelectionState selection;
    selection.synchronize(RoundNumber{5}, actions.size(), playingView());
    assert(selection.select(0, actions, playingView()));
    assert(selection.draft()->type == ActionType::Search);
    assert(selection.select(1, actions, playingView()));
    assert(selection.selectedIndex() == 1);
    assert(selection.draft()->type == ActionType::Shoot);
}

void spectatorCannotSelectOrSubmit() {
    const std::array actions{actionWithShape(ActionType::Search)};
    ActionSelectionState selection;
    selection.synchronize(RoundNumber{2}, actions.size(), spectatorView());
    assert(!selection.select(0, actions, spectatorView()));
    auto commands = std::make_unique<RecordingSink>();
    RecordingSink* recorded = commands.get();
    ClientSessionController session(
        {}, {}, spectatorView(), std::move(commands), nullptr);
    ingestSnapshotForView(session, spectatorView());
    assert(!selection.submitAndLock(session));
    assert(recorded->submits == 0);
    assert(recorded->locks == 0);

    selection.synchronize(RoundNumber{2}, actions.size(), playingView());
    assert(selection.select(0, actions, playingView()));
    selection.synchronize(RoundNumber{2}, actions.size(), spectatorView());
    assert(!selection.selectedIndex().has_value());
    assert(!selection.draft().has_value());
}

void successfulLockPreventsReplacement() {
    const std::array actions{
        actionWithShape(ActionType::Search),
        actionWithShape(ActionType::Shoot),
    };
    ActionSelectionState selection;
    selection.synchronize(RoundNumber{3}, actions.size(), playingView());
    assert(selection.select(0, actions, playingView()));
    auto commands = std::make_unique<RecordingSink>();
    RecordingSink* recorded = commands.get();
    ClientSessionController session(
        {}, {}, playingView(), std::move(commands), nullptr);
    ingestSnapshotForView(session, playingView());
    assert(selection.submitAndLock(session));
    assert(selection.locked());
    assert(selection.waitingForOtherHunter());
    assert(recorded->submits == 1);
    assert(recorded->locks == 1);
    assert(recorded->submitted->type == ActionType::Search);
    assert(recorded->lockedPlayer == PlayerId{1});
    assert(!selection.select(1, actions, playingView()));
    assert(!selection.submitAndLock(session));
    assert(recorded->submits == 1);
}

void lockFailureDoesNotShowLocked() {
    const std::array actions{actionWithShape(ActionType::Search)};
    ActionSelectionState selection;
    selection.synchronize(RoundNumber{7}, actions.size(), playingView());
    assert(selection.select(0, actions, playingView()));
    auto commands = std::make_unique<RecordingSink>();
    commands->lockSucceeds = false;
    ClientSessionController session(
        {}, {}, playingView(), std::move(commands), nullptr);
    ingestSnapshotForView(session, playingView());
    assert(!selection.submitAndLock(session));
    assert(!selection.locked());
}

void newRoundClearsDraftAndLockedState() {
    const std::array actions{actionWithShape(ActionType::Search)};
    ActionSelectionState selection;
    selection.synchronize(RoundNumber{8}, actions.size(), playingView());
    assert(selection.select(0, actions, playingView()));
    auto commands = std::make_unique<RecordingSink>();
    ClientSessionController session(
        {}, {}, playingView(), std::move(commands), nullptr);
    ingestSnapshotForView(session, playingView());
    assert(selection.submitAndLock(session));
    selection.synchronize(RoundNumber{9}, actions.size(), playingView());
    assert(!selection.selectedIndex().has_value());
    assert(!selection.draft().has_value());
    assert(!selection.locked());
    assert(!selection.waitingForOtherHunter());
    assert(selection.scrollOffset() == 0);
}

void objectivePresentationFollowsSnapshotOnly() {
    PlayerRoundSnapshot snapshot;
    assert(!secondaryObjectivePresentation(snapshot).has_value());

    snapshot.recoverableRivalSigilAvailable = true;
    auto objective = secondaryObjectivePresentation(snapshot);
    assert(objective.has_value());
    assert(objective->kind == SecondaryObjectiveKind::Recoverable);
    assert(objective->title == "RECOVER HUNTER'S SIGIL");
    assert(objective->status == "AVAILABLE");
    assert(!objective->detail.has_value());

    snapshot.hasHunterSigil = true;
    objective = secondaryObjectivePresentation(snapshot);
    assert(objective->kind == SecondaryObjectiveKind::Secured);
    assert(objective->title == "HUNTER'S SIGIL");
    assert(objective->status == "SECURED");
    assert(objective->detail ==
        "Extraction is active \xC2\xB7 location unavailable");

    snapshot.extractionCave = CaveId{34};
    objective = secondaryObjectivePresentation(snapshot);
    assert(objective->detail == "Extraction at Cave 34");
}

void emptyAndPopulatedRoundReportsUsePlayerObservations() {
    PlayerRoundSnapshot snapshot;
    assert((roundReportText(snapshot) ==
        std::vector<std::string>{"No new observations."}));

    PlayerObservation observation;
    observation.type = ObservationType::ArrowFound;
    observation.viewer = PlayerId{1};
    observation.amount = 1;
    snapshot.observations.push_back(observation);
    assert((roundReportText(snapshot) ==
        std::vector<std::string>{"You found 1 arrow(s)."}));
}

void roundReportLayoutIncludesEveryWrappedMessage() {
    PlayerRoundSnapshot snapshot;
    for (int index = 0; index < 4; ++index) {
        PlayerObservation observation;
        observation.type = ObservationType::ArrowFound;
        observation.viewer = PlayerId{1};
        observation.amount = index + 1;
        snapshot.observations.push_back(observation);
    }
    const std::vector<std::string> report = roundReportText(snapshot);
    assert(report.size() == 4);

    const std::array<std::size_t, 3> threeRows{1, 1, 1};
    const std::array<std::size_t, 4> fourRows{1, 1, 1, 1};
    const RoundReportLayout three = roundReportLayout(threeRows, 1.0F);
    const RoundReportLayout four = roundReportLayout(fourRows, 1.0F);
    assert(four.rowHeights.size() == report.size());
    assert(four.panelHeight > three.panelHeight);

    const std::array<std::size_t, 4> wrappedRows{1, 3, 1, 1};
    const RoundReportLayout wrapped = roundReportLayout(wrappedRows, 1.0F);
    assert(wrapped.rowHeights.size() == report.size());
    assert(wrapped.rowHeights[1] == 52.0F);
    assert(wrapped.panelHeight > four.panelHeight);
}

std::array<client::PublicPlayerProfile, 2> demoProfiles() {
    return {
        client::PublicPlayerProfile{
            PlayerId{1},
            "Mara Voss",
            client::CallingCardId{"ember"},
            client::EmblemId{"wayfinder"}},
        client::PublicPlayerProfile{
            PlayerId{2},
            "Elias Thorn",
            client::CallingCardId{"ward"},
            client::EmblemId{"sentinel"}},
    };
}

void playingLifecycleHasAuthorityAndNoModal() {
    PlayerRoundSnapshot snapshot;
    snapshot.player = PlayerId{1};
    snapshot.alive = true;
    const auto view = playingView();
    const auto profiles = demoProfiles();
    assert(!lifecycleModalPresentation(snapshot, view, profiles).has_value());
    assert(view.canSubmitActions());
}

void rivalReconnectStatusFollowsPlayerSafeObservations() {
    PlayerRoundSnapshot snapshot;
    snapshot.observations = {
        PlayerObservation{ObservationType::RivalDisconnected, PlayerId{1}},
    };
    assert(rivalReconnectWaiting(snapshot));

    snapshot.observations = {
        PlayerObservation{ObservationType::RivalReconnected, PlayerId{1}},
    };
    assert(!rivalReconnectWaiting(snapshot));

    snapshot.observations = {
        PlayerObservation{ObservationType::RivalDisconnectTimedOut, PlayerId{1}},
    };
    assert(!rivalReconnectWaiting(snapshot));
    const auto report = roundReportText(snapshot);
    assert(report.size() == 1);
    assert(report.front() ==
        "The rival hunter did not return before the reconnect grace expired "
        "and is out of the hunt.");
}

void firstDeathCanTransitionToViewOnlySpectating() {
    PlayerRoundSnapshot snapshot;
    snapshot.player = PlayerId{1};
    snapshot.alive = false;
    client::ClientViewContext view{
        PlayerId{1},
        PlayerId{1},
        client::ClientViewMode::Defeated,
        PlayerId{2},
    };
    const auto profiles = demoProfiles();
    const auto modal = lifecycleModalPresentation(snapshot, view, profiles);
    assert(modal.has_value());
    assert(modal->kind == LifecycleModalKind::FirstDeath);
    assert(modal->title == "YOU DIED");
    assert(modal->offersWatch);
    assert(!view.canSubmitActions());

    assert(beginSpectating(view));
    assert(view.localPlayer == PlayerId{1});
    assert(view.viewedPlayer == PlayerId{2});
    assert(view.mode == client::ClientViewMode::Spectating);
    assert(!view.canSubmitActions());
}

void finalDeathOffersQuitOnly() {
    PlayerRoundSnapshot snapshot;
    snapshot.player = PlayerId{1};
    snapshot.alive = false;
    snapshot.matchStatus = MatchStatus::Completed;
    snapshot.matchOutcome = MatchOutcome::Draw;
    client::ClientViewContext view{
        PlayerId{1},
        PlayerId{1},
        client::ClientViewMode::Defeated,
        std::nullopt,
    };
    const auto profiles = demoProfiles();
    const auto modal = lifecycleModalPresentation(snapshot, view, profiles);
    assert(modal.has_value());
    assert(modal->kind == LifecycleModalKind::FinalDeath);
    assert(modal->title == "YOU DIED");
    assert(!modal->offersWatch);
    assert(!beginSpectating(view));
}

void spectatorTerminalResultUsesPublicWinnerProfile() {
    PlayerRoundSnapshot snapshot;
    snapshot.player = PlayerId{2};
    snapshot.alive = true;
    snapshot.matchStatus = MatchStatus::Completed;
    snapshot.matchOutcome = MatchOutcome::BasiliskKilled;
    snapshot.winner = PlayerId{2};
    const auto view = spectatorView();
    const auto profiles = demoProfiles();
    const auto modal = lifecycleModalPresentation(snapshot, view, profiles);
    assert(modal.has_value());
    assert(modal->kind == LifecycleModalKind::HuntEnded);
    assert(modal->title == "HUNT ENDED");
    assert(modal->detail ==
        "Elias Thorn killed the Basilisk and wins the hunt.");
    assert(!modal->offersWatch);
    assert(!view.canSubmitActions());
}

void controllerOwnsMetadataAndSelectsNewestLocalSnapshot() {
    PublicMatchMetadata metadata;
    metadata.totalCaves = 40;
    metadata.players = {
        PublicPlayerSlot{PlayerId{1}, PlayerSlot::P1},
        PublicPlayerSlot{PlayerId{2}, PlayerSlot::P2},
    };
    const auto profileArray = demoProfiles();
    std::vector<client::PublicPlayerProfile> profiles(
        profileArray.begin(), profileArray.end());
    ClientSessionController session(
        std::move(metadata),
        std::move(profiles),
        playingView(),
        nullptr,
        nullptr);

    PlayerRoundSnapshot newest;
    newest.player = PlayerId{1};
    newest.round = RoundNumber{4};
    newest.health = 40;
    assert(session.ingestSnapshot(newest));
    assert(session.displayedSnapshot() != nullptr);
    assert(session.displayedSnapshot()->health == 40);

    PlayerRoundSnapshot older = newest;
    older.round = RoundNumber{3};
    older.health = 30;
    assert(!session.ingestSnapshot(older));
    assert(session.displayedSnapshot()->health == 40);

    PlayerRoundSnapshot sameRoundUpdate = newest;
    sameRoundUpdate.health = 44;
    assert(session.ingestSnapshot(sameRoundUpdate));
    assert(session.displayedSnapshot()->health == 44);
    assert(session.matchMetadata().totalCaves == 40);
    assert(session.profiles().size() == 2);
}

void controllerHandlesMissingSpectatorSnapshotSafely() {
    ClientSessionController session({}, {}, spectatorView(), nullptr, nullptr);
    PlayerRoundSnapshot local;
    local.player = PlayerId{1};
    local.round = RoundNumber{5};
    assert(session.ingestSnapshot(local));
    assert(session.displayedSnapshot() == nullptr);
    assert(!session.canSubmitActions());

    PlayerRoundSnapshot survivor;
    survivor.player = PlayerId{2};
    survivor.round = RoundNumber{6};
    survivor.health = 63;
    assert(session.ingestSnapshot(survivor));
    assert(session.displayedSnapshot() == session.snapshotFor(PlayerId{2}));
    assert(session.displayedSnapshot()->health == 63);
    assert(session.viewContext().localPlayer == PlayerId{1});
    assert(session.viewContext().viewedPlayer == PlayerId{2});
}

void controllerOwnsWatchTransition() {
    const client::ClientViewContext defeated{
        PlayerId{1},
        PlayerId{1},
        client::ClientViewMode::Defeated,
        PlayerId{2},
    };
    ClientSessionController session({}, {}, defeated, nullptr, nullptr);
    PlayerRoundSnapshot local;
    local.player = PlayerId{1};
    assert(session.ingestSnapshot(local));
    PlayerRoundSnapshot survivor;
    survivor.player = PlayerId{2};
    assert(session.ingestSnapshot(survivor));

    assert(session.watchRemainingHunter());
    assert(session.viewContext().mode == client::ClientViewMode::Spectating);
    assert(session.viewContext().localPlayer == PlayerId{1});
    assert(session.viewContext().viewedPlayer == PlayerId{2});
    assert(session.displayedSnapshot()->player == PlayerId{2});
    assert(!session.canSubmitActions());
}

void controllerForwardsCommandsWithAuthorityGating() {
    auto actions = std::make_unique<RecordingSink>();
    RecordingSink* recordedActions = actions.get();
    auto sessionCommands = std::make_unique<RecordingSessionSink>();
    RecordingSessionSink* recordedSession = sessionCommands.get();
    ClientSessionController session(
        {},
        {},
        playingView(),
        std::move(actions),
        std::move(sessionCommands));
    ingestSnapshotForView(session, playingView());

    AvailableAction search;
    search.type = ActionType::Search;
    assert(session.submitAndLock(search));
    assert(recordedActions->submits == 1);
    assert(recordedActions->locks == 1);
    assert(recordedActions->submitted->player == PlayerId{1});
    assert(recordedActions->submitted->type == ActionType::Search);
    assert(session.quit());
    assert(recordedSession->quits == 1);
    assert(recordedSession->quitPlayer == PlayerId{1});

    session.setViewContext(spectatorView());
    PlayerRoundSnapshot survivor;
    survivor.player = PlayerId{2};
    assert(session.ingestSnapshot(survivor));
    assert(!session.submitAndLock(search));
    assert(recordedActions->submits == 1);
    assert(recordedActions->locks == 1);
}

void localAdapterPublishesOnlyPlayerSafeSessionState() {
    auto session = LocalGameSessionAdapter::create(
        MapSeed{20260812}, MatchSeed{424242});
    assert(session != nullptr);
    assert(session->matchMetadata().totalCaves > 0);
    assert(session->matchMetadata().players.size() == 1);
    assert(session->matchMetadata().players.front().player == PlayerId{1});
    assert(session->matchMetadata().players.front().slot == PlayerSlot::P1);
    assert(session->profiles().size() == 1);
    assert(session->viewContext().mode == client::ClientViewMode::Playing);
    assert(session->canSubmitActions());

    const PlayerRoundSnapshot* initial = session->displayedSnapshot();
    assert(initial != nullptr);
    assert(initial->player == PlayerId{1});
    assert(!initial->availableActions.empty());
    assert(initial->currentCave == initial->map.currentCave);
    const PlayerFixedMapGeometry* initialGeometry =
        session->displayedMapGeometry();
    assert(initialGeometry != nullptr);
    assert(initialGeometry->fullBounds.populated);
}

void fixedUnknownEndpointBecomesDiscoveredCaveAtSameCoordinate() {
    ClientSessionController session({}, {}, playingView(), nullptr, nullptr);
    constexpr LogicalPoint hiddenEndpoint{4.0, 2.0};
    constexpr LogicalBounds fullBounds{-8.0, -6.0, 10.0, 7.0, true};

    PlayerRoundSnapshot before;
    before.player = PlayerId{1};
    before.round = RoundNumber{1};
    before.currentCave = CaveId{1};
    before.map.currentCave = CaveId{1};
    before.map.caves = {
        DiscoveredCaveView{CaveId{1}, {TunnelView{TunnelId{1}, std::nullopt}}},
    };
    PlayerFixedMapGeometry beforeGeometry;
    beforeGeometry.fullBounds = fullBounds;
    beforeGeometry.discoveredCaves.emplace(CaveId{1}, LogicalPoint{0.0, 0.0});
    beforeGeometry.unknownExitEndpoints.emplace(
        MapExitKey{CaveId{1}, TunnelId{1}}, hiddenEndpoint);
    assert(session.ingestSnapshot(before, beforeGeometry));
    assert(session.displayedMapGeometry()->unknownExitEndpoints.at(
               MapExitKey{CaveId{1}, TunnelId{1}}) == hiddenEndpoint);

    PlayerRoundSnapshot after = before;
    after.round = RoundNumber{2};
    after.currentCave = CaveId{2};
    after.map.currentCave = CaveId{2};
    after.map.caves = {
        DiscoveredCaveView{CaveId{1}, {TunnelView{TunnelId{1}, CaveId{2}}}},
        DiscoveredCaveView{CaveId{2}, {TunnelView{TunnelId{1}, CaveId{1}}}},
    };
    PlayerFixedMapGeometry afterGeometry;
    afterGeometry.fullBounds = fullBounds;
    afterGeometry.discoveredCaves.emplace(CaveId{1}, LogicalPoint{0.0, 0.0});
    afterGeometry.discoveredCaves.emplace(CaveId{2}, hiddenEndpoint);
    assert(session.ingestSnapshot(after, afterGeometry));
    assert(session.displayedMapGeometry()->fullBounds == fullBounds);
    assert(session.displayedMapGeometry()->discoveredCaves.at(CaveId{2}) ==
           hiddenEndpoint);
}

void caveMenuUsesOnlyLiteralTargetMatches() {
    AvailableAction move12 = actionWithShape(ActionType::Move);
    move12.targetCave = CaveId{12};
    AvailableAction shoot12 = actionWithShape(ActionType::Shoot);
    shoot12.targetCave = CaveId{12};
    AvailableAction move16 = actionWithShape(ActionType::Move);
    move16.targetCave = CaveId{16};
    AvailableAction unknown = actionWithShape(ActionType::Move);
    unknown.targetTunnel = TunnelId{6};
    const std::array actions{
        move12,
        actionWithShape(ActionType::Search),
        shoot12,
        move16,
        unknown,
    };

    const auto matches = matchingSpatialActionIndices(
        actions, caveActionTarget(CaveId{12}), CaveId{7});
    assert((matches == std::vector<std::size_t>{0, 2}));
    assert(spatialActionTitle(actions[0]) == "Move");
    assert(spatialActionTitle(actions[2]) == "Fire Arrow");
    assert(matchingSpatialActionIndices(
               actions, caveActionTarget(CaveId{34}), CaveId{7}).empty());
}

void currentCaveMenuUsesOnlyCurrentLocationActions() {
    AvailableAction healing = actionWithShape(ActionType::UseItem);
    healing.targetItem = ItemType::HealingDraught;
    AvailableAction survey = actionWithShape(ActionType::UseItem);
    survey.targetItem = ItemType::SurveyFragment;
    survey.targetTunnel = TunnelId{6};
    AvailableAction move = actionWithShape(ActionType::Move);
    move.targetCave = CaveId{12};
    AvailableAction shoot = actionWithShape(ActionType::Shoot);
    shoot.targetCave = CaveId{7};
    const std::array actions{
        actionWithShape(ActionType::Search),
        healing,
        survey,
        move,
        shoot,
        actionWithShape(ActionType::Contextual),
    };

    const auto matches = matchingSpatialActionIndices(
        actions, caveActionTarget(CaveId{7}), CaveId{7});
    assert((matches == std::vector<std::size_t>{0, 1}));

    MapActionMenuState menu;
    assert(menu.open(
        caveActionTarget(CaveId{7}),
        100.0,
        200.0,
        actions,
        CaveId{7},
        playingView()));
    assert(menu.choices().size() == 2);
    assert(menu.choices()[0].actionIndex == 0);
    assert(menu.choices()[1].actionIndex == 1);

    const auto unknownMatches = matchingSpatialActionIndices(
        actions, unknownExitActionTarget(CaveId{7}, TunnelId{6}), CaveId{7});
    assert((unknownMatches == std::vector<std::size_t>{2}));
}

void inventoryItemSelectsOnlyMatchingLegalUseAction() {
    AvailableAction useMap = actionWithShape(ActionType::UseItem);
    useMap.targetItem = ItemType::OldMinersMap;
    AvailableAction useHealing = actionWithShape(ActionType::UseItem);
    useHealing.targetItem = ItemType::HealingDraught;
    const std::array actions{
        actionWithShape(ActionType::Search),
        useMap,
        useHealing,
    };

    ActionSelectionState selection;
    selection.synchronize(RoundNumber{14}, actions.size(), playingView());
    assert(selectInventoryItemAction(
        ItemType::HealingDraught, actions, playingView(), selection));
    assert(selection.selectedIndex() == 2);
    assert(selection.draft()->type == actions[2].type);
    assert(selection.draft()->targetCave == actions[2].targetCave);
    assert(selection.draft()->targetTunnel == actions[2].targetTunnel);
    assert(selection.draft()->targetItem == actions[2].targetItem);
    assert(selection.draft()->contextualAction == actions[2].contextualAction);

    ActionSelectionState unusableSelection;
    unusableSelection.synchronize(RoundNumber{14}, actions.size(), playingView());
    assert(!selectInventoryItemAction(
        ItemType::BloodBait, actions, playingView(), unusableSelection));
    assert(!unusableSelection.selectedIndex().has_value());
    assert(!unusableSelection.draft().has_value());
}

void huntersMapStaysHuntersMapThroughClientActionPipeline() {
    MatchState state;
    state.mapSeed = MapSeed{1};
    state.matchSeed = MatchSeed{424242};
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FogOfWar;
    for (CaveId cave = 1; cave <= 4; ++cave) state.world.addCave(cave);
    state.world.connect(CaveId{1}, CaveId{2});
    state.world.connect(CaveId{1}, CaveId{3});
    state.world.connect(CaveId{3}, CaveId{4});
    state.players = {PlayerState{PlayerId{1}, CaveId{1}, 100, 3, true}};
    state.basilisk.cave = CaveId{4};
    state.pits = {PitState{CaveId{2}, true}};

    PlayerState& player = state.players.front();
    player.discovery.knownCaves = {CaveId{1}};
    const auto& exits = state.world.cave(CaveId{1}).connections;
    const auto pitCave = std::find(exits.begin(), exits.end(), CaveId{2});
    assert(pitCave != exits.end());
    player.knownPitTunnels[CaveId{1}] = static_cast<TunnelId>(
        std::distance(exits.begin(), pitCave) + 1);
    player.inventory.items = {ItemInstance{ItemType::OldHuntersMap}};
    player.pitMapRevealRounds = 0;

    auto commands = std::make_unique<CoreRoundSink>(state);
    CoreRoundSink* core = commands.get();
    ClientSessionController session(
        {}, {}, playingView(), std::move(commands), nullptr);
    core->attach(session);
    assert(session.ingestSnapshot(
        SnapshotSystem::buildForPlayer(state, player.id, {})));

    const PlayerRoundSnapshot* before = session.displayedSnapshot();
    assert(before != nullptr);
    ActionSelectionState selection;
    selection.synchronize(
        before->round, before->availableActions.size(), playingView());
    assert(selectInventoryItemAction(
        ItemType::OldHuntersMap,
        before->availableActions,
        playingView(),
        selection));
    assert(selection.draft()->targetItem == ItemType::OldHuntersMap);
    assert(selection.submitAndLock(session));

    assert(core->submitted->targetItem == ItemType::OldHuntersMap);
    assert(core->locked->targetItem == ItemType::OldHuntersMap);
    assert(player.pitMapRevealRounds == 0);
    const PlayerRoundSnapshot* after = session.displayedSnapshot();
    assert(after != nullptr);
    assert(after->temporarilyRevealedPitCaves.empty());
    assert(std::none_of(
        after->map.caves.begin(), after->map.caves.end(),
        [](const DiscoveredCaveView& cave) {
            return cave.cave == CaveId{2};
        }));
}

void mapMenuAndSidebarShareOneDraft() {
    AvailableAction move = actionWithShape(ActionType::Move);
    move.targetCave = CaveId{12};
    AvailableAction shoot = actionWithShape(ActionType::Shoot);
    shoot.targetCave = CaveId{12};
    const std::array actions{move, shoot};
    ActionSelectionState selection;
    selection.synchronize(RoundNumber{10}, actions.size(), playingView());

    MapActionMenuState menu;
    assert(menu.open(
        caveActionTarget(CaveId{12}),
        100.0,
        200.0,
        actions,
        CaveId{7},
        playingView()));
    assert(menu.chooseGameplayAction(
        {MapActionMenuChoiceKind::GameplayAction, 0},
        actions,
        playingView(),
        selection));
    assert(selection.selectedIndex() == 0);
    assert(selection.draft()->type == actions[0].type);
    assert(selection.draft()->targetCave == actions[0].targetCave);

    assert(menu.open(
        caveActionTarget(CaveId{12}),
        100.0,
        200.0,
        actions,
        CaveId{7},
        playingView()));
    assert(menu.chooseGameplayAction(
        {MapActionMenuChoiceKind::GameplayAction, 1},
        actions,
        playingView(),
        selection));
    assert(selection.selectedIndex() == 1);
    assert(selection.draft()->type == actions[1].type);
    assert(selection.draft()->targetCave == actions[1].targetCave);

    assert(selection.select(0, actions, playingView()));
    assert(selection.selectedIndex() == 0);
    assert(menu.open(
        caveActionTarget(CaveId{12}),
        100.0,
        200.0,
        actions,
        CaveId{7},
        playingView()));
    assert(menu.chooseGameplayAction(
        {MapActionMenuChoiceKind::GameplayAction, 1},
        actions,
        playingView(),
        selection));
    assert(selection.selectedIndex() == 1);
}

void unknownExitMatchesExactCurrentCaveProvenance() {
    AvailableAction unknown = actionWithShape(ActionType::Move);
    unknown.targetTunnel = TunnelId{6};
    AvailableAction shootUnknown = actionWithShape(ActionType::Shoot);
    shootUnknown.targetTunnel = TunnelId{6};
    AvailableAction other = actionWithShape(ActionType::Move);
    other.targetTunnel = TunnelId{4};
    const std::array actions{other, unknown, shootUnknown};

    const SpatialActionTarget target =
        unknownExitActionTarget(CaveId{19}, TunnelId{6});
    assert(target.kind == SpatialActionTargetKind::UnknownExit);
    assert(target.cave == CaveId{19});
    assert(target.tunnel == TunnelId{6});
    const auto matches = matchingSpatialActionIndices(
        actions, target, CaveId{19});
    assert((matches == std::vector<std::size_t>{1, 2}));
    assert(matchingSpatialActionIndices(
               actions, target, CaveId{18}).empty());

    const SpatialActionTarget collidingHistoricalExit =
        unknownExitActionTarget(CaveId{18}, TunnelId{6});
    assert(matchingSpatialActionIndices(
               actions, collidingHistoricalExit, CaveId{19}).empty());
    assert(spatialActionTitle(actions[1]) == "Move");
    assert(!actions[1].targetCave.has_value());
    assert(!actions[2].targetCave.has_value());
}

void spectatorCannotOpenOrChooseMapAction() {
    AvailableAction move = actionWithShape(ActionType::Move);
    move.targetCave = CaveId{12};
    const std::array actions{move};
    ActionSelectionState selection;
    selection.synchronize(RoundNumber{11}, actions.size(), spectatorView());
    MapActionMenuState menu;
    assert(!menu.open(
        caveActionTarget(CaveId{12}),
        100.0,
        200.0,
        actions,
        CaveId{7},
        spectatorView()));
    assert(!menu.chooseGameplayAction(
        {MapActionMenuChoiceKind::GameplayAction, 0},
        actions,
        spectatorView(),
        selection));
    assert(!selection.draft().has_value());
}

void mapChoiceStillRequiresExplicitLock() {
    AvailableAction move = actionWithShape(ActionType::Move);
    move.targetCave = CaveId{12};
    const std::array actions{move};
    ActionSelectionState selection;
    selection.synchronize(RoundNumber{12}, actions.size(), playingView());
    MapActionMenuState menu;
    assert(menu.open(
        caveActionTarget(CaveId{12}),
        100.0,
        200.0,
        actions,
        CaveId{7},
        playingView()));
    assert(menu.chooseGameplayAction(
        {MapActionMenuChoiceKind::GameplayAction, 0},
        actions,
        playingView(),
        selection));

    RecordingSink sink;
    assert(selection.canLock(playingView()));
    assert(!selection.locked());
    assert(sink.submits == 0);
    assert(sink.locks == 0);
}

void navigationMenuChoiceDoesNotCreateActionDraft() {
    const std::array actions{actionWithShape(ActionType::Search)};
    ActionSelectionState selection;
    selection.synchronize(RoundNumber{13}, actions.size(), playingView());
    MapActionMenuState menu;
    assert(menu.open(
        caveActionTarget(CaveId{21}),
        100.0,
        200.0,
        actions,
        CaveId{7},
        playingView(),
        DestinationControl::Mark));
    assert(menu.choices().size() == 1);
    assert(menu.choices().front().kind ==
        MapActionMenuChoiceKind::MarkDestination);
    assert(!menu.chooseGameplayAction(
        menu.choices().front(), actions, playingView(), selection));
    assert(!selection.draft().has_value());
    assert(!selection.canLock(playingView()));

    menu.dismiss();
    assert(menu.open(
        caveActionTarget(CaveId{21}),
        100.0,
        200.0,
        actions,
        CaveId{7},
        spectatorView(),
        DestinationControl::Clear));
    assert(menu.choices().size() == 1);
    assert(menu.choices().front().kind ==
        MapActionMenuChoiceKind::ClearDestination);
    assert(!selection.draft().has_value());
}

} // namespace

int main() {
    everyActionShapeCopiesExactly();
    rowsComeDirectlyFromEveryAvailableAction();
    actionPresentationUsesUnifiedCopyAndTargetDetails();
    changingSelectionReplacesDraft();
    spectatorCannotSelectOrSubmit();
    successfulLockPreventsReplacement();
    lockFailureDoesNotShowLocked();
    newRoundClearsDraftAndLockedState();
    objectivePresentationFollowsSnapshotOnly();
    emptyAndPopulatedRoundReportsUsePlayerObservations();
    roundReportLayoutIncludesEveryWrappedMessage();
    playingLifecycleHasAuthorityAndNoModal();
    rivalReconnectStatusFollowsPlayerSafeObservations();
    firstDeathCanTransitionToViewOnlySpectating();
    finalDeathOffersQuitOnly();
    spectatorTerminalResultUsesPublicWinnerProfile();
    controllerOwnsMetadataAndSelectsNewestLocalSnapshot();
    controllerHandlesMissingSpectatorSnapshotSafely();
    controllerOwnsWatchTransition();
    controllerForwardsCommandsWithAuthorityGating();
    localAdapterPublishesOnlyPlayerSafeSessionState();
    fixedUnknownEndpointBecomesDiscoveredCaveAtSameCoordinate();
    caveMenuUsesOnlyLiteralTargetMatches();
    currentCaveMenuUsesOnlyCurrentLocationActions();
    inventoryItemSelectsOnlyMatchingLegalUseAction();
    huntersMapStaysHuntersMapThroughClientActionPipeline();
    mapMenuAndSidebarShareOneDraft();
    unknownExitMatchesExactCurrentCaveProvenance();
    spectatorCannotOpenOrChooseMapAction();
    mapChoiceStillRequiresExplicitLock();
    navigationMenuChoiceDoesNotCreateActionDraft();
    return 0;
}
