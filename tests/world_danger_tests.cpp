#include <cassert>
#include <algorithm>
#include <iostream>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Random.hpp"
#include "basilisk/StatusEffect.hpp"
#include "basilisk/systems/TurnResolver.hpp"
#include "basilisk/systems/WorldDangerSystem.hpp"

using namespace basilisk;

namespace {

bool hasEvent(const std::vector<GameEvent>& events, GameEventType type) {
    for (const auto& event : events) {
        if (event.type == type) return true;
    }
    return false;
}

int eventCount(const std::vector<GameEvent>& events, GameEventType type) {
    return static_cast<int>(std::count_if(events.begin(), events.end(),
        [type](const GameEvent& event) { return event.type == type; }));
}

MatchState makeWorld() {
    MatchState state;
    state.matchSeed = 7001;
    state.mapSeed = 8002;

    for (CaveId cave = 1; cave <= 8; ++cave) state.world.addCave(cave);
    state.world.connect(1, 2);
    state.world.connect(1, 3);
    state.world.connect(1, 4);
    state.world.connect(2, 5);
    state.world.connect(3, 6);
    state.world.connect(4, 7);
    state.world.connect(5, 8);
    state.world.connect(6, 8);
    state.world.connect(7, 8);

    state.basilisk.cave = 8;
    return state;
}

void jackalDamageCapabilityDefaultsToFiveHp() {
    Rules rules;
    assert(rules.jackalDamageEnabled);
    assert(rules.jackalDamageMin == 5);
    assert(rules.jackalDamageMax == 5);
}

void movingIntoPitKillsHunterAndEjectsSigil() {
    auto state = makeWorld();
    state.players = {
        PlayerState{1, 1, 100, 3, true},
        PlayerState{2, 5, 100, 3, true}
    };
    state.pits = {PitState{2, true}};

    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Move, CaveId{2}},
        PlayerAction{2, ActionType::Search}
    });

    assert(!state.players[0].alive);
    assert(state.players[0].health == 0);
    assert(hasEvent(events, GameEventType::PitTriggered));
    assert(hasEvent(events, GameEventType::PlayerKilled));
    assert(hasEvent(events, GameEventType::BodyCreated));
    assert(hasEvent(events, GameEventType::SigilEjected));
    assert(state.bodies.size() == 1);
    assert(state.bodies[0].owner == 1);
    assert(state.bodies[0].cave == 2);
    assert(state.bodies[0].sigilCave.has_value());
    assert(*state.bodies[0].sigilCave != 2);
    assert(state.world.areConnected(2, *state.bodies[0].sigilCave));
    assert(state.result.status == MatchStatus::Active);
}

void ejectedPitSigilCanBeRecoveredBySearch() {
    auto state = makeWorld();
    state.players = {
        PlayerState{1, 1, 100, 3, true},
        PlayerState{2, 5, 100, 3, true}
    };
    state.pits = {PitState{2, true}};

    TurnResolver resolver;
    const auto deathEvents = resolver.resolve(state, {
        PlayerAction{1, ActionType::Move, CaveId{2}},
        PlayerAction{2, ActionType::Search}
    });
    (void)deathEvents;

    assert(state.bodies[0].sigilCave.has_value());
    const CaveId sigilCave = *state.bodies[0].sigilCave;

    state.players[1].cave = sigilCave;
    const auto searchEvents = resolver.resolve(state, {
        PlayerAction{2, ActionType::Search}
    });

    assert(hasEvent(searchEvents, GameEventType::SigilAcquired));
    assert(state.players[1].heldSigilFrom == PlayerId{1});
    assert(state.extraction.active);
    assert(state.extraction.sigilHolder == PlayerId{2});
}

