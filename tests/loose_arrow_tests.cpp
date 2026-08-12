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
    assert(cadence > 0);

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

} // namespace

int main() {
    arrowsSpawnAtConfiguredCadence();
    enteringLooseArrowCaveAutomaticallyCollectsIt();
    fullQuiverLeavesLooseArrowOnGround();
    std::cout << "Basilisk loose arrow tests passed.\n";
    return 0;
}
