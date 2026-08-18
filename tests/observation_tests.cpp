#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <vector>

#include "basilisk/MatchState.hpp"
#include "basilisk/Observation.hpp"
#include "basilisk/systems/ObservationSystem.hpp"

using namespace basilisk;

namespace {

MatchState makeObservationWorld() {
    MatchState state;
    for (CaveId cave = 1; cave <= 7; ++cave) state.world.addCave(cave);

    state.world.connect(1, 2);
    state.world.connect(2, 3);
    state.world.connect(3, 4);
    state.world.connect(2, 5);
    state.world.connect(5, 6);
    state.world.connect(6, 7);

    state.players = {
        PlayerState{1, 1, 100, 3, true},
        PlayerState{2, 2, 100, 3, true}
    };

    state.basilisk.cave = 3;
    state.pits = {PitState{2, true}};

    JackalState jackal;
    jackal.cave = 2;
    state.jackals = {jackal};

    return state;
}

bool hasType(const std::vector<PlayerObservation>& observations, ObservationType type) {
    return std::any_of(observations.begin(), observations.end(),
        [type](const PlayerObservation& observation) { return observation.type == type; });
}

const PlayerObservation* findType(
    const std::vector<PlayerObservation>& observations,
    ObservationType type) {

    const auto it = std::find_if(observations.begin(), observations.end(),
        [type](const PlayerObservation& observation) { return observation.type == type; });
    return it == observations.end() ? nullptr : &*it;
}

void nearbyThreatsProduceCluesWithoutLocations() {
    auto state = makeObservationWorld();
    const auto observations = ObservationSystem::buildForPlayer(state, 1, {});

    assert(hasType(observations, ObservationType::RivalNearby));
    assert(hasType(observations, ObservationType::PitNearby));
    assert(hasType(observations, ObservationType::JackalNearby));

    const auto* rival = findType(observations, ObservationType::RivalNearby);
    const auto* pit = findType(observations, ObservationType::PitNearby);
    const auto* jackal = findType(observations, ObservationType::JackalNearby);

    assert(rival != nullptr && !rival->cave.has_value());
    assert(pit != nullptr && !pit->cave.has_value());
    assert(jackal != nullptr && !jackal->cave.has_value());
}

void basiliskClueChangesForLurker() {
    auto state = makeObservationWorld();
    state.players[0].cave = 2;

    state.basilisk.behavior = BasiliskBehavior::Normal;
    auto observations = ObservationSystem::buildForPlayer(state, 1, {});
    assert(hasType(observations, ObservationType::BasiliskNearby));
    assert(!hasType(observations, ObservationType::BasiliskNearbySubtle));

    state.basilisk.behavior = BasiliskBehavior::Lurker;
    observations = ObservationSystem::buildForPlayer(state, 1, {});
    assert(!hasType(observations, ObservationType::BasiliskNearby));
    assert(hasType(observations, ObservationType::BasiliskNearbySubtle));
}

void restlessMovementCanBeHeardTwoHopsAway() {
    auto state = makeObservationWorld();
    state.players[0].cave = 1;
    state.players[1].cave = 7;
    state.basilisk.cave = 3;
    state.basilisk.behavior = BasiliskBehavior::Restless;

    const std::vector<GameEvent> moved{
        GameEvent{GameEventType::BasiliskMoved, std::nullopt, std::nullopt,
                  CaveId{3}, 4, BasiliskBehavior::Restless}
    };

    const auto observations = ObservationSystem::buildForPlayer(state, 1, moved);
    assert(hasType(observations, ObservationType::RestlessBasiliskNoise));
    assert(!hasType(observations, ObservationType::BasiliskNearby));
}

void enragedExposesOnlyLastKnownCave() {
    auto state = makeObservationWorld();
    state.players[0].cave = 1;
    state.players[1].cave = 7;
    state.basilisk.cave = 4;
    state.basilisk.behavior = BasiliskBehavior::Enraged;
    state.basilisk.lastCave = CaveId{3};

    const auto observations = ObservationSystem::buildForPlayer(state, 1, {});
    const auto* marker = findType(observations, ObservationType::EnragedLastKnownCave);

    assert(marker != nullptr);
    assert(marker->cave == CaveId{3});
    assert(marker->cave != state.basilisk.cave);
}

void privateLootDoesNotLeakToOtherHunter() {
    auto state = makeObservationWorld();
    const std::vector<GameEvent> events{
        GameEvent{GameEventType::ItemFound, PlayerId{1}, std::nullopt,
                  CaveId{1}, 0, std::nullopt, ItemType::HealingDraught},
        GameEvent{GameEventType::ExoticCallingCardFound, PlayerId{1}, std::nullopt, CaveId{1}}
    };

    const auto playerA = ObservationSystem::buildForPlayer(state, 1, events);
    const auto playerB = ObservationSystem::buildForPlayer(state, 2, events);

    assert(hasType(playerA, ObservationType::ItemFound));
    assert(hasType(playerA, ObservationType::ExoticCallingCardFound));
    assert(!hasType(playerB, ObservationType::ItemFound));
    assert(!hasType(playerB, ObservationType::ExoticCallingCardFound));
}

void exactRivalMovementEventIsNotForwarded() {
    auto state = makeObservationWorld();
    const std::vector<GameEvent> events{
        GameEvent{GameEventType::PlayerMoved, PlayerId{2}, std::nullopt, CaveId{2}}
    };

    const auto observations = ObservationSystem::buildForPlayer(state, 1, events);

    const auto* rival = findType(observations, ObservationType::RivalNearby);
    assert(rival != nullptr);
    assert(!rival->cave.has_value());
    assert(!rival->otherPlayer.has_value());
}

void ownBasiliskShotGetsOutcomeFeedback() {
    auto state = makeObservationWorld();
    state.players[0].cave = 2;
    state.basilisk.cave = 3;
    state.basilisk.alive = true;

    const std::vector<GameEvent> events{
        GameEvent{GameEventType::ArrowReachedBasilisk, PlayerId{1}, std::nullopt,
                  CaveId{3}, 1, BasiliskBehavior::Normal},
        GameEvent{GameEventType::BasiliskEvaded, std::nullopt, std::nullopt,
                  CaveId{3}, 1, BasiliskBehavior::Normal}
    };

    const auto playerA = ObservationSystem::buildForPlayer(state, 1, events);
    const auto playerB = ObservationSystem::buildForPlayer(state, 2, events);

    assert(hasType(playerA, ObservationType::BasiliskEvaded));
    assert(!hasType(playerB, ObservationType::BasiliskEvaded));
}

void jackalStunConfirmationIsPrivateToShooter() {
    auto state = makeObservationWorld();
    const std::vector<GameEvent> events{
        GameEvent{GameEventType::JackalStunned, PlayerId{1}, std::nullopt,
                  CaveId{2}, 2}
    };

    const auto shooter = ObservationSystem::buildForPlayer(state, 1, events);
    const auto rival = ObservationSystem::buildForPlayer(state, 2, events);
    const auto* confirmation = findType(shooter, ObservationType::JackalStunned);

    assert(confirmation != nullptr);
    assert(!confirmation->cave.has_value());
    assert(confirmation->amount == 0);
    assert(!hasType(rival, ObservationType::JackalStunned));
}

void pvpFeedbackDistinguishesHitsKillsAndOtherHunters() {
    auto state = makeObservationWorld();
    state.players.push_back(PlayerState{3, 7, 100, 3, true});

    const std::vector<GameEvent> nonlethal{
        GameEvent{GameEventType::ArrowHitPlayer, PlayerId{1}, PlayerId{2},
                  CaveId{2}, 50},
        GameEvent{GameEventType::PlayerDamaged, PlayerId{1}, PlayerId{2},
                  CaveId{2}, 50},
    };
    const auto shooterHit =
        ObservationSystem::buildForPlayer(state, 1, nonlethal);
    const auto victimHit =
        ObservationSystem::buildForPlayer(state, 2, nonlethal);
    const auto observerHit =
        ObservationSystem::buildForPlayer(state, 3, nonlethal);
    assert(hasType(shooterHit, ObservationType::YouHitRival));
    assert(!hasType(shooterHit, ObservationType::YouKilledRival));
    assert(hasType(victimHit, ObservationType::ArrowHitYou));
    assert(!hasType(victimHit, ObservationType::YouHitRival));
    assert(!hasType(observerHit, ObservationType::YouHitRival));

    state.players[1].alive = false;
    state.players[1].health = 0;
    const std::vector<GameEvent> lethal{
        GameEvent{GameEventType::ArrowHitPlayer, PlayerId{1}, PlayerId{2},
                  CaveId{2}, 50},
        GameEvent{GameEventType::PlayerDamaged, PlayerId{1}, PlayerId{2},
                  CaveId{2}, 50},
        GameEvent{GameEventType::PlayerKilled, PlayerId{1}, PlayerId{2},
                  CaveId{2}},
    };
    const auto killer = ObservationSystem::buildForPlayer(state, 1, lethal);
    const auto observer = ObservationSystem::buildForPlayer(state, 3, lethal);
    assert(hasType(killer, ObservationType::YouKilledRival));
    assert(!hasType(killer, ObservationType::YouHitRival));
    assert(!hasType(killer, ObservationType::RivalDied));
    assert(hasType(observer, ObservationType::RivalDied));
    assert(!hasType(observer, ObservationType::YouKilledRival));
}

void privateFailureFeedbackGoesOnlyToActor() {
    auto state = makeObservationWorld();
    const std::vector<GameEvent> events{
        GameEvent{GameEventType::ArrowMissed, PlayerId{1}, std::nullopt,
                  CaveId{2}},
        GameEvent{GameEventType::CaveAlreadySearched, PlayerId{1},
                  std::nullopt, CaveId{1}},
        GameEvent{GameEventType::InventoryFull, PlayerId{1}, std::nullopt,
                  CaveId{1}, 0, std::nullopt, ItemType::BloodBait},
    };

    const auto actor = ObservationSystem::buildForPlayer(state, 1, events);
    const auto rival = ObservationSystem::buildForPlayer(state, 2, events);
    assert(hasType(actor, ObservationType::ArrowMissed));
    assert(hasType(actor, ObservationType::CaveAlreadySearched));
    const auto* full = findType(actor, ObservationType::InventoryFull);
    assert(full != nullptr && full->itemType == ItemType::BloodBait);
    assert(!hasType(rival, ObservationType::ArrowMissed));
    assert(!hasType(rival, ObservationType::CaveAlreadySearched));
    assert(!hasType(rival, ObservationType::InventoryFull));
}

void emptySearchFeedbackIsSuppressedByConcreteOutcomes() {
    auto state = makeObservationWorld();
    const GameEvent completed{
        GameEventType::SearchCompleted, PlayerId{1}, std::nullopt, CaveId{1}};
    auto observations =
        ObservationSystem::buildForPlayer(state, 1, {completed});
    assert(hasType(observations, ObservationType::SearchEmpty));
    assert(!hasType(
        ObservationSystem::buildForPlayer(state, 2, {completed}),
        ObservationType::SearchEmpty));

    constexpr std::array concreteOutcomes{
        GameEventType::ItemFound,
        GameEventType::OldHuntersMapFound,
        GameEventType::InventoryFull,
        GameEventType::ExoticCallingCardFound,
    };
    for (const GameEventType outcome : concreteOutcomes) {
        GameEvent result{outcome, PlayerId{1}, std::nullopt, CaveId{1}};
        result.itemType = ItemType::OldMinersMap;
        observations = ObservationSystem::buildForPlayer(
            state, 1, {completed, result});
        assert(!hasType(observations, ObservationType::SearchEmpty));
    }
}

void itemUseFeedbackAvoidsHealingDuplication() {
    auto state = makeObservationWorld();
    const std::vector<GameEvent> healing{
        GameEvent{GameEventType::ItemUsed, PlayerId{1}, std::nullopt,
                  CaveId{1}, 1, std::nullopt, ItemType::HealingDraught},
        GameEvent{GameEventType::PlayerHealed, PlayerId{1}, PlayerId{1},
                  CaveId{1}, 35, std::nullopt, ItemType::HealingDraught},
    };
    const auto healed = ObservationSystem::buildForPlayer(state, 1, healing);
    assert(hasType(healed, ObservationType::PlayerHealed));
    assert(!hasType(healed, ObservationType::ItemUsed));
    assert(findType(healed, ObservationType::PlayerHealed)->amount == 35);

    constexpr std::array confirmedItems{
        ItemType::OldMinersMap,
        ItemType::JackalRepellent,
        ItemType::SurveyFragment,
        ItemType::BloodBait,
    };
    for (const ItemType item : confirmedItems) {
        const std::vector<GameEvent> events{
            GameEvent{GameEventType::ItemUsed, PlayerId{1}, std::nullopt,
                      CaveId{1}, 1, std::nullopt, item},
        };
        const auto actor = ObservationSystem::buildForPlayer(state, 1, events);
        const auto rival = ObservationSystem::buildForPlayer(state, 2, events);
        const auto* used = findType(actor, ObservationType::ItemUsed);
        assert(used != nullptr && used->itemType == item);
        assert(!hasType(rival, ObservationType::ItemUsed));
    }
}

void behaviorChangeFeedbackRequiresOwnBasiliskHit() {
    auto state = makeObservationWorld();
    const std::vector<GameEvent> events{
        GameEvent{GameEventType::BasiliskBehaviorChanged, std::nullopt,
                  std::nullopt, CaveId{3}, 0, BasiliskBehavior::Restless},
        GameEvent{GameEventType::ArrowReachedBasilisk, PlayerId{1},
                  std::nullopt, CaveId{3}, 1, BasiliskBehavior::Normal},
    };

    const auto shooter = ObservationSystem::buildForPlayer(state, 1, events);
    const auto rival = ObservationSystem::buildForPlayer(state, 2, events);
    const auto* changed =
        findType(shooter, ObservationType::BasiliskBehaviorChanged);
    assert(changed != nullptr);
    assert(!changed->cave.has_value());
    assert(!changed->basiliskBehavior.has_value());
    assert(!hasType(rival, ObservationType::BasiliskBehaviorChanged));

    const auto noWitness = ObservationSystem::buildForPlayer(
        state, 1, {events.front()});
    assert(!hasType(noWitness, ObservationType::BasiliskBehaviorChanged));
}

void pitDeathExplainsCauseAndHidesRivalLocation() {
    auto state = makeObservationWorld();
    state.players[1].alive = false;
    state.players[1].health = 0;

    const std::vector<GameEvent> events{
        GameEvent{GameEventType::PitTriggered, PlayerId{2}, PlayerId{2}, CaveId{2}},
        GameEvent{GameEventType::PlayerKilled, std::nullopt, PlayerId{2}, CaveId{2}}
    };

    const auto deadHunter = ObservationSystem::buildForPlayer(state, 2, events);
    const auto survivor = ObservationSystem::buildForPlayer(state, 1, events);

    assert(hasType(deadHunter, ObservationType::FellIntoPit));
    assert(!hasType(deadHunter, ObservationType::YouDied));

    const auto* rivalDied = findType(survivor, ObservationType::RivalDied);
    assert(rivalDied != nullptr);
    assert(!rivalDied->cave.has_value());
    assert(!rivalDied->otherPlayer.has_value());
}

void basiliskContactDeathExplainsCauseOnlyToVictim() {
    auto state = makeObservationWorld();
    state.players[0].alive = false;
    state.players[0].health = 0;

    const std::vector<GameEvent> events{
        GameEvent{GameEventType::PlayerKilled, std::nullopt, PlayerId{1},
                  CaveId{3}, 0, BasiliskBehavior::Normal}
    };

    const auto victim = ObservationSystem::buildForPlayer(state, 1, events);
    const auto rival = ObservationSystem::buildForPlayer(state, 2, events);

    assert(hasType(victim, ObservationType::BasiliskFoundYou));
    assert(!hasType(victim, ObservationType::YouDied));
    assert(!hasType(rival, ObservationType::BasiliskFoundYou));
    assert(hasType(rival, ObservationType::RivalDied));
}

void nonBasiliskDeathKeepsGenericDeathObservation() {
    auto state = makeObservationWorld();
    state.players[0].alive = false;
    state.players[0].health = 0;

    const std::vector<GameEvent> events{
        GameEvent{GameEventType::PlayerKilled, PlayerId{2}, PlayerId{1}, CaveId{1}}
    };

    const auto victim = ObservationSystem::buildForPlayer(state, 1, events);
    assert(hasType(victim, ObservationType::YouDied));
    assert(!hasType(victim, ObservationType::BasiliskFoundYou));
}

void jackalAttacksExplainCauseOnlyToVictim() {
    auto state = makeObservationWorld();
    const std::vector<GameEvent> events{
        GameEvent{GameEventType::JackalRobbedArrow, std::nullopt, PlayerId{1}, CaveId{1}, 1},
        GameEvent{GameEventType::JackalScaredPlayer, std::nullopt, PlayerId{1}, CaveId{5}},
        GameEvent{GameEventType::JackalKnockedOutPlayer, std::nullopt, PlayerId{1}, CaveId{6}, 5},
        GameEvent{GameEventType::PlayerDamaged, std::nullopt, PlayerId{1}, CaveId{6}, 5},
        GameEvent{GameEventType::JackalRepelled, PlayerId{1}, PlayerId{1}, CaveId{6}}
    };

    const auto victim = ObservationSystem::buildForPlayer(state, 1, events);
    const auto rival = ObservationSystem::buildForPlayer(state, 2, events);

    assert(hasType(victim, ObservationType::JackalRobbedYou));
    assert(hasType(victim, ObservationType::JackalScaredYou));
    assert(hasType(victim, ObservationType::JackalKnockedOutYou));
    assert(hasType(victim, ObservationType::JackalRepelled));
    assert(hasType(victim, ObservationType::YouWereDamaged));

    const auto* robbed = findType(victim, ObservationType::JackalRobbedYou);
    assert(robbed != nullptr && robbed->amount == 1);

    assert(!hasType(rival, ObservationType::JackalRobbedYou));
    assert(!hasType(rival, ObservationType::JackalScaredYou));
    assert(!hasType(rival, ObservationType::JackalKnockedOutYou));
    assert(!hasType(rival, ObservationType::JackalRepelled));
    assert(!hasType(rival, ObservationType::YouWereDamaged));
}

void multipleJackalTheftsAggregateIntoOneObservation() {
    auto state = makeObservationWorld();
    const std::vector<GameEvent> events{
        GameEvent{GameEventType::JackalRobbedArrow, std::nullopt, PlayerId{1}, CaveId{1}, 1},
        GameEvent{GameEventType::JackalRobbedArrow, std::nullopt, PlayerId{1}, CaveId{1}, 1}
    };

    const auto observations = ObservationSystem::buildForPlayer(state, 1, events);
    const auto count = std::count_if(observations.begin(), observations.end(),
        [](const PlayerObservation& observation) {
            return observation.type == ObservationType::JackalRobbedYou;
        });

    assert(count == 1);
    const auto* robbed = findType(observations, ObservationType::JackalRobbedYou);
    assert(robbed != nullptr && robbed->amount == 2);
}

} // namespace

int main() {
    nearbyThreatsProduceCluesWithoutLocations();
    basiliskClueChangesForLurker();
    restlessMovementCanBeHeardTwoHopsAway();
    enragedExposesOnlyLastKnownCave();
    privateLootDoesNotLeakToOtherHunter();
    exactRivalMovementEventIsNotForwarded();
    ownBasiliskShotGetsOutcomeFeedback();
    jackalStunConfirmationIsPrivateToShooter();
    pvpFeedbackDistinguishesHitsKillsAndOtherHunters();
    privateFailureFeedbackGoesOnlyToActor();
    emptySearchFeedbackIsSuppressedByConcreteOutcomes();
    itemUseFeedbackAvoidsHealingDuplication();
    behaviorChangeFeedbackRequiresOwnBasiliskHit();
    pitDeathExplainsCauseAndHidesRivalLocation();
    basiliskContactDeathExplainsCauseOnlyToVictim();
    nonBasiliskDeathKeepsGenericDeathObservation();
    jackalAttacksExplainCauseOnlyToVictim();
    multipleJackalTheftsAggregateIntoOneObservation();

    std::cout << "Basilisk observation tests passed.\n";
    return 0;
}
