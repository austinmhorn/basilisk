#include <cassert>
#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/systems/TurnResolver.hpp"

using namespace basilisk;

namespace {

bool hasEvent(const std::vector<GameEvent>& events, GameEventType type) {
    for (const auto& event : events) {
        if (event.type == type) return true;
    }
    return false;
}

int distanceBetween(const MatchState& state, CaveId start, CaveId target) {
    std::queue<CaveId> frontier;
    std::unordered_map<CaveId, int> distance;
    frontier.push(start);
    distance[start] = 0;

    while (!frontier.empty()) {
        const CaveId current = frontier.front();
        frontier.pop();
        if (current == target) return distance.at(current);
        for (const CaveId next : state.world.cave(current).connections) {
            if (distance.contains(next)) continue;
            distance[next] = distance.at(current) + 1;
            frontier.push(next);
        }
    }
    return -1;
}

MatchState makeRelocationMatch(MatchSeed seed) {
    MatchState state;
    state.matchSeed = seed;
    state.mapSeed = 9001;

    for (CaveId cave = 1; cave <= 7; ++cave) state.world.addCave(cave);
    for (CaveId cave = 1; cave < 7; ++cave) state.world.connect(cave, cave + 1);

    state.players = {PlayerState{1, 1, 100, 3, true}};
    state.basilisk.cave = 2;

    // Cave 3 is adjacent but forbidden by a Pit. Cave 1 is occupied. This
    // forces evade relocation to respect both safety exclusions and graph
    // distance rather than simply picking the nearest cave.
    state.pits = {PitState{3, true}};
    return state;
}

void firstEvadeRelocatesTwoToThreeCavesAway() {
    TurnResolver resolver;
    bool foundEvade = false;

    for (MatchSeed seed = 1; seed <= 10000 && !foundEvade; ++seed) {
        auto state = makeRelocationMatch(seed);
        const CaveId origin = state.basilisk.cave;
        const auto events = resolver.resolve(state, {
            PlayerAction{1, ActionType::Shoot, CaveId{2}}
        });

        if (!hasEvent(events, GameEventType::BasiliskEvaded)) continue;
        foundEvade = true;

        const int distance = distanceBetween(state, origin, state.basilisk.cave);
        assert(state.basilisk.alive);
        assert(state.basilisk.trueEncounters == 1);
        assert(hasEvent(events, GameEventType::BasiliskMoved));
        assert(distance >= 2 && distance <= 3);
        assert(state.basilisk.cave != CaveId{1});
        assert(state.basilisk.cave != CaveId{3});
        assert(state.basilisk.lastCave == CaveId{2});
    }

    assert(foundEvade);
}

void secondEvadeRelocatesOneToTwoCavesAwayBeforeEnraging() {
    TurnResolver resolver;
    bool foundEvade = false;

    for (MatchSeed seed = 1; seed <= 10000 && !foundEvade; ++seed) {
        auto state = makeRelocationMatch(seed);
        state.basilisk.trueEncounters = 1;
        state.basilisk.behavior = BasiliskBehavior::Restless;
        const CaveId origin = state.basilisk.cave;

        const auto events = resolver.resolve(state, {
            PlayerAction{1, ActionType::Shoot, CaveId{2}}
        });

        if (!hasEvent(events, GameEventType::BasiliskEvaded)) continue;
        foundEvade = true;

        const int distance = distanceBetween(state, origin, state.basilisk.cave);
        assert(state.basilisk.alive);
        assert(state.basilisk.trueEncounters == 2);
        assert(hasEvent(events, GameEventType::BasiliskMoved));
        assert(distance >= 1 && distance <= 2);
        assert(state.basilisk.cave != CaveId{1});
        assert(state.basilisk.cave != CaveId{3});
        assert(state.basilisk.lastCave == CaveId{2});
        assert(state.basilisk.behavior == BasiliskBehavior::Enraged);
        assert(hasEvent(events, GameEventType::BasiliskBehaviorChanged));
    }

    assert(foundEvade);
}

void enragedBasiliskPursuesNearestLivingHunter() {
    MatchState state;
    state.matchSeed = 424242;
    state.mapSeed = 9002;
    for (CaveId cave = 1; cave <= 5; ++cave) state.world.addCave(cave);
    for (CaveId cave = 1; cave < 5; ++cave) state.world.connect(cave, cave + 1);

    // Hunter 2 at Cave 3 is nearer than Hunter 1 at Cave 1. From Cave 5 the
    // only safe pursuit step is Cave 4, which reduces distance to Hunter 2.
    state.players = {
        PlayerState{1, 1, 100, 3, true},
        PlayerState{2, 3, 100, 3, true}
    };
    state.basilisk.cave = 5;
    state.basilisk.behavior = BasiliskBehavior::Enraged;
    state.basilisk.roundsSinceMove = 1;

    const int before = distanceBetween(state, state.basilisk.cave, CaveId{3});
    TurnResolver resolver;
    const auto events = resolver.resolve(state, {});
    const int after = distanceBetween(state, state.basilisk.cave, CaveId{3});

    assert(hasEvent(events, GameEventType::BasiliskMoved));
    assert(state.basilisk.cave == CaveId{4});
    assert(after < before);
    assert(state.basilisk.cave != CaveId{3});
}

} // namespace

int main() {
    firstEvadeRelocatesTwoToThreeCavesAway();
    secondEvadeRelocatesOneToTwoCavesAwayBeforeEnraging();
    enragedBasiliskPursuesNearestLivingHunter();
    std::cout << "Basilisk evade relocation tests passed.\n";
    return 0;
}
