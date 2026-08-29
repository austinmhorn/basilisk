#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>

#include "basilisk/Action.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/systems/MatchCoordinator.hpp"
#include "basilisk/systems/RoundController.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

PlayerAction search(PlayerId player) {
    PlayerAction action;
    action.player = player;
    action.type = ActionType::Search;
    return action;
}

PlayerAction move(PlayerId player, CaveId cave) {
    PlayerAction action; action.player = player; action.type = ActionType::Move;
    action.targetCave = cave; return action;
}

PlayerAction moveThrough(PlayerId player, TunnelId tunnel) {
    PlayerAction action; action.player = player; action.type = ActionType::Move;
    action.targetTunnel = tunnel; return action;
}

PlayerAction use(PlayerId player, ItemType item) {
    PlayerAction action; action.player = player; action.type = ActionType::UseItem;
    action.targetItem = item; return action;
}

PlayerAction shoot(PlayerId player, CaveId cave) {
    PlayerAction action; action.player = player; action.type = ActionType::Shoot;
    action.targetCave = cave; return action;
}

PlayerAction escape(PlayerId player) {
    PlayerAction action; action.player = player; action.type = ActionType::Contextual;
    action.contextualAction = ContextualActionType::Escape; return action;
}

MatchState clashFixture() {
    MatchState state; state.matchSeed = 77; state.rules.mapDiscoveryMode = MapDiscoveryMode::FullMap;
    for (CaveId cave = 1; cave <= 6; ++cave) state.world.addCave(cave);
    state.world.connect(1, 3); state.world.connect(2, 3); state.world.connect(1, 2);
    state.world.connect(3, 4); state.world.connect(4, 5); state.world.connect(5, 6);
    state.players = {PlayerState{PlayerId{17}, CaveId{1}}, PlayerState{PlayerId{42}, CaveId{2}}};
    state.basilisk.alive = false;
    return state;
}

MatchState nonConflictingMoveFixture() {
    MatchState state; state.matchSeed = 91;
    for (CaveId cave = 1; cave <= 6; ++cave) state.world.addCave(cave);
    state.world.connect(1, 3);
    state.world.connect(2, 4);
    state.world.connect(3, 5);
    state.world.connect(4, 6);
    state.players = {PlayerState{PlayerId{17}, CaveId{1}}, PlayerState{PlayerId{42}, CaveId{2}}};
    state.basilisk.alive = false;
    return state;
}

MatchState multiHunterFixture(std::size_t count = 6) {
    MatchState state; state.matchSeed = 1701;
    for (CaveId cave = 1; cave <= 16; ++cave) state.world.addCave(cave);
    for (CaveId cave = 1; cave <= 16; ++cave)
        state.world.connect(cave, cave == 16 ? CaveId{1} : cave + 1);
    state.world.addCave(20); state.world.addCave(21);
    for (CaveId cave = 1; cave <= 6; ++cave) state.world.connect(cave, 20);
    for (CaveId cave = 7; cave <= 12; ++cave) state.world.connect(cave, 21);
    for (std::size_t index = 0; index < count; ++index) {
        PlayerState player;
        player.id = static_cast<PlayerId>(101 + index);
        player.cave = static_cast<CaveId>(1 + index);
        state.players.push_back(player);
    }
    state.basilisk.alive = false;
    return state;
}

void submitAndLockAll(MatchCoordinator& coordinator,
                      const std::vector<PlayerAction>& actions) {
    for (const auto& action : actions) {
        assert(coordinator.submitAction(action));
        assert(coordinator.lockAction(action.player));
    }
}

const PlayerState& playerById(const MatchState& state, PlayerId id) {
    const auto it = std::find_if(state.players.begin(), state.players.end(),
        [id](const PlayerState& player) { return player.id == id; });
    assert(it != state.players.end());
    return *it;
}

bool hasEvent(const std::vector<GameEvent>& events, GameEventType type) {
    return std::any_of(events.begin(), events.end(),
        [type](const GameEvent& event) { return event.type == type; });
}

std::size_t eventCount(const std::vector<GameEvent>& events, GameEventType type) {
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(),
        [type](const GameEvent& event) { return event.type == type; }));
}

void actionsCanChangeUntilLockedAndTwoLocksResolve() {
    auto state = MapGenerator::generate(101, 202);
    MatchCoordinator coordinator(state);

    auto first = search(1);
    assert(coordinator.submitAction(first));

    auto replacement = search(1);
    replacement.type = ActionType::Shoot; // invalid target is okay; RoundController safely rejects it.
    assert(coordinator.submitAction(replacement));
    assert(coordinator.hostSession(1)->pendingAction->type == ActionType::Shoot);

    assert(coordinator.lockAction(1));
    assert(state.round == 1);
    assert(!coordinator.submitAction(first));

    assert(coordinator.submitAction(search(2)));
    assert(coordinator.lockAction(2));
    assert(state.round == 2);
    assert(!coordinator.hostSession(1)->actionLocked);
    assert(!coordinator.hostSession(2)->actionLocked);
    assert(!coordinator.hostSession(1)->pendingAction.has_value());
    assert(!coordinator.hostSession(2)->pendingAction.has_value());
}

void reserveRunsOnlyForHunterMakingLockedOpponentWait() {
    auto state = MapGenerator::generate(303, 404);
    state.rules.multiplayerReserveMs = 300000;
    MatchCoordinator coordinator(state);

    assert(coordinator.submitAction(search(1)));
    assert(coordinator.lockAction(1));
    coordinator.advanceTime(12500);

    assert(coordinator.hostSession(1)->reserveRemainingMs == 300000);
    assert(coordinator.hostSession(2)->reserveRemainingMs == 287500);
    assert(playerById(state, 2).alive);

    assert(coordinator.submitAction(search(2)));
    assert(coordinator.lockAction(2));
    assert(state.round == 2);
}

