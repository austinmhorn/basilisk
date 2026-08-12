#include <cassert>
#include <iostream>

#include "basilisk/MatchState.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"

using namespace basilisk;

int main() {
    MatchState state;
    state.world.addCave(1);
    state.world.addCave(2);
    state.world.connect(1, 2);
    state.players = {PlayerState{1, 1, 100, 5, true}};

    state.looseArrows = {CaveId{1}, CaveId{2}};
    auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    assert(snapshot.looseArrowPresent);

    state.looseArrows = {CaveId{2}};
    snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    assert(!snapshot.looseArrowPresent);

    std::cout << "Loose arrow visibility tests passed.\n";
    return 0;
}
