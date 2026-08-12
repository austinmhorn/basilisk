#include <cassert>
#include <iostream>
#include <optional>
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
    const auto events = resolver.resolve(state, actions);
    (void)events;

    assert(state.players[1].cave == 2);
    assert(state.players[1].health == 50);
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
    const auto events = resolver.resolve(state, actions);
    (void)events;

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

    const auto roundTwoEvents = resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    });
    (void)roundTwoEvents;
    assert(stunRemaining(state.jackals[0]) == 1);

    const auto roundThreeEvents = resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    });
    (void)roundThreeEvents;
    assert(stunRemaining(state.jackals[0]) == 0);
}

void wrongCaveDoesNotCountAsTrueBasiliskEncounter() {
    auto state = makeTestMatch();
    state.players = {
        PlayerState{1, 1, 100, 3, true}
    };
    state.basilisk.cave = 2;

    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Shoot, CaveId{4}}
    });

    assert(hasEvent(events, GameEventType::ArrowMissed));
    assert(state.basilisk.trueEncounters == 0);
    assert(state.basilisk.alive);
}

void firstTrueEncounterCanEvadeAndMutate() {
    TurnResolver resolver;
    bool foundEvade = false;

    // Search deterministic seeds until we exercise the 25% evade path.
    for (MatchSeed seed = 1; seed <= 10000 && !foundEvade; ++seed) {
        auto state = makeTestMatch();
        state.matchSeed = seed;
        state.players = {
            PlayerState{1, 1, 100, 3, true}
        };
        state.basilisk.cave = 2;

        const auto events = resolver.resolve(state, {
            PlayerAction{1, ActionType::Shoot, CaveId{2}}
        });

        if (!hasEvent(events, GameEventType::BasiliskEvaded)) {
            continue;
        }

        foundEvade = true;
        assert(state.basilisk.alive);
        assert(state.basilisk.trueEncounters == 1);

        if (hasEvent(events, GameEventType::BasiliskBehaviorChanged)) {
            assert(state.basilisk.behavior == BasiliskBehavior::Restless ||
                   state.basilisk.behavior == BasiliskBehavior::Lurker ||
                   state.basilisk.behavior == BasiliskBehavior::Skittish ||
                   state.basilisk.behavior == BasiliskBehavior::Territorial);
        } else {
            assert(state.basilisk.behavior == BasiliskBehavior::Normal);
        }
    }

    assert(foundEvade);
}

void secondEvadeAlwaysBecomesEnraged() {
    TurnResolver resolver;
    bool foundSecondEvade = false;

    for (MatchSeed seed = 1; seed <= 10000 && !foundSecondEvade; ++seed) {
        auto state = makeTestMatch();
        state.matchSeed = seed;
        state.players = {
            PlayerState{1, 1, 100, 3, true}
        };
        state.basilisk.cave = 2;
        state.basilisk.trueEncounters = 1;
        state.basilisk.behavior = BasiliskBehavior::Restless;

        const auto events = resolver.resolve(state, {
            PlayerAction{1, ActionType::Shoot, CaveId{2}}
        });

        if (!hasEvent(events, GameEventType::BasiliskEvaded)) {
            continue;
        }

        foundSecondEvade = true;
        assert(state.basilisk.alive);
        assert(state.basilisk.trueEncounters == 2);
        assert(state.basilisk.behavior == BasiliskBehavior::Enraged);
        assert(hasEvent(events, GameEventType::BasiliskBehaviorChanged));
    }

    assert(foundSecondEvade);
}

void thirdTrueEncounterIsGuaranteedKill() {
    auto state = makeTestMatch();
    state.players = {
        PlayerState{1, 1, 100, 3, true}
    };
    state.basilisk.cave = 2;
    state.basilisk.trueEncounters = 2;
    state.basilisk.behavior = BasiliskBehavior::Enraged;

    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Shoot, CaveId{2}}
    });

    assert(state.basilisk.trueEncounters == 3);
    assert(!state.basilisk.alive);
    assert(hasEvent(events, GameEventType::BasiliskKilled));
}

void skittishBasiliskMovesWhenAdjacentCaveIsSearched() {
    auto state = makeTestMatch();
    state.players = {
        PlayerState{1, 1, 100, 3, true}
    };
    state.basilisk.cave = 2;
    state.basilisk.behavior = BasiliskBehavior::Skittish;

    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    });

    assert(hasEvent(events, GameEventType::BasiliskMoved));
    assert(state.basilisk.cave != 2);
    assert(state.basilisk.lastCave.has_value());
    assert(*state.basilisk.lastCave == 2);
}

void enragedBasiliskMovesEveryTwoRounds() {
    auto state = makeTestMatch();
    state.players = {
        PlayerState{1, 4, 100, 3, true}
    };
    state.basilisk.cave = 2;
    state.basilisk.behavior = BasiliskBehavior::Enraged;

    TurnResolver resolver;

    const auto firstRound = resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    });
    assert(!hasEvent(firstRound, GameEventType::BasiliskMoved));
    assert(state.basilisk.cave == 2);

    const auto secondRound = resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    });
    assert(hasEvent(secondRound, GameEventType::BasiliskMoved));
    assert(state.basilisk.cave != 2);
    assert(state.basilisk.lastCave.has_value());
    assert(*state.basilisk.lastCave == 2);
}

void identicalSeedAndActionsProduceIdenticalBasiliskOutcome() {
    auto first = makeTestMatch();
    auto second = makeTestMatch();

    first.matchSeed = 456789;
    second.matchSeed = 456789;

    first.players = {PlayerState{1, 1, 100, 3, true}};
    second.players = {PlayerState{1, 1, 100, 3, true}};

    first.basilisk.cave = 2;
    second.basilisk.cave = 2;

    TurnResolver resolver;
    const std::vector<PlayerAction> actions{
        PlayerAction{1, ActionType::Shoot, CaveId{2}}
    };

    const auto firstEvents = resolver.resolve(first, actions);
    const auto secondEvents = resolver.resolve(second, actions);

    assert(first.basilisk.alive == second.basilisk.alive);
    assert(first.basilisk.trueEncounters == second.basilisk.trueEncounters);
    assert(first.basilisk.behavior == second.basilisk.behavior);
    assert(firstEvents.size() == secondEvents.size());
}

} // namespace

int main() {
    movementResolvesBeforeShooting();
    lethalShotsResolveSimultaneously();
    randomGeneratorIsDeterministic();
    shootingJackalStunsForThreeNpcPhases();
    wrongCaveDoesNotCountAsTrueBasiliskEncounter();
    firstTrueEncounterCanEvadeAndMutate();
    secondEvadeAlwaysBecomesEnraged();
    thirdTrueEncounterIsGuaranteedKill();
    skittishBasiliskMovesWhenAdjacentCaveIsSearched();
    enragedBasiliskMovesEveryTwoRounds();
    identicalSeedAndActionsProduceIdenticalBasiliskOutcome();

    std::cout << "BasiliskCore tests passed.\n";
    return 0;
}