void reconnectWithinGracePreservesHunterAndActionState() {
    auto state = MapGenerator::generate(505, 606);
    state.rules.disconnectGraceMs = 30000;
    MatchCoordinator coordinator(state);

    assert(coordinator.submitAction(search(2)));
    coordinator.disconnect(2);
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::PlayerDisconnected));
    assert(!coordinator.hostSession(2)->connected);

    coordinator.advanceTime(29000);
    assert(playerById(state, 2).alive);
    assert(coordinator.hostSession(2)->pendingAction.has_value());

    assert(coordinator.reconnect(2));
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::PlayerReconnected));
    assert(coordinator.hostSession(2)->connected);
    assert(coordinator.hostSession(2)->disconnectGraceRemainingMs == 30000);
}

void explicitForfeitEliminatesImmediatelyAndDoesNotUseGrace() {
    auto state = MapGenerator::generate(506, 607);
    state.rules.disconnectGraceMs = 30000;
    MatchCoordinator coordinator(state);

    coordinator.forfeit(PlayerId{2});
    assert(!playerById(state, PlayerId{2}).alive);
    assert(playerById(state, PlayerId{1}).alive);
    assert(!coordinator.hostSession(PlayerId{2})->connected);
    assert(coordinator.hostSession(PlayerId{2})->disconnectGraceRemainingMs == 0);
    assert(!hasEvent(
        coordinator.authoritativeEvents(), GameEventType::PlayerDisconnected));
    assert(!hasEvent(coordinator.authoritativeEvents(),
        GameEventType::PlayerDisconnectTimedOut));
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::PlayerKilled));
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::BodyCreated));
    assert(!coordinator.reconnect(PlayerId{2}));

    const RoundNumber priorRound = state.round;
    assert(coordinator.submitAction(search(PlayerId{1})));
    assert(coordinator.lockAction(PlayerId{1}));
    assert(state.round == priorRound + 1);
}

void disconnectTimeoutKillsHunterAndLeavesRecoverableBody() {
    auto state = MapGenerator::generate(707, 808);
    state.rules.disconnectGraceMs = 1000;
    MatchCoordinator coordinator(state);
    const CaveId deathCave = playerById(state, 2).cave;

    coordinator.disconnect(2);
    coordinator.advanceTime(1000);

    assert(!playerById(state, 2).alive);
    assert(playerById(state, 1).alive);
    assert(state.result.status == MatchStatus::Active);
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::PlayerDisconnectTimedOut));
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::PlayerKilled));
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::BodyCreated));

    const auto body = std::find_if(state.bodies.begin(), state.bodies.end(),
        [](const BodyState& candidate) { return candidate.owner == 2; });
    assert(body != state.bodies.end());
    assert(body->cave == deathCave);
    assert(body->sigilAvailable);
    assert(body->sigilCave == deathCave);
}

void reserveExpirationKillsWaitingHunterAndResolvesLockedSurvivor() {
    auto state = MapGenerator::generate(909, 1001);
    state.rules.multiplayerReserveMs = 500;
    MatchCoordinator coordinator(state);

    assert(coordinator.submitAction(search(1)));
    assert(coordinator.lockAction(1));
    coordinator.advanceTime(500);

    assert(!playerById(state, 2).alive);
    assert(playerById(state, 1).alive);
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::PlayerReserveExpired));
    assert(state.round == 2); // Player 1's already-locked action resolves immediately.

    assert(coordinator.submitAction(search(1)));
    assert(coordinator.lockAction(1));
    assert(state.round == 3); // Only the surviving hunter is required now.
}

void deadPlayerDisconnectDoesNotBlockLivingHunter() {
    auto state = MapGenerator::generate(1010, 2020);
    state.rules.disconnectGraceMs = 1;
    MatchCoordinator coordinator(state);

    coordinator.disconnect(PlayerId{2});
    coordinator.advanceTime(1);
    assert(!playerById(state, PlayerId{2}).alive);
    assert(playerById(state, PlayerId{1}).alive);
    assert(state.result.status == MatchStatus::Active);

    // A dead player's later session departure is intentionally irrelevant to
    // the living-player action barrier.
    coordinator.disconnect(PlayerId{2});
    const RoundNumber priorRound = state.round;
    assert(coordinator.submitAction(search(PlayerId{1})));
    assert(coordinator.lockAction(PlayerId{1}));
    assert(state.round == priorRound + 1);
}

void bothDisconnectingCanEndInDraw() {
    auto state = MapGenerator::generate(1111, 1212);
    state.rules.disconnectGraceMs = 100;
    MatchCoordinator coordinator(state);

    coordinator.disconnect(1);
    coordinator.disconnect(2);
    coordinator.advanceTime(100);

    assert(!playerById(state, 1).alive);
    assert(!playerById(state, 2).alive);
    assert(state.result.status == MatchStatus::Completed);
    assert(state.result.outcome == MatchOutcome::Draw);
    assert(!state.result.winner.has_value());
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::MatchDrawn));
}

