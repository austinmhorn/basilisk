#include <cassert>
#include <iostream>

#include "basilisk/Action.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Player.hpp"
#include "basilisk/systems/SoloCoordinator.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"

using namespace basilisk;

namespace {

MatchState makeSoloState(CaveId basiliskCave) {
    MatchState state;
    state.world.addCave(1);
    state.world.addCave(2);
    state.world.addCave(3);
    state.world.connect(1, 2);
    state.world.connect(2, 3);

    PlayerState player;
    player.id = 1;
    player.cave = 1;
    player.health = state.rules.maxHealth;
    player.arrows = state.rules.startingArrows;
    state.players = {player};
    state.basilisk.cave = basiliskCave;
    return state;
}

bool hasEvent(const std::vector<GameEvent>& events, GameEventType type) {
    for (const auto& event : events) {
        if (event.type == type) return true;
    }
    return false;
}

PlayerAction materialize(PlayerId player, const AvailableAction& available) {
    PlayerAction action;
    action.player = player;
    action.type = available.type;
    action.targetCave = available.targetCave;
    action.targetTunnel = available.targetTunnel;
    action.targetItem = available.targetItem;
    action.contextualAction = available.contextualAction;
    return action;
}

const AvailableAction& firstActionOfType(
    const PlayerRoundSnapshot& snapshot,
    ActionType type) {
    for (const auto& action : snapshot.availableActions) {
        if (action.type == type) return action;
    }
    assert(false && "Expected action type was not available.");
    return snapshot.availableActions.front();
}

void snapshotActionResolvesImmediately() {
    auto state = makeSoloState(3);
    assert(state.players.size() == 1);

    SoloCoordinator solo(state);
    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    const auto action = materialize(
        snapshot.player,
        firstActionOfType(snapshot, ActionType::Move));

    assert(snapshot.round == state.round);
    assert(snapshot.player == PlayerId{1});

    const auto startingRound = state.round;
    assert(solo.submitAction(action));
    assert(state.round == startingRound + 1);
    assert(state.players.front().cave == CaveId{2});
    assert(hasEvent(solo.lastEvents(), GameEventType::PlayerMoved));
}

void unknownAndDeadPlayersAreRejected() {
    auto state = makeSoloState(3);
    SoloCoordinator solo(state);

    PlayerAction invalid;
    invalid.player = 2;
    invalid.type = ActionType::Search;
    assert(!solo.submitAction(invalid));

    state.players.front().alive = false;
    PlayerAction search;
    search.player = 1;
    search.type = ActionType::Search;
    assert(!solo.submitAction(search));
}

void soloResolutionUsesNormalCoreHazards() {
    auto state = makeSoloState(2);
    SoloCoordinator solo(state);
    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    const auto action = materialize(
        snapshot.player,
        firstActionOfType(snapshot, ActionType::Move));

    const auto startingRound = state.round;
    assert(solo.submitAction(action));
    assert(state.round == startingRound + 1);
    assert(!state.players.front().alive);
    assert(hasEvent(solo.lastEvents(), GameEventType::PlayerKilled));
    assert(state.result.status == MatchStatus::Active);
}

} // namespace

int main() {
    snapshotActionResolvesImmediately();
    unknownAndDeadPlayersAreRejected();
    soloResolutionUsesNormalCoreHazards();

    std::cout << "Solo coordinator tests passed.\n";
    return 0;
}
