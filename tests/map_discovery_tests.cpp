#include <cassert>
#include <iostream>
#include <optional>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/systems/MapDiscoverySystem.hpp"
#include "basilisk/systems/RoundController.hpp"

using namespace basilisk;

namespace {

MatchState makeFogWorld() {
    MatchState state;
    state.matchSeed = 91001;
    state.mapSeed = 92002;
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FogOfWar;

    for (CaveId cave = 1; cave <= 6; ++cave) {
        state.world.addCave(cave);
    }

    // Connection insertion order is intentional: from Cave 1, tunnel #1 goes
    // to Cave 2 and tunnel #2 goes to Cave 3.
    state.world.connect(1, 2);
    state.world.connect(1, 3);
    state.world.connect(2, 4);
    state.world.connect(2, 5);
    state.world.connect(3, 6);

    state.basilisk.cave = 6;
    state.players = {
        PlayerState{1, 1, 100, 3, true},
        PlayerState{2, 5, 100, 3, true}
    };

    return state;
}

const DiscoveredCaveView& caveView(const PlayerMapView& view, CaveId cave) {
    for (const auto& entry : view.caves) {
        if (entry.cave == cave) return entry;
    }
    assert(false && "Expected cave was not present in map view");
    return view.caves.front();
}

bool hasEventForPlayer(
    const std::vector<GameEvent>& events,
    GameEventType type,
    PlayerId player) {

    for (const auto& event : events) {
        if (event.type == type && event.actor == player) return true;
    }
    return false;
}

void initialFogShowsExitsButHidesDestinations() {
    auto state = makeFogWorld();
    MapDiscoverySystem::initializePlayer(state, state.players[0]);

    const auto view = MapDiscoverySystem::buildView(state, state.players[0]);
    assert(view.currentCave == 1);
    assert(view.caves.size() == 1);

    const auto& current = caveView(view, 1);
    assert(current.exits.size() == 2);
    assert(current.exits[0].id == 1);
    assert(current.exits[1].id == 2);
    assert(!current.exits[0].destination.has_value());
    assert(!current.exits[1].destination.has_value());
}

void hiddenDestinationCannotBeSubmittedDirectly() {
    auto state = makeFogWorld();
    RoundController controller;

    // Player knows there are exits, but Cave 2 has not been revealed. Sending
    // the hidden CaveId directly must not bypass the fog-of-war contract.
    const auto events = controller.resolve(state, {
        PlayerAction{1, ActionType::Move, CaveId{2}},
        PlayerAction{2, ActionType::Search}
    });
    (void)events;

    assert(state.players[0].cave == 1);
}

void opaqueTunnelTraversalRevealsDestination() {
    auto state = makeFogWorld();
    RoundController controller;

    PlayerAction move;
    move.player = 1;
    move.type = ActionType::Move;
    move.targetTunnel = TunnelId{1};

    const auto events = controller.resolve(state, {
        move,
        PlayerAction{2, ActionType::Search}
    });

    assert(state.players[0].cave == 2);
    assert(state.players[0].discovery.knownCaves.contains(1));
    assert(state.players[0].discovery.knownCaves.contains(2));
    assert(MapDiscoverySystem::knowsConnection(state.players[0], 1, 2));
    assert(hasEventForPlayer(events, GameEventType::CaveDiscovered, 1));
    assert(hasEventForPlayer(events, GameEventType::TunnelDestinationRevealed, 1));

    const auto view = MapDiscoverySystem::buildView(state, state.players[0]);

    const auto& oldCave = caveView(view, 1);
    assert(oldCave.exits[0].destination == CaveId{2});
    assert(!oldCave.exits[1].destination.has_value());

    const auto& newCave = caveView(view, 2);
    assert(newCave.exits.size() == 3);

    // The tunnel back to Cave 1 is known because it is the connection just
    // traversed. Other exits from Cave 2 remain opaque.
    bool foundKnownReturn = false;
    int hiddenExits = 0;
    for (const auto& exit : newCave.exits) {
        if (exit.destination == CaveId{1}) foundKnownReturn = true;
        if (!exit.destination.has_value()) ++hiddenExits;
    }
    assert(foundKnownReturn);
    assert(hiddenExits == 2);
}

void discoveriesRemainPlayerSpecific() {
    auto state = makeFogWorld();
    RoundController controller;

    PlayerAction move;
    move.player = 1;
    move.type = ActionType::Move;
    move.targetTunnel = TunnelId{1};

    const auto events = controller.resolve(state, {
        move,
        PlayerAction{2, ActionType::Search}
    });
    (void)events;

    assert(state.players[0].discovery.knownCaves.contains(2));
    assert(!state.players[1].discovery.knownCaves.contains(2));
    assert(!MapDiscoverySystem::knowsConnection(state.players[1], 1, 2));
}

void fullMapModeRevealsCompleteTopology() {
    auto state = makeFogWorld();
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FullMap;

    MapDiscoverySystem::initializePlayer(state, state.players[0]);
    const auto view = MapDiscoverySystem::buildView(state, state.players[0]);

    assert(view.caves.size() == state.world.size());
    for (const auto& cave : view.caves) {
        for (const auto& exit : cave.exits) {
            assert(exit.destination.has_value());
        }
    }
}

} // namespace

int main() {
    initialFogShowsExitsButHidesDestinations();
    hiddenDestinationCannotBeSubmittedDirectly();
    opaqueTunnelTraversalRevealsDestination();
    discoveriesRemainPlayerSpecific();
    fullMapModeRevealsCompleteTopology();

    std::cout << "Basilisk map discovery tests passed.\n";
    return 0;
}