void sameDestinationClashPausesAndFirstCorrectWins() {
    auto state = clashFixture(); MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 3))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(move(42, 3))); assert(coordinator.lockAction(42));
    const ActiveClash* clash = coordinator.activeClash(); assert(clash != nullptr);
    assert(state.round == 1); assert(playerById(state, 17).cave == 1); assert(playerById(state, 42).cave == 2);
    const ClashId id = clash->id; const std::string word = clash->challengeWord;
    assert(coordinator.submitClashResponse(99, id, word) == ClashSubmissionResult::Rejected);
    assert(coordinator.submitClashResponse(17, id, " wrong ") == ClashSubmissionResult::Incorrect);
    std::string upper = word; std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    assert(coordinator.submitClashResponse(42, id, "  " + upper + "  ") == ClashSubmissionResult::Resolved);
    assert(coordinator.activeClash() == nullptr); assert(state.round == 2);
    assert(playerById(state, 42).cave == 3); assert(playerById(state, 17).health == 80);
    assert(playerById(state, 17).cave != 3);
    assert(coordinator.submitClashResponse(17, id, word) == ClashSubmissionResult::Rejected);
}

void nonConflictingTunnelMovesResolveOnceAndReachSnapshots() {
    auto state = nonConflictingMoveFixture();
    MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(moveThrough(17, TunnelId{1})));
    assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(moveThrough(42, TunnelId{1})));
    assert(coordinator.lockAction(42));

    assert(coordinator.activeClash() == nullptr);
    assert(state.round == 2);
    assert(playerById(state, 17).cave == 3);
    assert(playerById(state, 42).cave == 4);
    const auto player17 = SnapshotSystem::buildForPlayer(state, 17, coordinator.authoritativeEvents());
    const auto player42 = SnapshotSystem::buildForPlayer(state, 42, coordinator.authoritativeEvents());
    assert(player17.currentCave == 3 && player17.map.currentCave == 3);
    assert(player42.currentCave == 4 && player42.map.currentCave == 4);
}

void lockedPendingMoveDoesNotAdvanceJackalFlee() {
    auto state = nonConflictingMoveFixture();
    JackalState jackal{5};
    jackal.fleeOrigin = CaveId{3};
    jackal.protectedHunter = PlayerId{17};
    jackal.fleeRoundsRemaining = 3;
    state.jackals = {jackal};
    MatchCoordinator coordinator(state);

    assert(coordinator.submitAction(moveThrough(17, TunnelId{1})));
    assert(coordinator.lockAction(17));

    // Until every required hunter locks, the Move remains host-session state:
    // neither its destination nor a Jackal movement opportunity is applied.
    assert(playerById(state, 17).cave == CaveId{1});
    assert(state.jackals[0].cave == CaveId{5});
    assert(state.jackals[0].fleeRoundsRemaining == 3);
    assert(state.round == 1);
}

void nonConflictingMoveAndSearchBothResolve() {
    auto state = nonConflictingMoveFixture();
    MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(moveThrough(17, TunnelId{1})));
    assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(search(42)));
    assert(coordinator.lockAction(42));

    assert(coordinator.activeClash() == nullptr);
    assert(state.round == 2);
    assert(playerById(state, 17).cave == 3);
    assert(playerById(state, 42).cave == 2);
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::SearchCompleted));
}

void ordinaryMovesMatchDirectRoundResolution() {
    auto coordinated = nonConflictingMoveFixture();
    auto direct = coordinated;
    const std::vector<PlayerAction> actions{
        moveThrough(17, TunnelId{1}), moveThrough(42, TunnelId{1})};
    RoundController controller;
    (void)controller.resolve(direct, actions);

    MatchCoordinator coordinator(coordinated);
    assert(coordinator.submitAction(actions[0]));
    assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(actions[1]));
    assert(coordinator.lockAction(42));

    assert(coordinated.round == direct.round);
    assert(playerById(coordinated, 17).cave == playerById(direct, 17).cave);
    assert(playerById(coordinated, 42).cave == playerById(direct, 42).cave);
    assert(playerById(coordinated, 17).discovery.knownCaves ==
           playerById(direct, 17).discovery.knownCaves);
    assert(playerById(coordinated, 42).discovery.knownCaves ==
           playerById(direct, 42).discovery.knownCaves);
}

void moveIntoUseItemConsumesExactlyOnceAndBlocksNormalCommands() {
    auto state = clashFixture();
    auto& stationary = const_cast<PlayerState&>(playerById(state, 42));
    stationary.health = 50;
    assert(stationary.inventory.add(ItemInstance{ItemType::HealingDraught}, state.rules.maxInventoryItems));
    MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 2))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(use(42, ItemType::HealingDraught))); assert(coordinator.lockAction(42));
    assert(coordinator.activeClash() != nullptr);
    assert(playerById(state, 42).health == 100);
    assert(!playerById(state, 42).inventory.contains(ItemType::HealingDraught));
    assert(!coordinator.submitAction(search(17)) && !coordinator.lockAction(17));
    const auto clash = *coordinator.activeClash();
    assert(coordinator.submitClashResponse(17, clash.id, clash.challengeWord) == ClashSubmissionResult::Resolved);
    assert(playerById(state, 42).health == 80);
    assert(!playerById(state, 42).inventory.contains(ItemType::HealingDraught));
    assert(playerById(state, 17).cave == 2);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::ItemUsed) == 1);
}

void oppositeTraversalAndTimeoutStalemate() {
    auto state = clashFixture(); MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 2))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(move(42, 1))); assert(coordinator.lockAction(42));
    assert(coordinator.activeClash() != nullptr); coordinator.advanceTime(state.rules.clashTimeoutMs);
    assert(coordinator.activeClash() == nullptr); assert(state.round == 2);
    assert(playerById(state, 17).cave == 1); assert(playerById(state, 42).cave == 2);
    assert(playerById(state, 17).health == 100); assert(playerById(state, 42).health == 100);
}

