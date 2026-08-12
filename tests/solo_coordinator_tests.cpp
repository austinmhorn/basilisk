#include <cassert>
#include <iostream>

#include "basilisk/Action.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Player.hpp"
#include "basilisk/systems/SoloCoordinator.hpp"

using namespace basilisk;

int main() {
    MatchState state;
    state.world.addCave(1);
    state.world.addCave(2);
    state.world.connect(1, 2);

    PlayerState player;
    player.id = 1;
    player.cave = 1;
    player.health = state.rules.maxHealth;
    player.arrows = state.rules.startingArrows;
    state.players = {player};
    state.basilisk.cave = 2;

    SoloCoordinator solo(state);

    PlayerAction search;
    search.player = 1;
    search.type = ActionType::Search;

    const auto startingRound = state.round;
    assert(solo.submitAction(search));
    assert(state.round == startingRound + 1);
    assert(!solo.lastEvents().empty());

    PlayerAction invalid;
    invalid.player = 2;
    invalid.type = ActionType::Search;
    assert(!solo.submitAction(invalid));

    state.players.front().alive = false;
    assert(!solo.submitAction(search));

    std::cout << "Solo coordinator tests passed.\n";
    return 0;
}