void twoHuntersFallingIntoPitsDraws() {
    auto state = makeWorld();
    state.players = {
        PlayerState{1, 1, 100, 3, true},
        PlayerState{2, 5, 100, 3, true}
    };
    state.pits = {PitState{2, true}, PitState{8, true}};

    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Move, CaveId{2}},
        PlayerAction{2, ActionType::Move, CaveId{8}}
    });

    assert(!state.players[0].alive);
    assert(!state.players[1].alive);
    assert(state.result.status == MatchStatus::Completed);
    assert(state.result.outcome == MatchOutcome::Draw);
    assert(hasEvent(events, GameEventType::MatchDrawn));
}

void stunnedJackalSuppressesMovementAndAttack() {
    auto state = makeWorld();
    state.players = {PlayerState{1, 2, 100, 3, true}};
    JackalState jackal;
    jackal.cave = 1;
    jackal.statuses.push_back(StatusEffect{StatusEffectType::Stunned, 2});
    state.jackals = {jackal};

    RandomGenerator rng{44};
    std::vector<GameEvent> events;
    WorldDangerSystem::resolveJackals(state, rng, events);

    assert(state.jackals[0].cave == 1);
    assert(!hasEvent(events, GameEventType::JackalMoved));
    assert(!hasEvent(events, GameEventType::JackalRobbedArrow));
    assert(!hasEvent(events, GameEventType::JackalScaredPlayer));
    assert(!hasEvent(events, GameEventType::JackalKnockedOutPlayer));
    assert(state.jackals[0].statuses[0].remainingApplications == 1);
}

void jackalAvoidsPitAndBasiliskWhenRoaming() {
    auto state = makeWorld();
    state.basilisk.cave = 2;
    state.pits = {PitState{3, true}};
    JackalState jackal;
    jackal.cave = 1;
    state.jackals = {jackal};

    RandomGenerator rng{99};
    std::vector<GameEvent> events;
    WorldDangerSystem::resolveJackals(state, rng, events);

    assert(state.jackals[0].cave == 4);
    assert(state.jackals[0].lastCave == CaveId{1});
    assert(hasEvent(events, GameEventType::JackalMoved));
}

void allThreeClassicJackalAttacksAreReachable() {
    bool foundRob = false;
    bool foundScare = false;
    bool foundKnockout = false;

    for (std::uint64_t seed = 1; seed <= 10000 &&
         !(foundRob && foundScare && foundKnockout); ++seed) {
        auto state = makeWorld();
        state.players = {PlayerState{1, 1, 100, 3, true}};
        JackalState jackal;
        jackal.cave = 1;
        state.jackals = {jackal};

        RandomGenerator rng{seed};
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, rng, events);

        foundRob = foundRob || hasEvent(events, GameEventType::JackalRobbedArrow);
        foundScare = foundScare || hasEvent(events, GameEventType::JackalScaredPlayer);
        foundKnockout = foundKnockout || hasEvent(events, GameEventType::JackalKnockedOutPlayer);
    }

    assert(foundRob);
    assert(foundScare);
    assert(foundKnockout);
}

void jackalRobberyRemovesExactlyOneArrow() {
    bool verified = false;

    for (std::uint64_t seed = 1; seed <= 10000 && !verified; ++seed) {
        auto state = makeWorld();
        state.players = {PlayerState{1, 1, 100, 3, true}};
        JackalState jackal;
        jackal.cave = 1;
        state.jackals = {jackal};

        RandomGenerator rng{seed};
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, rng, events);

        if (hasEvent(events, GameEventType::JackalRobbedArrow)) {
            assert(state.players[0].arrows == 2);
            verified = true;
        }
    }

    assert(verified);
}

