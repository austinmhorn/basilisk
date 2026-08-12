#include <algorithm>
#include <cassert>
#include <iostream>

#include "basilisk/Action.hpp"
#include "basilisk/systems/MapDiscoverySystem.hpp"
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

void placePlayerAdjacentToPit(MatchState& state, PlayerState& player) {
    assert(!state.pits.empty());
    const CaveId pit = state.pits.front().cave;
    const auto ids = state.world.caveIds();
    const auto it = std::find_if(ids.begin(), ids.end(), [&](CaveId cave) {
        return state.world.areConnected(cave, pit);
    });
    assert(it != ids.end());
    player.cave = *it;
}

void successfulInvestigationMarksCorrectLocalTunnel() {
    auto state = MapGenerator::generate(8101, 8102);
    auto& player = state.players.front();
    placePlayerAdjacentToPit(state, player);
    state.rules.pitInvestigationNumerator = 1;
    state.rules.pitInvestigationDenominator = 1;

    RoundController controller;
    const auto events = controller.resolve(state, {search(player.id)});

    const auto clue = player.knownPitTunnels.find(player.cave);
    assert(clue != player.knownPitTunnels.end());

    const auto& connections = state.world.cave(player.cave).connections;
    assert(clue->second >= 1 && clue->second <= connections.size());
    const CaveId destination = connections[static_cast<std::size_t>(clue->second - 1)];
    assert(std::any_of(state.pits.begin(), state.pits.end(), [&](const PitState& pit) {
        return pit.active && pit.cave == destination;
    }));

    assert(std::any_of(events.begin(), events.end(), [&](const GameEvent& event) {
        return event.type == GameEventType::PitInvestigationSucceeded &&
               event.actor == player.id && event.tunnel == clue->second;
    }));

    const auto view = MapDiscoverySystem::buildView(state, player);
    const auto cave = std::find_if(view.caves.begin(), view.caves.end(), [&](const auto& c) {
        return c.cave == player.cave;
    });
    assert(cave != view.caves.end());
    assert(std::count_if(cave->exits.begin(), cave->exits.end(), [](const TunnelView& tunnel) {
        return tunnel.strongColdDraft;
    }) == 1);
}

void inconclusiveInvestigationDoesNotInventTunnel() {
    auto state = MapGenerator::generate(8201, 8202);
    auto& player = state.players.front();
    placePlayerAdjacentToPit(state, player);
    state.rules.pitInvestigationNumerator = 0;
    state.rules.pitInvestigationDenominator = 1;

    RoundController controller;
    const auto events = controller.resolve(state, {search(player.id)});

    assert(!player.knownPitTunnels.contains(player.cave));
    assert(std::any_of(events.begin(), events.end(), [&](const GameEvent& event) {
        return event.type == GameEventType::PitInvestigationInconclusive && event.actor == player.id;
    }));
}

void inconclusiveInvestigationCanBeRetriedAfterCaveWasSearched() {
    auto state = MapGenerator::generate(8301, 8302);
    auto& player = state.players.front();
    placePlayerAdjacentToPit(state, player);
    RoundController controller;

    state.rules.pitInvestigationNumerator = 0;
    state.rules.pitInvestigationDenominator = 1;
    const auto firstEvents = controller.resolve(state, {search(player.id)});
    (void)firstEvents;
    assert(player.searchedCaves.contains(player.cave));
    assert(!player.knownPitTunnels.contains(player.cave));

    state.rules.pitInvestigationNumerator = 1;
    state.rules.pitInvestigationDenominator = 1;
    const auto secondEvents = controller.resolve(state, {search(player.id)});

    assert(player.knownPitTunnels.contains(player.cave));
    assert(std::any_of(secondEvents.begin(), secondEvents.end(), [&](const GameEvent& event) {
        return event.type == GameEventType::PitInvestigationSucceeded && event.actor == player.id;
    }));
}

} // namespace

int main() {
    successfulInvestigationMarksCorrectLocalTunnel();
    inconclusiveInvestigationDoesNotInventTunnel();
    inconclusiveInvestigationCanBeRetriedAfterCaveWasSearched();
    std::cout << "Basilisk pit investigation tests passed.\n";
    return 0;
}