void moveIntoSearchResolvesSearchOnlyOnce() {
    auto state = clashFixture(); MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 2))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(search(42))); assert(coordinator.lockAction(42));
    assert(coordinator.activeClash() != nullptr);
    const std::size_t inventoryAfterSearch = playerById(state, 42).inventory.items.size();
    const auto clash = *coordinator.activeClash();
    assert(coordinator.submitClashResponse(17, clash.id, clash.challengeWord) == ClashSubmissionResult::Resolved);
    assert(playerById(state, 42).inventory.items.size() == inventoryAfterSearch);
    assert(state.round == 2);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::SearchCompleted) == 1);

    assert(coordinator.submitAction(search(17))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(search(42))); assert(coordinator.lockAction(42));
    assert(state.round == 3);
    assert(coordinator.activeClash() == nullptr);
}

void moveIntoShootStartsClashAndNeverLeavesColocation() {
    auto state = clashFixture(); MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 2))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(shoot(42, 1))); assert(coordinator.lockAction(42));
    assert(coordinator.activeClash() != nullptr);
    assert(coordinator.activeClash()->kind == ClashKind::MoveIntoStationary);
    assert(state.round == 1);

    const auto clash = *coordinator.activeClash();
    assert(coordinator.submitClashResponse(42, clash.id, clash.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == 2);
    assert(playerById(state, 17).cave != playerById(state, 42).cave);
    assert(playerById(state, 42).arrows == 2);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::ArrowFired) == 1);
}

void moveIntoContextualEscapeStartsClashThenEscapesOnce() {
    auto state = clashFixture();
    auto& escaping = const_cast<PlayerState&>(playerById(state, 42));
    escaping.heldSigilFrom = PlayerId{17};
    state.extraction.active = true;
    state.extraction.cave = CaveId{2};
    state.extraction.sigilHolder = PlayerId{42};
    MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 2))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(escape(42))); assert(coordinator.lockAction(42));
    assert(coordinator.activeClash() != nullptr);
    assert(coordinator.activeClash()->kind == ClashKind::MoveIntoStationary);

    const auto clash = *coordinator.activeClash();
    assert(coordinator.submitClashResponse(42, clash.id, clash.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.result.status == MatchStatus::Completed);
    assert(state.result.outcome == MatchOutcome::EscapedWithSigil);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::PlayerEscaped) == 1);
}

void occupantMovingAwayDoesNotCreateFalseOccupancyClash() {
    auto state = clashFixture(); MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 2))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(move(42, 3))); assert(coordinator.lockAction(42));
    assert(coordinator.activeClash() == nullptr);
    assert(state.round == 2);
    assert(playerById(state, 17).cave == CaveId{2});
    assert(playerById(state, 42).cave == CaveId{3});
}

void invalidVacatingMoveStillCreatesOccupancyClash() {
    auto state = clashFixture(); MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 2))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(move(42, 6))); assert(coordinator.lockAction(42));
    assert(coordinator.activeClash() != nullptr);
    assert(coordinator.activeClash()->kind == ClashKind::MoveIntoStationary);
}

void reconnectDuringClashRestoresSameChallengeAndSearch() {
    auto state = clashFixture();
    state.rules.disconnectGraceMs = 500;
    state.rules.clashTimeoutMs = 1000;
    MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 2))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(search(42))); assert(coordinator.lockAction(42));
    const ActiveClash original = *coordinator.activeClash();
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::SearchCompleted) == 1);

    coordinator.disconnect(17);
    coordinator.advanceTime(100);
    assert(coordinator.activeClash() != nullptr);
    assert(coordinator.activeClash()->id == original.id);
    assert(coordinator.activeClash()->challengeWord == original.challengeWord);
    assert(coordinator.activeClash()->remainingMs == original.remainingMs - 100);
    assert(coordinator.reconnect(17));
    assert(coordinator.hostSession(17)->connected);
    assert(coordinator.activeClash() != nullptr);
    assert(coordinator.activeClash()->id == original.id);
    assert(coordinator.activeClash()->challengeWord == original.challengeWord);

    assert(coordinator.submitClashResponse(42, original.id,
        original.challengeWord) == ClashSubmissionResult::Resolved);
    assert(coordinator.activeClash() == nullptr);
    assert(state.round == 2);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::SearchCompleted) == 1);
    const RoundNumber resolvedRound = state.round;
    coordinator.advanceTime(100);
    assert(state.round == resolvedRound);
}

void disconnectGraceExpiryClearsClashAndDoesNotRepeatUseItem() {
    auto state = clashFixture();
    state.rules.disconnectGraceMs = 50;
    state.rules.clashTimeoutMs = 1000;
    auto& stationary = const_cast<PlayerState&>(playerById(state, 42));
    stationary.health = 50;
    assert(stationary.inventory.add(ItemInstance{ItemType::HealingDraught},
        state.rules.maxInventoryItems));
    MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 2))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(use(42, ItemType::HealingDraught))); assert(coordinator.lockAction(42));
    const ClashId clash = coordinator.activeClash()->id;
    assert(playerById(state, 42).health == 100);
    assert(!playerById(state, 42).inventory.contains(ItemType::HealingDraught));

    coordinator.disconnect(17);
    coordinator.advanceTime(50);
    assert(!playerById(state, 17).alive);
    assert(coordinator.activeClash() == nullptr);
    assert(state.round == 2);
    assert(playerById(state, 42).health == 100);
    assert(!playerById(state, 42).inventory.contains(ItemType::HealingDraught));
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::ItemUsed) == 1);
    assert(coordinator.submitClashResponse(42, clash, "anything") ==
        ClashSubmissionResult::Rejected);
    const RoundNumber resolvedRound = state.round;
    coordinator.advanceTime(50);
    assert(state.round == resolvedRound);
}