MatchState theftRelocationWorld() {
    MatchState state;
    for (CaveId cave = 1; cave <= 9; ++cave) state.world.addCave(cave);
    state.world.connect(1, 2); // Pit.
    state.world.connect(1, 3); // Basilisk.
    state.world.connect(1, 4); // Living hunter occupied.
    state.world.connect(1, 5); // Effective dead end.
    state.world.connect(1, 6); // Only valid immediate destination.
    state.world.connect(6, 7);
    state.world.connect(7, 8);
    state.world.connect(8, 9);
    state.world.connect(9, 6);
    state.basilisk.cave = 3;
    state.pits = {PitState{2, true}};
    state.players = {
        PlayerState{1, 1, 100, 3, true},
        PlayerState{2, 4, 100, 3, true}
    };
    state.jackals = {JackalState{1}};
    return state;
}

void successfulTheftStartsFleeAndRelocatesSafely() {
    bool verified = false;
    for (std::uint64_t seed = 1; seed <= 10000 && !verified; ++seed) {
        auto state = theftRelocationWorld();
        RandomGenerator rng{seed};
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, rng, events);
        if (!hasEvent(events, GameEventType::JackalRobbedArrow)) continue;

        const JackalState& jackal = state.jackals.front();
        assert(state.players[0].arrows == 2);
        assert(jackal.cave == CaveId{6});
        assert(jackal.lastCave == CaveId{1});
        assert(jackal.fleeOrigin == CaveId{1});
        assert(jackal.protectedHunter == PlayerId{1});
        assert(jackal.fleeRoundsRemaining == 3);
        assert(eventCount(events, GameEventType::JackalMoved) == 1);
        verified = true;
    }
    assert(verified);
}

void ineffectiveTheftCannotStartFlee() {
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        auto state = theftRelocationWorld();
        state.players[0].arrows = 0;
        RandomGenerator rng{seed};
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, rng, events);
        assert(!hasEvent(events, GameEventType::JackalRobbedArrow));
        assert(!state.jackals[0].fleeOrigin.has_value());
        assert(!state.jackals[0].protectedHunter.has_value());
        assert(state.jackals[0].fleeRoundsRemaining == 0);
    }
}

MatchState fleePathWorld() {
    MatchState state;
    for (CaveId cave = 1; cave <= 8; ++cave) state.world.addCave(cave);
    state.world.connect(1, 2);
    state.world.connect(2, 3);
    state.world.connect(2, 4);
    state.world.connect(3, 5);
    state.world.connect(3, 6);
    state.world.connect(4, 7);
    state.world.connect(4, 8);
    state.world.connect(5, 6);
    state.world.connect(7, 8);
    state.basilisk.cave = 6;
    state.players = {PlayerState{1, 5, 100, 3, true}};
    JackalState jackal{2};
    jackal.lastCave = CaveId{1};
    jackal.fleeOrigin = CaveId{1};
    jackal.protectedHunter = PlayerId{1};
    jackal.fleeRoundsRemaining = 3;
    state.jackals = {jackal};
    return state;
}

void fleePrefersDistanceAndAvoidsEquivalentBacktracking() {
    auto state = fleePathWorld();
    RandomGenerator rng{1};
    std::vector<GameEvent> events;
    WorldDangerSystem::resolveJackals(state, rng, events);
    // Cave 3 and Cave 4 are equally farther from origin. Cave 1 is the
    // immediate backtrack and is never selected over them.
    assert(state.jackals[0].cave == CaveId{3} || state.jackals[0].cave == CaveId{4});
    assert(state.jackals[0].cave != CaveId{1});
    assert(state.jackals[0].fleeRoundsRemaining == 2);
}

