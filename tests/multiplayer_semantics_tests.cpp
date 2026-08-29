#include <algorithm>
#include <cassert>
#include <set>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Observation.hpp"
#include "basilisk/Random.hpp"
#include "basilisk/systems/MatchCoordinator.hpp"
#include "basilisk/systems/ObservationSystem.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/systems/TurnResolver.hpp"
#include "basilisk/systems/WorldDangerSystem.hpp"

using namespace basilisk;

namespace {

MatchState arena() {
    MatchState state;
    state.matchSeed = 424242;
    for (CaveId cave = 1; cave <= 9; ++cave) state.world.addCave(cave);
    state.world.connect(1, 2); state.world.connect(2, 3);
    state.world.connect(2, 4); state.world.connect(3, 5);
    state.world.connect(4, 6); state.world.connect(5, 7);
    state.world.connect(6, 7); state.world.connect(7, 8);
    state.world.connect(8, 9);
    state.basilisk.cave = 9;
    return state;
}

int eventCount(const std::vector<GameEvent>& events, GameEventType type) {
    return static_cast<int>(std::count_if(events.begin(), events.end(),
        [type](const GameEvent& event) { return event.type == type; }));
}

const GameEvent* eventOfType(
    const std::vector<GameEvent>& events, GameEventType type) {
    const auto found = std::find_if(events.begin(), events.end(),
        [type](const GameEvent& event) { return event.type == type; });
    return found == events.end() ? nullptr : &*found;
}

bool hasObservation(
    const std::vector<PlayerObservation>& observations, ObservationType type) {
    return std::any_of(observations.begin(), observations.end(),
        [type](const PlayerObservation& observation) {
            return observation.type == type;
        });
}

void simultaneousPvpKeepsFirstLethalAttribution() {
    auto state = arena();
    state.players = {
        PlayerState{10, 1, 100, 3, true},
        PlayerState{20, 2, 50, 3, true},
        PlayerState{30, 3, 100, 3, true},
        PlayerState{40, 4, 100, 3, true},
    };
    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{10, ActionType::Shoot, CaveId{2}},
        PlayerAction{20, ActionType::Search},
        PlayerAction{30, ActionType::Shoot, CaveId{2}},
        PlayerAction{40, ActionType::Search},
    });
    assert(!state.players[1].alive);
    assert(eventCount(events, GameEventType::ArrowHitPlayer) == 2);
    assert(eventCount(events, GameEventType::PlayerDamaged) == 2);
    assert(eventCount(events, GameEventType::PlayerKilled) == 1);
    const GameEvent* killed = eventOfType(events, GameEventType::PlayerKilled);
    assert(killed != nullptr && killed->targetPlayer == PlayerId{20});
    assert(killed->actor == PlayerId{10});

    const auto killer = ObservationSystem::buildForPlayer(state, 10, events);
    const auto otherShooter = ObservationSystem::buildForPlayer(state, 30, events);
    const auto observer = ObservationSystem::buildForPlayer(state, 40, events);
    assert(hasObservation(killer, ObservationType::YouKilledRival));
    assert(!hasObservation(killer, ObservationType::RivalDied));
    assert(hasObservation(otherShooter, ObservationType::YouHitRival));
    assert(hasObservation(otherShooter, ObservationType::RivalDied));
    assert(hasObservation(observer, ObservationType::RivalDied));
    for (const auto& observation : observer) {
        if (observation.type != ObservationType::RivalDied) continue;
        assert(!observation.cave.has_value());
        assert(!observation.otherPlayer.has_value());
    }
}

void rivalNearbyIsGenericAndEmittedOnce() {
    auto state = arena();
    state.players = {
        PlayerState{10, 2, 100, 3, true},
        PlayerState{20, 1, 100, 3, true},
        PlayerState{30, 3, 100, 3, true},
        PlayerState{40, 8, 100, 3, true},
    };
    const auto observations = ObservationSystem::buildForPlayer(state, 10, {});
    const auto nearbyCount = std::count_if(observations.begin(), observations.end(),
        [](const PlayerObservation& observation) {
            return observation.type == ObservationType::RivalNearby;
        });
    assert(nearbyCount == 1);
    const auto found = std::find_if(observations.begin(), observations.end(),
        [](const PlayerObservation& observation) {
            return observation.type == ObservationType::RivalNearby;
        });
    assert(found != observations.end());
    assert(!found->cave.has_value() && !found->otherPlayer.has_value());
}

