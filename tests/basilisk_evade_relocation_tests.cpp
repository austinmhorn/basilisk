#include <cassert>
#include <iostream>
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

MatchState makeForcedRelocationMatch(MatchSeed seed) {
    MatchState state;
    state.matchSeed = seed;
    state.mapSeed = 9001;

    for (CaveId cave = 1; cave <= 6; ++cave) {
        state.world.addCave(cave);
    }

    // The hunter can shoot the Basilisk from Cave 1 into Cave 2.
    // Cave 6 is intentionally not adjacent to Cave 2, proving evade relocation
    // is map-wide rather than limited by tunnel distance.
    state.world.connect(1, 2);

    state.players = {
        PlayerState{1, 1, 100, 3, true}
    };
    state.basilisk.cave = 2;

    // Current cave 2 is illegal by definition, Cave 1 is occupied by the
    // living hunter, and Caves 3/4/5 are active Pits. Cave 6 is therefore the
    // only legal evade destination anywhere on the map.
    state.pits = {
        PitState{3, true},
        PitState{4, true},
        PitState{5, true}
    };

    return state;
}

void firstEvadeRelocatesAnywhereButPitOccupiedOrCurrentCave() {
    TurnResolver resolver;
    bool foundEvade = false;

    for (MatchSeed seed = 1; seed <= 10000 && !foundEvade; ++seed) {
        auto state = makeForcedRelocationMatch(seed);
        const auto events = resolver.resolve(state, {
            PlayerAction{1, ActionType::Shoot, CaveId{2}}
        });

        if (!hasEvent(events, GameEventType::BasiliskEvaded)) continue;
        foundEvade = true;

        assert(state.basilisk.alive);
        assert(state.basilisk.trueEncounters == 1);
        assert(hasEvent(events, GameEventType::BasiliskMoved));
        assert(state.basilisk.cave == CaveId{6});
        assert(state.basilisk.lastCave.has_value());
        assert(*state.basilisk.lastCave == CaveId{2});
    }

    assert(foundEvade);
}

void secondEvadeRelocatesBeforeBecomingEnraged() {
    TurnResolver resolver;
    bool foundEvade = false;

    for (MatchSeed seed = 1; seed <= 10000 && !foundEvade; ++seed) {
        auto state = makeForcedRelocationMatch(seed);
        state.basilisk.trueEncounters = 1;
        state.basilisk.behavior = BasiliskBehavior::Restless;

        const auto events = resolver.resolve(state, {
            PlayerAction{1, ActionType::Shoot, CaveId{2}}
        });

        if (!hasEvent(events, GameEventType::BasiliskEvaded)) continue;
        foundEvade = true;

        assert(state.basilisk.alive);
        assert(state.basilisk.trueEncounters == 2);
        assert(hasEvent(events, GameEventType::BasiliskMoved));
        assert(state.basilisk.cave == CaveId{6});
        assert(state.basilisk.lastCave.has_value());
        assert(*state.basilisk.lastCave == CaveId{2});
        assert(state.basilisk.behavior == BasiliskBehavior::Enraged);
        assert(hasEvent(events, GameEventType::BasiliskBehaviorChanged));
    }

    assert(foundEvade);
}

} // namespace

int main() {
    firstEvadeRelocatesAnywhereButPitOccupiedOrCurrentCave();
    secondEvadeRelocatesBeforeBecomingEnraged();

    std::cout << "Basilisk evade relocation tests passed.\n";
    return 0;
}
