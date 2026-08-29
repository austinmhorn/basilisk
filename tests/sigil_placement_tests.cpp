#include <cassert>
#include <algorithm>

#include "basilisk/systems/SigilPlacementSystem.hpp"
#include "basilisk/systems/TurnResolver.hpp"

using namespace basilisk;

namespace {

MatchState blockedFixture(PlayerId dead = PlayerId{2}) {
    MatchState state;
    for (CaveId cave = 1; cave <= 6; ++cave) state.world.addCave(cave);
    state.world.connect(1, 2);
    state.world.connect(1, 3);
    state.world.connect(2, 4);
    state.world.connect(3, 5);
    state.world.connect(5, 6);
    state.basilisk.cave = 1;
    state.basilisk.alive = true;
    state.pits.push_back(PitState{2, true});
    state.players.push_back(PlayerState{PlayerId{1}, CaveId{4}});
    state.players.push_back(PlayerState{dead, CaveId{1}, 0, 3, false});
    return state;
}

void basiliskPitAndBlockedComponentsAreExcluded() {
    MatchState state = blockedFixture();
    // Cave 2 is an active Pit. Caves 3/5/6 are cut off from the living hunter
    // when Cave 1 (the Basilisk cave) is removed.
    assert(nearestRecoverableSigilCave(state, CaveId{1}) == CaveId{4});
    std::vector<GameEvent> events;
    placeSigilsForDeath(state, state.players[1], CaveId{1}, events);
    assert(state.bodies.size() == 1);
    assert(state.bodies.front().sigilAvailable);
    assert(state.bodies.front().sigilCave == CaveId{4});
}

void basiliskContactDeathUsesRecoverablePlacement() {
    MatchState state = blockedFixture();
    state.players[1].health = 100;
    state.players[1].alive = true;
    const std::vector<GameEvent> events = TurnResolver{}.resolve(state, {});
    assert(!state.players[1].alive);
    assert(state.bodies.size() == 1);
    assert(state.bodies.front().sigilCave == CaveId{4});
    assert(std::ranges::any_of(events, [](const GameEvent& event) {
        return event.type == GameEventType::SigilEjected &&
            event.cave == CaveId{4};
    }));
}

void nearestAndTieBreakAreDeterministic() {
    MatchState state;
    for (CaveId cave = 1; cave <= 4; ++cave) state.world.addCave(cave);
    state.world.connect(1, 2);
    state.world.connect(1, 3);
    state.world.connect(2, 4);
    state.world.connect(3, 4);
    state.basilisk.cave = 1;
    state.players.push_back(PlayerState{PlayerId{1}, CaveId{4}});
    assert(nearestRecoverableSigilCave(state, CaveId{1}) == CaveId{2});
    state.pits.push_back(PitState{2, true});
    assert(nearestRecoverableSigilCave(state, CaveId{1}) == CaveId{3});
}

void validDropIsUnchanged() {
    MatchState state;
    state.world.connect(1, 2);
    state.world.connect(2, 3);
    state.basilisk.cave = 1;
    state.players.push_back(PlayerState{PlayerId{1}, CaveId{3}});
    state.players.push_back(PlayerState{PlayerId{2}, CaveId{2}, 0, 3, false});
    assert(nearestRecoverableSigilCave(state, CaveId{2}) == CaveId{2});
    std::vector<GameEvent> events;
    placeSigilsForDeath(state, state.players[1], CaveId{2}, events);
    assert(state.bodies.front().sigilCave == CaveId{2});
}

void playerIdentityDoesNotChangePlacement() {
    for (const PlayerId dead : {PlayerId{2}, PlayerId{99}}) {
        MatchState state = blockedFixture(dead);
        std::vector<GameEvent> events;
        placeSigilsForDeath(state, state.players[1], CaveId{1}, events);
        assert(state.bodies.front().owner == dead);
        assert(state.bodies.front().sigilCave == CaveId{4});
    }
}

void carriedSigilDropsThroughSameRule() {
    MatchState state = blockedFixture();
    state.bodies.push_back(BodyState{PlayerId{7}, CaveId{6}, false, CaveId{6}});
    state.players[1].heldSigilFrom = PlayerId{7};
    state.extraction.active = true;
    state.extraction.cave = CaveId{6};
    state.extraction.sigilHolder = state.players[1].id;
    std::vector<GameEvent> events;
    placeSigilsForDeath(state, state.players[1], CaveId{1}, events);
    const auto carriedBody = std::find_if(state.bodies.begin(), state.bodies.end(),
        [](const BodyState& body) { return body.owner == PlayerId{7}; });
    assert(carriedBody != state.bodies.end());
    assert(carriedBody->sigilAvailable);
    assert(carriedBody->sigilCave == CaveId{4});
    assert(!state.players[1].heldSigilFrom.has_value());
    assert(!state.extraction.active && !state.extraction.sigilHolder);
}

} // namespace

int main() {
    basiliskPitAndBlockedComponentsAreExcluded();
    basiliskContactDeathUsesRecoverablePlacement();
    nearestAndTieBreakAreDeterministic();
    validDropIsUnchanged();
    playerIdentityDoesNotChangePlacement();
    carriedSigilDropsThroughSameRule();
}
