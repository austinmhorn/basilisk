#include <cassert>
#include <iostream>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/systems/TurnResolver.hpp"

using namespace basilisk;

namespace {

MatchState makeTestMatch() {
    MatchState state;
    state.matchSeed = 1001;
    state.mapSeed = 2002;

    for (CaveId cave = 1; cave <= 6; ++cave) {
        state.world.addCave(cave);
    }

    state.world.connect(1, 2);
    state.world.connect(2, 3);
    state.world.connect(1, 4);
    state.world.connect(2, 5);
    state.world.connect(3, 6);
    state.world.connect(4, 5);
    state.world.connect(5, 6);

    return state;
}

void movementResolvesBeforeShooting() {
    auto state = makeTestMatch();
    state.players = {
        PlayerState{1, 1, 100, 3, true},
        PlayerState{2, 5, 100, 3, true}
    };

    const std::vector<PlayerAction> actions{
        PlayerAction{1, ActionType::Shoot, CaveId{2}},
        PlayerAction{2, ActionType::Move, CaveId{2}}
    };

    TurnResolver resolver;
    resolver.resolve(state, actions);

    assert(state.players[1].cave == 2);
    assert(state.players[1].health == 60);
    assert(state.players[0].arrows == 2);
}

void lethalShotsResolveSimultaneously() {
    auto state = makeTestMatch();
    state.players = {
        PlayerState{1, 1, 20, 3, true},
        PlayerState{2, 2, 20, 3, true}
    };

    const std::vector<PlayerAction> actions{
        PlayerAction{1, ActionType::Shoot, CaveId{2}},
        PlayerAction{2, ActionType::Shoot, CaveId{1}}
    };

    TurnResolver resolver;
    resolver.resolve(state, actions);

    assert(!state.players[0].alive);
    assert(!state.players[1].alive);
    assert(state.players[0].health == 0);
    assert(state.players[1].health == 0);
}

} // namespace

int main() {
    movementResolvesBeforeShooting();
    lethalShotsResolveSimultaneously();

    std::cout << "BasiliskCore tests passed.\n";
    return 0;
}
