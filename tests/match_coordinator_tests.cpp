#include <algorithm>
#include <cassert>
#include <iostream>

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

    coordinator.reconnect(2);
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
    assert(!hasEvent(
        coordinator.authoritativeEvents(), GameEventType::PlayerDisconnected));
    assert(!hasEvent(coordinator.authoritativeEvents(),
        GameEventType::PlayerDisconnectTimedOut));
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::PlayerKilled));
    assert(hasEvent(coordinator.authoritativeEvents(), GameEventType::BodyCreated));

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
    coordinator.reconnect(17);
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
    nonConflictingMoveAndSearchBothResolve();
    ordinaryMovesMatchDirectRoundResolution();
    sameDestinationClashPausesAndFirstCorrectWins();
    oppositeTraversalAndTimeoutStalemate();
    moveIntoSearchResolvesSearchOnlyOnce();
    moveIntoUseItemConsumesExactlyOnceAndBlocksNormalCommands();
    reconnectDuringClashRestoresSameChallengeAndSearch();
    disconnectGraceExpiryClearsClashAndDoesNotRepeatUseItem();
    terminalDisconnectExpiryDiscardsPendingClashWithoutResolution();

    std::cout << "Basilisk match coordinator tests passed.\n";
    return 0;
}
