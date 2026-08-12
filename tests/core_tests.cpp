#include <cassert>
#include <iostream>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Random.hpp"
#include "basilisk/StatusEffect.hpp"
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

bool hasEvent(const std::vector<GameEvent>& events, GameEventType type) {
    for (const auto& event : events) {
        if (event.type == type) {
            return true;
        }
    }
    return false;
}

int stunRemaining(const JackalState& jackal) {
    for (const auto& status : jackal.statuses) {
        if (status.type == StatusEffectType::Stunned) {
            return status.remainingApplications;
        }
    }
    return 0;
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

void randomGeneratorIsDeterministic() {
    RandomGenerator first{987654321};
    RandomGenerator second{987654321};

    for (int i = 0; i < 100; ++i) {
        assert(first.range(1, 100000) == second.range(1, 100000));
    }

    for (int i = 0; i < 100; ++i) {
        assert(first.chance(1, 4) == second.chance(1, 4));
    }
}

void shootingJackalStunsForThreeNpcPhases() {
    auto state = makeTestMatch();
    state.players = {
        PlayerState{1, 1, 100, 3, true}
    };
    state.jackals = {
        JackalState{2, {}}
    };

    TurnResolver resolver;

    const auto shotEvents = resolver.resolve(state, {
        PlayerAction{1, ActionType::Shoot, CaveId{2}}
    });

    assert(hasEvent(shotEvents, GameEventType::ArrowHitJackal));
    assert(hasEvent(shotEvents, GameEventType::JackalStunned));
    assert(state.players[0].arrows == 2);

    // The shot round itself suppresses NPC phase #1, leaving two more.
    assert(stunRemaining(state.jackals[0]) == 2);

    resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    });
    assert(stunRemaining(state.jackals[0]) == 1);

    resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    });
    assert(stunRemaining(state.jackals[0]) == 0);
}

void shootingBasiliskRecordsOutcomeNeutralEvent() {
    auto state = makeTestMatch();
    state.players = {
        PlayerState{1, 1, 100, 3, true}
    };
    state.basilisk = BasiliskState{2, true};

    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Shoot, CaveId{2}}
    });

    assert(hasEvent(events, GameEventType::ArrowReachedBasilisk));
    assert(state.basilisk.alive);
    assert(state.players[0].arrows == 2);
}

} // namespace

int main() {
    movementResolvesBeforeShooting();
    lethalShotsResolveSimultaneously();
    randomGeneratorIsDeterministic();
    shootingJackalStunsForThreeNpcPhases();
    shootingBasiliskRecordsOutcomeNeutralEvent();

    std::cout << "BasiliskCore tests passed.\n";
    return 0;
}
