#include <algorithm>
#include <cassert>
#include <iostream>

#include "basilisk/Action.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/systems/MatchCoordinator.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

PlayerAction search(PlayerId player) {
    PlayerAction action;
    action.player = player;
    action.type = ActionType::Search;
    return action;
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

void actionsCanChangeUntilLockedAndTwoLocksResolve() {
    auto state = MapGenerator::generate(101, 202);
    MatchCoordinator coordinator(state);

    auto first = search(1);
    assert(coordinator.submitAction(first));

    auto replacement = search(1);
    replacement.type = ActionType::Shoot; // invalid target is okay; RoundController safely rejects it.
    assert(coordinator.submitAction(replacement));
    assert(coordinator.session(1)->pendingAction->type == ActionType::Shoot);

    assert(coordinator.lockAction(1));
    assert(state.round == 1);
    assert(!coordinator.submitAction(first));

    assert(coordinator.submitAction(search(2)));
    assert(coordinator.lockAction(2));
    assert(state.round == 2);
    assert(!coordinator.session(1)->actionLocked);
    assert(!coordinator.session(2)->actionLocked);
    assert(!coordinator.session(1)->pendingAction.has_value());
    assert(!coordinator.session(2)->pendingAction.has_value());
}

void reserveRunsOnlyForHunterMakingLockedOpponentWait() {
    auto state = MapGenerator::generate(303, 404);
    state.rules.multiplayerReserveMs = 300000;
    MatchCoordinator coordinator(state);

    assert(coordinator.submitAction(search(1)));
    assert(coordinator.lockAction(1));
    coordinator.advanceTime(12500);

    assert(coordinator.session(1)->reserveRemainingMs == 300000);
    assert(coordinator.session(2)->reserveRemainingMs == 287500);
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
    assert(hasEvent(coordinator.lastEvents(), GameEventType::PlayerDisconnected));
    assert(!coordinator.session(2)->connected);

    coordinator.advanceTime(29000);
    assert(playerById(state, 2).alive);
    assert(coordinator.session(2)->pendingAction.has_value());

    coordinator.reconnect(2);
    assert(hasEvent(coordinator.lastEvents(), GameEventType::PlayerReconnected));
    assert(coordinator.session(2)->connected);
    assert(coordinator.session(2)->disconnectGraceRemainingMs == 30000);
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
    assert(hasEvent(coordinator.lastEvents(), GameEventType::PlayerDisconnectTimedOut));
    assert(hasEvent(coordinator.lastEvents(), GameEventType::PlayerKilled));
    assert(hasEvent(coordinator.lastEvents(), GameEventType::BodyCreated));

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
    assert(hasEvent(coordinator.lastEvents(), GameEventType::PlayerReserveExpired));
    assert(state.round == 2); // Player 1's already-locked action resolves immediately.

    assert(coordinator.submitAction(search(1)));
    assert(coordinator.lockAction(1));
    assert(state.round == 3); // Only the surviving hunter is required now.
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
    assert(hasEvent(coordinator.lastEvents(), GameEventType::MatchDrawn));
}

} // namespace

int main() {
    actionsCanChangeUntilLockedAndTwoLocksResolve();
    reserveRunsOnlyForHunterMakingLockedOpponentWait();
    reconnectWithinGracePreservesHunterAndActionState();
    disconnectTimeoutKillsHunterAndLeavesRecoverableBody();
    reserveExpirationKillsWaitingHunterAndResolvesLockedSurvivor();
    bothDisconnectingCanEndInDraw();

    std::cout << "Basilisk match coordinator tests passed.\n";
    return 0;
}