void fleeFallsBackToLeastDecreaseAndExpiresAfterThreeOpportunities() {
    MatchState state;
    for (CaveId cave = 1; cave <= 5; ++cave) state.world.addCave(cave);
    state.world.connect(1, 2);
    state.world.connect(2, 3);
    state.world.connect(2, 4);
    state.world.connect(3, 5);
    state.world.connect(4, 5);
    state.basilisk.cave = 99;
    state.players = {PlayerState{1, 1, 100, 3, true}};
    JackalState jackal{5};
    jackal.lastCave = CaveId{3};
    jackal.fleeOrigin = CaveId{1};
    jackal.protectedHunter = PlayerId{1};
    jackal.fleeRoundsRemaining = 3;
    state.jackals = {jackal};

    RandomGenerator rng{77};
    std::vector<GameEvent> events;
    WorldDangerSystem::resolveJackals(state, rng, events);
    // Both choices decrease distance equally; avoid the previous Cave 3.
    assert(state.jackals[0].cave == CaveId{4});
    assert(state.jackals[0].fleeRoundsRemaining == 2);
    events.clear();
    WorldDangerSystem::resolveJackals(state, rng, events);
    assert(state.jackals[0].fleeRoundsRemaining == 1);
    events.clear();
    WorldDangerSystem::resolveJackals(state, rng, events);
    assert(state.jackals[0].fleeRoundsRemaining == 0);
    assert(!state.jackals[0].fleeOrigin.has_value());
    assert(!state.jackals[0].protectedHunter.has_value());
    assert(!state.jackals[0].lastCave.has_value());
}

void blockedFleeStillConsumesExactlyThreeOpportunities() {
    MatchState state;
    state.world.connect(1, 2);
    state.world.connect(1, 3);
    state.world.addCave(4);
    state.basilisk.cave = 99;
    state.players = {PlayerState{1, 4, 100, 3, true}};
    JackalState jackal{1};
    jackal.fleeOrigin = CaveId{1};
    jackal.protectedHunter = PlayerId{1};
    jackal.fleeRoundsRemaining = 3;
    state.jackals = {jackal};
    RandomGenerator rng{91};

    for (int expected = 2; expected >= 0; --expected) {
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, rng, events);
        assert(state.jackals[0].cave == CaveId{1});
        assert(!hasEvent(events, GameEventType::JackalMoved));
        assert(state.jackals[0].fleeRoundsRemaining == expected);
    }
    assert(!state.jackals[0].fleeOrigin.has_value());
    assert(!state.jackals[0].protectedHunter.has_value());
}

void protectedHunterCannotBeRobbedButOtherOutcomesRemain() {
    bool foundScare = false;
    bool foundKnockout = false;
    for (std::uint64_t seed = 1; seed <= 10000 && !(foundScare && foundKnockout); ++seed) {
        auto state = makeWorld();
        state.players = {PlayerState{1, 1, 100, 3, true}};
        JackalState jackal{1};
        jackal.fleeOrigin = CaveId{2};
        jackal.protectedHunter = PlayerId{1};
        jackal.fleeRoundsRemaining = 3;
        state.jackals = {jackal};
        RandomGenerator rng{seed};
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, rng, events);
        assert(!hasEvent(events, GameEventType::JackalRobbedArrow));
        foundScare = foundScare || hasEvent(events, GameEventType::JackalScaredPlayer);
        foundKnockout = foundKnockout || hasEvent(events, GameEventType::JackalKnockedOutPlayer);
    }
    assert(foundScare && foundKnockout);
}

void theftFromDifferentHunterRefreshesProtectionAndOrigin() {
    bool verified = false;
    for (std::uint64_t seed = 1; seed <= 10000 && !verified; ++seed) {
        auto state = theftRelocationWorld();
        state.players[0].cave = CaveId{8};
        state.players[1].cave = CaveId{1};
        state.jackals[0].fleeOrigin = CaveId{9};
        state.jackals[0].protectedHunter = PlayerId{1};
        state.jackals[0].fleeRoundsRemaining = 1;
        RandomGenerator rng{seed};
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, rng, events);
        if (!hasEvent(events, GameEventType::JackalRobbedArrow)) continue;
        assert(state.players[1].arrows == 2);
        assert(state.jackals[0].fleeOrigin == CaveId{1});
        assert(state.jackals[0].protectedHunter == PlayerId{2});
        assert(state.jackals[0].fleeRoundsRemaining == 3);
        verified = true;
    }
    assert(verified);
}

