#include <cassert>
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
    jackalKnockoutDealsExactlyFiveHp();

    std::cout << "Basilisk world danger tests passed.\n";
    return 0;
}