void terminalDisconnectExpiryDiscardsPendingClashWithoutResolution() {
    auto state = clashFixture();
    state.rules.disconnectGraceMs = 50;
    state.rules.clashTimeoutMs = 1000;
    MatchCoordinator coordinator(state);
    assert(coordinator.submitAction(move(17, 3))); assert(coordinator.lockAction(17));
    assert(coordinator.submitAction(move(42, 3))); assert(coordinator.lockAction(42));
    const ClashId clash = coordinator.activeClash()->id;
    coordinator.disconnect(17);
    coordinator.disconnect(42);
    coordinator.advanceTime(50);

    assert(state.result.status == MatchStatus::Completed);
    assert(state.result.outcome == MatchOutcome::Draw);
    assert(coordinator.activeClash() == nullptr);
    assert(state.round == 1);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::MatchDrawn) == 1);
    assert(coordinator.submitClashResponse(17, clash, "anything") ==
        ClashSubmissionResult::Rejected);
    coordinator.advanceTime(50);
    assert(state.round == 1);
    assert(coordinator.authoritativeEvents().empty());
}

void sixHuntersRequireAllLocksAndResolveOnce() {
    auto state = multiHunterFixture(); MatchCoordinator coordinator(state);
    const RoundNumber round = state.round;
    for (PlayerId player = 101; player <= 105; ++player) {
        assert(coordinator.submitAction(search(player)));
        assert(coordinator.lockAction(player));
        assert(state.round == round);
    }
    assert(coordinator.submitAction(search(106)));
    assert(coordinator.lockAction(106));
    assert(state.round == round + 1);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::SearchCompleted) == 6);
}