void theftIsEligibleAgainAfterFleeExpiresAndIsDeterministic() {
    auto first = makeWorld();
    first.players = {PlayerState{1, 1, 100, 3, true}};
    first.jackals = {JackalState{1}};
    auto second = first;
    RandomGenerator firstRng{11};
    RandomGenerator secondRng{11};
    std::vector<GameEvent> firstEvents;
    std::vector<GameEvent> secondEvents;
    WorldDangerSystem::resolveJackals(first, firstRng, firstEvents);
    WorldDangerSystem::resolveJackals(second, secondRng, secondEvents);
    assert(first.players[0].arrows == second.players[0].arrows);
    assert(first.jackals[0].cave == second.jackals[0].cave);
    assert(first.jackals[0].fleeOrigin == second.jackals[0].fleeOrigin);
    assert(firstEvents.size() == secondEvents.size());
    for (std::size_t i = 0; i < firstEvents.size(); ++i) {
        assert(firstEvents[i].type == secondEvents[i].type);
        assert(firstEvents[i].actor == secondEvents[i].actor);
        assert(firstEvents[i].targetPlayer == secondEvents[i].targetPlayer);
        assert(firstEvents[i].cave == secondEvents[i].cave);
        assert(firstEvents[i].amount == secondEvents[i].amount);
    }

    bool theftFound = false;
    for (std::uint64_t seed = 1; seed <= 10000 && !theftFound; ++seed) {
        auto state = makeWorld();
        state.players = {PlayerState{1, 1, 100, 3, true}};
        state.jackals = {JackalState{1}}; // Expired/cleared Flee state.
        RandomGenerator rng{seed};
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, rng, events);
        theftFound = hasEvent(events, GameEventType::JackalRobbedArrow);
    }
    assert(theftFound);
}

void jackalKnockoutDealsExactlyFiveHp() {
    bool verified = false;

    for (std::uint64_t seed = 1; seed <= 10000 && !verified; ++seed) {
        auto state = makeWorld();
        state.players = {PlayerState{1, 1, 100, 3, true}};
        JackalState jackal;
        jackal.cave = 1;
        state.jackals = {jackal};

        RandomGenerator rng{seed};
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, rng, events);

        if (hasEvent(events, GameEventType::JackalKnockedOutPlayer)) {
            assert(state.players[0].health == 95);
            assert(state.players[0].alive);

            bool sawDamage = false;
            for (const auto& event : events) {
                if (event.type == GameEventType::PlayerDamaged &&
                    event.targetPlayer == PlayerId{1} && event.amount == 5) {
                    sawDamage = true;
                    break;
                }
            }
            assert(sawDamage);
            verified = true;
        }
    }

    assert(verified);
}

} // namespace

int main() {
    jackalDamageCapabilityDefaultsToFiveHp();
    movingIntoPitKillsHunterAndEjectsSigil();
    ejectedPitSigilCanBeRecoveredBySearch();
    twoHuntersFallingIntoPitsDraws();
    stunnedJackalSuppressesMovementAndAttack();
    jackalAvoidsPitAndBasiliskWhenRoaming();
    allThreeClassicJackalAttacksAreReachable();
    jackalRobberyRemovesExactlyOneArrow();
    successfulTheftStartsFleeAndRelocatesSafely();
    ineffectiveTheftCannotStartFlee();
    fleePrefersDistanceAndAvoidsEquivalentBacktracking();
    fleeFallsBackToLeastDecreaseAndExpiresAfterThreeOpportunities();
    blockedFleeStillConsumesExactlyThreeOpportunities();
    protectedHunterCannotBeRobbedButOtherOutcomesRemain();
    theftFromDifferentHunterRefreshesProtectionAndOrigin();
    theftIsEligibleAgainAfterFleeExpiresAndIsDeterministic();
    jackalKnockoutDealsExactlyFiveHp();

    std::cout << "Basilisk world danger tests passed.\n";
    return 0;
}