void competingSigilRecoveryHasOneDeterministicCarrier() {
    auto state = arena();
    state.basilisk.alive = false;
    state.players = {
        PlayerState{10, 1, 100, 3, true},
        PlayerState{20, 2, 0, 3, false},
        PlayerState{30, 3, 0, 3, false},
        PlayerState{40, 4, 100, 3, true},
    };
    state.bodies = {
        BodyState{20, 1, true, CaveId{1}},
        BodyState{30, 4, true, CaveId{4}},
    };
    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{40, ActionType::Search},
        PlayerAction{10, ActionType::Search},
    });
    assert(eventCount(events, GameEventType::SigilAcquired) == 1);
    assert(state.players[0].heldSigilFrom == PlayerId{20});
    assert(!state.players[3].heldSigilFrom.has_value());
    assert(state.extraction.sigilHolder == PlayerId{10});
    assert(!state.bodies[0].sigilAvailable && state.bodies[1].sigilAvailable);
    const auto waitingHunter = SnapshotSystem::buildForPlayer(state, 40, events);
    assert(!waitingHunter.recoverableRivalSigilAvailable);

    state.players[0].cave = *state.extraction.cave;
    const auto escape = resolver.resolve(state, {
        PlayerAction{10, ActionType::Contextual, std::nullopt, std::nullopt,
            ContextualActionType::Escape},
        PlayerAction{40, ActionType::Search},
    });
    assert(state.result.status == MatchStatus::Completed);
    assert(state.result.outcome == MatchOutcome::EscapedWithSigil);
    assert(state.result.winner == PlayerId{10});
    assert(eventCount(escape, GameEventType::PlayerEscaped) == 1);
}

void carrierDeathRestoresOnlyTheCarriedSigil() {
    auto state = arena();
    state.players = {
        PlayerState{10, 2, 50, 3, true},
        PlayerState{20, 1, 100, 3, true},
        PlayerState{30, 5, 0, 3, false},
        PlayerState{40, 4, 100, 3, true},
    };
    state.players[0].heldSigilFrom = PlayerId{30};
    state.bodies = {BodyState{30, 5, false, CaveId{5}}};
    state.extraction.active = true;
    state.extraction.sigilHolder = PlayerId{10};
    state.extraction.cave = CaveId{8};
    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{10, ActionType::Search},
        PlayerAction{20, ActionType::Shoot, CaveId{2}},
        PlayerAction{40, ActionType::Search},
    });
    assert(!state.players[0].alive);
    assert(!state.players[0].heldSigilFrom.has_value());
    assert(!state.extraction.active && !state.extraction.sigilHolder.has_value());
    const auto carried = std::find_if(state.bodies.begin(), state.bodies.end(),
        [](const BodyState& body) { return body.owner == PlayerId{30}; });
    assert(carried != state.bodies.end() && carried->sigilAvailable);
    assert(carried->sigilCave.has_value());
    assert(eventCount(events, GameEventType::PlayerKilled) == 1);
}

void stationaryClashSearchRecoversSigilExactlyOnce() {
    auto state = arena();
    state.players = {
        PlayerState{10, 1, 100, 3, true},
        PlayerState{20, 2, 100, 3, true},
        PlayerState{30, 4, 100, 3, true},
        PlayerState{40, 5, 0, 3, false},
    };
    state.bodies = {BodyState{40, 2, true, CaveId{2}}};
    MatchCoordinator coordinator{state};
    assert(coordinator.submitAction(PlayerAction{10, ActionType::Move, CaveId{2}}));
    assert(coordinator.lockAction(10));
    assert(coordinator.submitAction(PlayerAction{20, ActionType::Search}));
    assert(coordinator.lockAction(20));
    assert(coordinator.submitAction(PlayerAction{30, ActionType::Search}));
    assert(coordinator.lockAction(30));
    assert(coordinator.activeClash() != nullptr);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::SigilAcquired) == 1);
    assert(state.players[1].heldSigilFrom == PlayerId{40});
    const ActiveClash clash = *coordinator.activeClash();
    assert(coordinator.submitClashResponse(20, clash.id, clash.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == 2);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::SigilAcquired) == 1);
}

void jackalDisplacementNeverDuplicatesLivingOccupancy() {
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        auto state = arena();
        state.players = {
            PlayerState{10, 1, 100, 3, true},
            PlayerState{20, 2, 100, 3, true},
            PlayerState{30, 3, 100, 3, true},
            PlayerState{40, 4, 100, 3, true},
        };
        state.jackals = {JackalState{1}};
        RandomGenerator random{seed};
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, random, events);
        std::set<CaveId> occupied;
        for (const PlayerState& player : state.players) {
            if (player.alive) assert(occupied.insert(player.cave).second);
        }
    }
}

