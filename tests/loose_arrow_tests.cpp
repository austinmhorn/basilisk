#include <algorithm>
#include <cassert>
#include <iostream>

#include "basilisk/Action.hpp"
#include "basilisk/systems/RoundController.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

PlayerAction search(PlayerId player) {
    PlayerAction action;
    action.player = player;
    action.type = ActionType::Search;
    return action;
}

void arrowsSpawnAtConfiguredCadence() {
    auto state = MapGenerator::generate(7001, 7002);
    state.rules.maxLooseArrows = 4;
    RoundController controller;

    const auto cadence = state.rules.looseArrowSpawnIntervalRounds;
    assert(cadence == 5);

    for (std::uint32_t i = 0; i + 1 < cadence; ++i) {
        const auto ignoredEvents = controller.resolve(state, {search(1), search(2)});
        (void)ignoredEvents;
    }
    assert(state.looseArrows.empty());

    const auto events = controller.resolve(state, {search(1), search(2)});
    assert(state.looseArrows.size() == 1);
    assert(std::count_if(events.begin(), events.end(), [](const GameEvent& event) {
        return event.type == GameEventType::LooseArrowSpawned;
    }) == 1);
}

void customCadenceAndOffAreAuthoritative() {
    auto frequent = MapGenerator::generate(7051, 7052);
    frequent.rules.looseArrowSpawnIntervalRounds = 3;
    RoundController controller;
    for (int round = 0; round < 2; ++round)
        (void)controller.resolve(frequent, {search(1), search(2)});
    assert(frequent.looseArrows.empty());
    (void)controller.resolve(frequent, {search(1), search(2)});
    assert(frequent.looseArrows.size() == 1);

    auto disabled = MapGenerator::generate(7061, 7062);
    disabled.rules.looseArrowSpawnIntervalRounds = 0;
    for (int round = 0; round < 8; ++round)
        (void)controller.resolve(disabled, {search(1), search(2)});
    assert(disabled.looseArrows.empty());
}

void enteringLooseArrowCaveAutomaticallyCollectsIt() {
    auto state = MapGenerator::generate(7101, 7102);
    state.rules.looseArrowSpawnIntervalRounds = 0;
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FullMap;
    RoundController controller;

    auto& player = state.players[0];
    player.arrows = 1;
    const CaveId destination = state.world.cave(player.cave).connections.front();
    state.looseArrows = {destination};

    PlayerAction move;
    move.player = player.id;
    move.type = ActionType::Move;
    move.targetCave = destination;

    const auto events = controller.resolve(state, {move, search(2)});
    assert(player.arrows == 2);
    assert(state.looseArrows.empty());
    assert(std::any_of(events.begin(), events.end(), [&](const GameEvent& event) {
        return event.type == GameEventType::ArrowFound && event.actor == player.id && event.cave == destination;
    }));
}

void fullQuiverLeavesLooseArrowOnGround() {
    auto state = MapGenerator::generate(7201, 7202);
    state.rules.looseArrowSpawnIntervalRounds = 0;
    RoundController controller;

    auto& player = state.players[0];
    player.arrows = state.rules.maxArrows;
    state.looseArrows = {player.cave};
    const auto ignoredEvents = controller.resolve(state, {search(1), search(2)});
    (void)ignoredEvents;
    assert(state.looseArrows.size() == 1);
    assert(player.arrows == state.rules.maxArrows);
}

void customArrowCapacityIsAuthoritative() {
    Rules rules;
    rules.startingArrows = 1;
    rules.maxArrows = 2;
    rules.looseArrowSpawnIntervalRounds = 0;
    auto state = MapGenerator::generate(7301, 7302, rules);
    assert(std::all_of(state.players.begin(), state.players.end(), [](const PlayerState& player) {
        return player.arrows == 1;
    }));
    RoundController controller;
    auto& player = state.players[0];
    state.looseArrows = {player.cave};
    (void)controller.resolve(state, {search(1), search(2)});
    assert(player.arrows == 2 && state.looseArrows.empty());
    state.looseArrows = {player.cave};
    (void)controller.resolve(state, {search(1), search(2)});
    assert(player.arrows == 2 && state.looseArrows.size() == 1);
}

} // namespace

int main() {
    arrowsSpawnAtConfiguredCadence();
    customCadenceAndOffAreAuthoritative();
    enteringLooseArrowCaveAutomaticallyCollectsIt();
    fullQuiverLeavesLooseArrowOnGround();
    customArrowCapacityIsAuthoritative();
    std::cout << "Basilisk loose arrow tests passed.\n";
    return 0;
}