void threeHuntersConvergeIntoOneComponent() {
    auto state = multiHunterFixture(3); MatchCoordinator coordinator(state);
    submitAndLockAll(coordinator, {move(101, 20), move(102, 20), move(103, 20)});
    const ActiveClash clash = *coordinator.activeClash();
    assert((clash.participants == std::vector<PlayerId>{101, 102, 103}));
    assert(state.round == 1);
    assert(coordinator.submitClashResponse(102, clash.id, clash.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == 2 && coordinator.activeClash() == nullptr);
    assert(playerById(state, 102).cave == 20);
    assert(playerById(state, 101).health == 80);
    assert(playerById(state, 103).health == 80);
    assert(playerById(state, 101).cave != playerById(state, 103).cave);
    std::set<CaveId> occupied;
    for (const auto& player : state.players) assert(occupied.insert(player.cave).second);
}

void fiveHuntersCanShareOneConflictComponent() {
    auto state = multiHunterFixture(5); MatchCoordinator coordinator(state);
    submitAndLockAll(coordinator,
        {move(101, 20), move(102, 20), move(103, 20), move(104, 20), move(105, 20)});
    const ActiveClash clash = *coordinator.activeClash();
    assert(clash.participants.size() == 5);
    assert(coordinator.submitClashResponse(105, clash.id, clash.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == 2 && playerById(state, 105).cave == 20);
    std::set<CaveId> occupied;
    for (const auto& player : state.players) {
        assert(occupied.insert(player.cave).second);
        if (player.id != 105) assert(player.health == 80);
    }
}

void fourAndSixHuntersCanShareOneConflictComponent() {
    for (const std::size_t count : {4U, 6U}) {
        auto state = multiHunterFixture(count);
        MatchCoordinator coordinator(state);
        std::vector<PlayerAction> actions;
        for (std::size_t index = 0; index < count; ++index)
            actions.push_back(move(static_cast<PlayerId>(101 + index), 20));
        submitAndLockAll(coordinator, actions);

        const ActiveClash clash = *coordinator.activeClash();
        assert(clash.participants.size() == count);
        const PlayerId winner = clash.participants.back();
        assert(coordinator.submitClashResponse(winner, clash.id, clash.challengeWord) ==
            ClashSubmissionResult::Resolved);
        assert(state.round == 2 && coordinator.activeClash() == nullptr);
        assert(playerById(state, winner).cave == 20);

        std::set<CaveId> occupied;
        for (const auto& player : state.players) {
            assert(occupied.insert(player.cave).second);
            if (player.id != winner) assert(player.health == 80);
        }
    }
}

void connectedOppositeAndDestinationConflictsFormOneComponent() {
    auto state = multiHunterFixture(3); state.world.connect(3, 1);
    MatchCoordinator coordinator(state);
    submitAndLockAll(coordinator, {move(101, 2), move(102, 1), move(103, 1)});
    const ActiveClash clash = *coordinator.activeClash();
    assert((clash.participants == std::vector<PlayerId>{101, 102, 103}));
    assert(coordinator.submitClashResponse(103, clash.id, clash.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == 2);
    assert(playerById(state, 103).cave == 1);
}

void multiPartyClashReconnectKeepsIdentityAndDoesNotResurrectResolvedClash() {
    auto state = multiHunterFixture(3);
    state.rules.disconnectGraceMs = 500;
    MatchCoordinator coordinator(state);
    submitAndLockAll(coordinator,
        {move(101, 20), move(102, 20), move(103, 20)});
    const ActiveClash original = *coordinator.activeClash();
    assert((original.participants ==
        std::vector<PlayerId>{101, 102, 103}));

    coordinator.disconnect(PlayerId{103});
    coordinator.advanceTime(100);
    assert(coordinator.activeClash() != nullptr);
    assert(coordinator.activeClash()->id == original.id);
    assert(coordinator.reconnect(PlayerId{103}));
    assert(coordinator.hostSession(PlayerId{103})->connected);
    assert(coordinator.activeClash()->id == original.id);

    assert(coordinator.submitClashResponse(
        PlayerId{103}, original.id, original.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == RoundNumber{2});
    assert(coordinator.activeClash() == nullptr);
    assert(coordinator.submitClashResponse(
        PlayerId{101}, original.id, original.challengeWord) ==
        ClashSubmissionResult::Rejected);
}

void defeatedAndTerminalSessionsCanReconnectWhileExternallyReserved() {
    auto state = multiHunterFixture(2);
    state.players.front().alive = false;
    state.players.front().health = 0;
    state.rules.disconnectGraceMs = 500;
    MatchCoordinator coordinator(state);
    coordinator.disconnect(PlayerId{101});
    assert(coordinator.reconnect(PlayerId{101}));
    coordinator.disconnect(PlayerId{101});
    state.result.status = MatchStatus::Completed;
    state.result.outcome = MatchOutcome::Draw;
    assert(state.result.status == MatchStatus::Completed);
    assert(coordinator.reconnect(PlayerId{101}));
    assert(!coordinator.reconnect(PlayerId{101}));

    coordinator.disconnect(PlayerId{101});
    coordinator.advanceTime(500);
    assert(!coordinator.reconnect(PlayerId{101}));
}

void disjointConflictComponentsQueueDeterministically() {
    auto state = multiHunterFixture(4);
    state.players[2].cave = 7; state.players[3].cave = 8;
    MatchCoordinator coordinator(state);
    submitAndLockAll(coordinator,
        {move(101, 20), move(102, 20), move(103, 21), move(104, 21)});
    const ActiveClash first = *coordinator.activeClash();
    assert((first.participants == std::vector<PlayerId>{101, 102}));
    assert(coordinator.submitClashResponse(101, first.id, first.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == 1);
    assert(playerById(state, 102).cave != 21);
    assert(playerById(state, 102).cave != 7);
    assert(playerById(state, 102).cave != 8);
    const ActiveClash second = *coordinator.activeClash();
    assert((second.participants == std::vector<PlayerId>{103, 104}));
    assert(second.id != first.id);
    assert(coordinator.submitClashResponse(104, second.id, second.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == 2 && coordinator.activeClash() == nullptr);
    assert(playerById(state, 101).cave == 20);
    assert(playerById(state, 104).cave == 21);
}

void clashTimeoutAdvancesToNextQueuedComponent() {
    auto state = multiHunterFixture(4);
    state.players[2].cave = 7;
    state.players[3].cave = 8;
    MatchCoordinator coordinator(state);
    submitAndLockAll(coordinator,
        {move(101, 20), move(102, 20), move(103, 21), move(104, 21)});

    const ActiveClash first = *coordinator.activeClash();
    assert((first.participants == std::vector<PlayerId>{101, 102}));
    coordinator.advanceTime(state.rules.clashTimeoutMs);
    assert(state.round == 1);

    const ActiveClash second = *coordinator.activeClash();
    assert(second.id != first.id);
    assert((second.participants == std::vector<PlayerId>{103, 104}));
    assert(coordinator.submitClashResponse(104, second.id, second.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == 2 && coordinator.activeClash() == nullptr);

    std::set<CaveId> occupied;
    for (const auto& player : state.players)
        assert(occupied.insert(player.cave).second);
}

void clashResponseAndTimeoutBoundaryCannotResolveTwice() {
    {
        auto state = multiHunterFixture(3);
        MatchCoordinator coordinator(state);
        submitAndLockAll(coordinator,
            {move(101, 20), move(102, 20), move(103, 20)});
        const ActiveClash clash = *coordinator.activeClash();
        assert(coordinator.submitClashResponse(
            101, clash.id, clash.challengeWord) ==
            ClashSubmissionResult::Resolved);
        assert(state.round == 2);
        coordinator.advanceTime(clash.remainingMs);
        assert(state.round == 2);
        assert(coordinator.activeClash() == nullptr);
        assert(coordinator.submitClashResponse(
            102, clash.id, clash.challengeWord) ==
            ClashSubmissionResult::Rejected);
    }
    {
        auto state = multiHunterFixture(3);
        MatchCoordinator coordinator(state);
        submitAndLockAll(coordinator,
            {move(101, 20), move(102, 20), move(103, 20)});
        const ActiveClash clash = *coordinator.activeClash();
        coordinator.advanceTime(clash.remainingMs);
        assert(state.round == 2);
        assert(coordinator.activeClash() == nullptr);
        assert(coordinator.submitClashResponse(
            101, clash.id, clash.challengeWord) ==
            ClashSubmissionResult::Rejected);
        coordinator.advanceTime(clash.remainingMs);
        assert(state.round == 2);
    }
}

void multiPartyStalemateCancelsMovesAndPreservesSearchOnce() {
    auto state = multiHunterFixture(3); MatchCoordinator coordinator(state);
    submitAndLockAll(coordinator, {move(101, 2), search(102), move(103, 2)});
    assert(coordinator.activeClash() != nullptr);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::SearchCompleted) == 1);
    coordinator.advanceTime(state.rules.clashTimeoutMs);
    assert(state.round == 2 && coordinator.activeClash() == nullptr);
    assert(playerById(state, 101).cave == 1);
    assert(playerById(state, 102).cave == 2);
    assert(playerById(state, 103).cave == 3);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::SearchCompleted) == 1);
}

void movementChainAndCycleWithUniqueDestinationsRemainLegal() {
    auto chain = multiHunterFixture(3); MatchCoordinator chainCoordinator(chain);
    submitAndLockAll(chainCoordinator, {move(101, 2), move(102, 3), move(103, 4)});
    assert(chainCoordinator.activeClash() == nullptr && chain.round == 2);
    assert(playerById(chain, 101).cave == 2);
    assert(playerById(chain, 102).cave == 3);
    assert(playerById(chain, 103).cave == 4);

    auto cycle = multiHunterFixture(3); MatchCoordinator cycleCoordinator(cycle);
    cycle.world.connect(3, 1);
    submitAndLockAll(cycleCoordinator, {move(101, 2), move(102, 3), move(103, 1)});
    assert(cycleCoordinator.activeClash() == nullptr && cycle.round == 2);
    assert(playerById(cycle, 101).cave == 2);
    assert(playerById(cycle, 102).cave == 3);
    assert(playerById(cycle, 103).cave == 1);
}

void multiHunterSubmissionOrderDoesNotChangeResolution() {
    auto forward = multiHunterFixture(4);
    auto reverse = forward;
    const std::vector<PlayerAction> actions{
        move(101, 16), move(102, 3), move(103, 4), move(104, 5)};
    MatchCoordinator forwardCoordinator(forward);
    submitAndLockAll(forwardCoordinator, actions);
    MatchCoordinator reverseCoordinator(reverse);
    submitAndLockAll(reverseCoordinator,
        std::vector<PlayerAction>(actions.rbegin(), actions.rend()));
    assert(forward.round == 2 && reverse.round == 2);
    for (PlayerId player = 101; player <= 104; ++player)
        assert(playerById(forward, player).cave == playerById(reverse, player).cave);
}

void forfeitAndDisconnectExpiryCannotDeadlockMultiHunterReadiness() {
    auto forfeited = multiHunterFixture(); MatchCoordinator forfeitCoordinator(forfeited);
    for (PlayerId player = 101; player <= 105; ++player) {
        assert(forfeitCoordinator.submitAction(search(player)));
        assert(forfeitCoordinator.lockAction(player));
    }
    forfeitCoordinator.forfeit(106);
    assert(!playerById(forfeited, 106).alive);
    assert(forfeited.round == 2);

    auto disconnected = multiHunterFixture();
    disconnected.rules.disconnectGraceMs = 10;
    MatchCoordinator disconnectCoordinator(disconnected);
    for (PlayerId player = 101; player <= 105; ++player) {
        assert(disconnectCoordinator.submitAction(search(player)));
        assert(disconnectCoordinator.lockAction(player));
    }
    disconnectCoordinator.disconnect(106);
    disconnectCoordinator.advanceTime(10);
    assert(!playerById(disconnected, 106).alive);
    assert(disconnected.round == 2);
}

void multipleReserveExpiriesResolveOneRoundExactlyOnce() {
    auto state = multiHunterFixture(6);
    state.rules.disconnectGraceMs = 25;
    MatchCoordinator coordinator(state);
    for (PlayerId player = 101; player <= 104; ++player) {
        assert(coordinator.submitAction(search(player)));
        assert(coordinator.lockAction(player));
    }
    coordinator.disconnect(105);
    coordinator.disconnect(106);
    coordinator.advanceTime(24);
    assert(state.round == 1);
    assert(playerById(state, 105).alive);
    assert(playerById(state, 106).alive);
    coordinator.advanceTime(1);
    assert(state.round == 2);
    assert(!playerById(state, 105).alive);
    assert(!playerById(state, 106).alive);
    coordinator.advanceTime(100);
    assert(state.round == 2);
    assert(coordinator.activeClash() == nullptr);
}

void reconnectChurnPreservesSubmitAndLockStagesNearExpiry() {
    auto state = multiHunterFixture(6);
    state.rules.disconnectGraceMs = 25;
    MatchCoordinator coordinator(state);
    for (PlayerId player = 101; player <= 103; ++player) {
        assert(coordinator.submitAction(search(player)));
        assert(coordinator.lockAction(player));
    }

    coordinator.disconnect(104); // Before submit.
    assert(coordinator.submitAction(search(105)));
    coordinator.disconnect(105); // After submit, before lock.
    assert(coordinator.submitAction(search(106)));
    assert(coordinator.lockAction(106));
    coordinator.disconnect(106); // Immediately after lock.
    coordinator.advanceTime(24);
    assert(state.round == 1);

    assert(coordinator.reconnect(104));
    assert(coordinator.reconnect(105));
    assert(coordinator.reconnect(106));
    assert(coordinator.submitAction(search(104)));
    assert(coordinator.lockAction(104));
    assert(coordinator.lockAction(105));
    assert(state.round == 2);
    assert(coordinator.activeClash() == nullptr);

    coordinator.disconnect(106);
    assert(coordinator.reconnect(106));
    const HostSessionState* restored = coordinator.hostSession(106);
    assert(restored != nullptr && restored->connected);
    assert(!restored->pendingAction.has_value());
    assert(!restored->actionLocked);
    coordinator.advanceTime(100);
    assert(state.round == 2);
}

void participantDeathCannotStrandQueuedConflictRound() {
    auto state = multiHunterFixture(4);
    state.players[2].cave = 7; state.players[3].cave = 8;
    MatchCoordinator coordinator(state);
    submitAndLockAll(coordinator,
        {move(101, 20), move(102, 20), move(103, 21), move(104, 21)});
    assert(coordinator.activeClash() != nullptr);
    coordinator.forfeit(101);
    assert(!playerById(state, 101).alive);
    const ActiveClash second = *coordinator.activeClash();
    assert((second.participants == std::vector<PlayerId>{103, 104}));
    assert(coordinator.submitClashResponse(103, second.id, second.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == 2 && coordinator.activeClash() == nullptr);
}

void multipleBodiesCoexistAndLoneSurvivorContinues() {
    auto state = multiHunterFixture(4); MatchCoordinator coordinator(state);
    coordinator.forfeit(102);
    coordinator.forfeit(103);
    coordinator.forfeit(104);
    assert(state.result.status == MatchStatus::Active);
    assert(playerById(state, 101).alive);
    assert(state.bodies.size() == 3);
    for (PlayerId owner = 102; owner <= 104; ++owner) {
        const auto body = std::find_if(state.bodies.begin(), state.bodies.end(),
            [&](const BodyState& candidate) { return candidate.owner == owner; });
        assert(body != state.bodies.end());
    }
    assert(coordinator.submitAction(search(101)));
    assert(coordinator.lockAction(101));
    assert(state.round == 2);
    coordinator.forfeit(101);
    assert(state.result.status == MatchStatus::Completed);
    assert(state.result.outcome == MatchOutcome::Draw);
}

void multiHunterSnapshotsRemainPlayerSpecific() {
    auto state = multiHunterFixture(6);
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FogOfWar;
    for (const auto& player : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, {});
        assert(snapshot.player == player.id);
        assert(snapshot.currentCave == player.cave);
        assert(snapshot.map.currentCave == player.cave);
        assert(snapshot.map.caves.size() == 1);
        assert(snapshot.map.caves.front().cave == player.cave);
        for (const auto& rival : state.players) {
            if (rival.id == player.id) continue;
            assert(std::none_of(snapshot.map.caves.begin(), snapshot.map.caves.end(),
                [&](const DiscoveredCaveView& cave) { return cave.cave == rival.cave; }));
        }
    }
}

} // namespace

int main() {
    actionsCanChangeUntilLockedAndTwoLocksResolve();
    reserveRunsOnlyForHunterMakingLockedOpponentWait();
    reconnectWithinGracePreservesHunterAndActionState();
    explicitForfeitEliminatesImmediatelyAndDoesNotUseGrace();
    disconnectTimeoutKillsHunterAndLeavesRecoverableBody();
    reserveExpirationKillsWaitingHunterAndResolvesLockedSurvivor();
    deadPlayerDisconnectDoesNotBlockLivingHunter();
    bothDisconnectingCanEndInDraw();
    nonConflictingTunnelMovesResolveOnceAndReachSnapshots();
    lockedPendingMoveDoesNotAdvanceJackalFlee();
    nonConflictingMoveAndSearchBothResolve();
    ordinaryMovesMatchDirectRoundResolution();
    sameDestinationClashPausesAndFirstCorrectWins();
    oppositeTraversalAndTimeoutStalemate();
    moveIntoSearchResolvesSearchOnlyOnce();
    moveIntoShootStartsClashAndNeverLeavesColocation();
    moveIntoContextualEscapeStartsClashThenEscapesOnce();
    occupantMovingAwayDoesNotCreateFalseOccupancyClash();
    invalidVacatingMoveStillCreatesOccupancyClash();
    moveIntoUseItemConsumesExactlyOnceAndBlocksNormalCommands();
    reconnectDuringClashRestoresSameChallengeAndSearch();
    disconnectGraceExpiryClearsClashAndDoesNotRepeatUseItem();
    terminalDisconnectExpiryDiscardsPendingClashWithoutResolution();
    sixHuntersRequireAllLocksAndResolveOnce();
    threeHuntersConvergeIntoOneComponent();
    fiveHuntersCanShareOneConflictComponent();
    fourAndSixHuntersCanShareOneConflictComponent();
    connectedOppositeAndDestinationConflictsFormOneComponent();
    multiPartyClashReconnectKeepsIdentityAndDoesNotResurrectResolvedClash();
    defeatedAndTerminalSessionsCanReconnectWhileExternallyReserved();
    disjointConflictComponentsQueueDeterministically();
    clashTimeoutAdvancesToNextQueuedComponent();
    clashResponseAndTimeoutBoundaryCannotResolveTwice();
    multiPartyStalemateCancelsMovesAndPreservesSearchOnce();
    movementChainAndCycleWithUniqueDestinationsRemainLegal();
    multiHunterSubmissionOrderDoesNotChangeResolution();
    forfeitAndDisconnectExpiryCannotDeadlockMultiHunterReadiness();
    multipleReserveExpiriesResolveOneRoundExactlyOnce();
    reconnectChurnPreservesSubmitAndLockStagesNearExpiry();
    participantDeathCannotStrandQueuedConflictRound();
    multipleBodiesCoexistAndLoneSurvivorContinues();
    multiHunterSnapshotsRemainPlayerSpecific();

    std::cout << "Basilisk match coordinator tests passed.\n";
    return 0;
}