void jackalFleeProtectionRemainsVictimSpecificWithSeveralHunters() {
    bool otherHunterWasRobbed = false;
    for (std::uint64_t seed = 1; seed <= 10000 && !otherHunterWasRobbed; ++seed) {
        auto state = arena();
        state.players = {
            PlayerState{10, 8, 100, 3, true},
            PlayerState{20, 1, 100, 3, true},
            PlayerState{30, 3, 100, 3, true},
            PlayerState{40, 4, 100, 3, true},
        };
        JackalState jackal{1};
        jackal.fleeOrigin = CaveId{7};
        jackal.protectedHunter = PlayerId{10};
        jackal.fleeRoundsRemaining = 2;
        state.jackals = {jackal};
        RandomGenerator random{seed};
        std::vector<GameEvent> events;
        WorldDangerSystem::resolveJackals(state, random, events);
        otherHunterWasRobbed = std::any_of(events.begin(), events.end(),
            [](const GameEvent& event) {
                return event.type == GameEventType::JackalRobbedArrow &&
                    event.targetPlayer == PlayerId{20};
            });
        if (otherHunterWasRobbed) {
            assert(state.players[0].arrows == 3);
            assert(state.players[1].arrows == 2);
            assert(state.jackals.front().protectedHunter == PlayerId{20});
        }
    }
    assert(otherHunterWasRobbed);
}

void multiPartyClashCanFlowIntoHazardDeathOnce() {
    auto state = arena();
    state.players = {
        PlayerState{10, 1, 100, 3, true},
        PlayerState{20, 3, 100, 3, true},
        PlayerState{30, 4, 100, 3, true},
    };
    state.pits = {PitState{2, true}};
    MatchCoordinator coordinator{state};
    for (const PlayerId player : {PlayerId{10}, PlayerId{20}, PlayerId{30}}) {
        assert(coordinator.submitAction(PlayerAction{player, ActionType::Move, CaveId{2}}));
        assert(coordinator.lockAction(player));
    }
    const ActiveClash clash = *coordinator.activeClash();
    assert(clash.participants.size() == 3);
    assert(coordinator.submitClashResponse(10, clash.id, clash.challengeWord) ==
        ClashSubmissionResult::Resolved);
    assert(state.round == 2 && coordinator.activeClash() == nullptr);
    assert(!state.players[0].alive);
    assert(state.players[1].alive && state.players[2].alive);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::PitTriggered) == 1);
    assert(eventCount(coordinator.authoritativeEvents(), GameEventType::PlayerKilled) == 1);
    assert(state.result.status == MatchStatus::Active);
    std::set<CaveId> occupied;
    for (const PlayerState& player : state.players) {
        if (player.alive) assert(occupied.insert(player.cave).second);
    }
}

void severalHazardDeathsDrawOnlyWhenNobodyLives() {
    auto state = arena();
    state.players = {
        PlayerState{10, 1, 100, 3, true},
        PlayerState{20, 3, 100, 3, true},
        PlayerState{30, 4, 100, 3, true},
        PlayerState{40, 8, 100, 3, true},
    };
    state.pits = {PitState{1, true}, PitState{3, true}, PitState{4, true}};
    TurnResolver resolver;
    auto events = resolver.resolve(state, {
        PlayerAction{10, ActionType::Search}, PlayerAction{20, ActionType::Search},
        PlayerAction{30, ActionType::Search}, PlayerAction{40, ActionType::Search},
    });
    assert(!state.players[0].alive && !state.players[1].alive &&
        !state.players[2].alive && state.players[3].alive);
    assert(state.result.status == MatchStatus::Active);
    const RoundNumber round = state.round;
    events = resolver.resolve(state, {PlayerAction{40, ActionType::Search}});
    assert(state.round == round + 1 && state.result.status == MatchStatus::Active);

    state.pits.push_back(PitState{8, true});
    events = resolver.resolve(state, {PlayerAction{40, ActionType::Search}});
    assert(!state.players[3].alive);
    assert(state.result.status == MatchStatus::Completed);
    assert(state.result.outcome == MatchOutcome::Draw);
    assert(eventCount(events, GameEventType::MatchDrawn) == 1);
}

} // namespace

int main() {
    simultaneousPvpKeepsFirstLethalAttribution();
    rivalNearbyIsGenericAndEmittedOnce();
    competingSigilRecoveryHasOneDeterministicCarrier();
    carrierDeathRestoresOnlyTheCarriedSigil();
    stationaryClashSearchRecoversSigilExactlyOnce();
    jackalDisplacementNeverDuplicatesLivingOccupancy();
    jackalFleeProtectionRemainsVictimSpecificWithSeveralHunters();
    multiPartyClashCanFlowIntoHazardDeathOnce();
    severalHazardDeathsDrawOnlyWhenNobodyLives();
}
