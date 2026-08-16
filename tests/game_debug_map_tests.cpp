#include <algorithm>
#include <cassert>
#include <set>

#include "DebugMapProvider.hpp"
#include "LocalGameSessionAdapter.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;
using namespace basilisk::game;
using namespace basilisk::game::debug;

namespace {

std::set<PhysicalTunnel> physicalTunnels(const MatchState& state) {
    std::set<PhysicalTunnel> tunnels;
    for (const CaveId source : state.world.caveIds()) {
        for (const CaveId destination : state.world.cave(source).connections) {
            const auto [first, second] = std::minmax(source, destination);
            tunnels.insert(PhysicalTunnel{first, second});
        }
    }
    return tunnels;
}

std::size_t playerMapSignature(const PlayerMapView& map) {
    std::size_t signature = static_cast<std::size_t>(map.currentCave);
    for (const DiscoveredCaveView& cave : map.caves) {
        signature = signature * 131U + static_cast<std::size_t>(cave.cave);
        for (const TunnelView& exit : cave.exits) {
            signature = signature * 131U + static_cast<std::size_t>(exit.id);
            signature = signature * 131U + static_cast<std::size_t>(
                exit.destination.value_or(CaveId{}));
            signature = signature * 2U + static_cast<std::size_t>(
                exit.strongColdDraft);
        }
    }
    return signature;
}

void revealContainsCompletePhysicalTopology() {
    constexpr MapSeed mapSeed{1};
    constexpr MatchSeed matchSeed{424242};
    const MatchState authoritative = MapGenerator::generate(mapSeed, matchSeed);
    auto debugSession = LocalGameSessionAdapter::createDebug(mapSeed, matchSeed);
    assert(debugSession.session != nullptr);
    assert(debugSession.mapProvider != nullptr);

    const DebugMapTruth& truth = debugSession.mapProvider->truth();
    assert(truth.fullBounds.populated);
    assert(truth.cavePositions.size() == authoritative.world.size());
    assert(std::set<PhysicalTunnel>(
               truth.tunnels.begin(), truth.tunnels.end()) ==
           physicalTunnels(authoritative));
}

void togglingRevealDoesNotMutatePlayerState() {
    auto debugSession = LocalGameSessionAdapter::createDebug(
        MapSeed{1}, MatchSeed{424242});
    assert(debugSession.session != nullptr);
    const PlayerRoundSnapshot* snapshot =
        debugSession.session->displayedSnapshot();
    assert(snapshot != nullptr);
    const RoundNumber round = snapshot->round;
    const CaveId currentCave = snapshot->currentCave;
    const std::size_t mapSignature = playerMapSignature(snapshot->map);

    DebugMapRevealState reveal;
    assert(!reveal.revealed());
    reveal.toggle();
    assert(reveal.revealed());
    reveal.toggle();
    assert(!reveal.revealed());

    snapshot = debugSession.session->displayedSnapshot();
    assert(snapshot != nullptr);
    assert(snapshot->round == round);
    assert(snapshot->currentCave == currentCave);
    assert(playerMapSignature(snapshot->map) == mapSignature);
}

void fixedHiddenEndpointsMatchDebugDestinationCoordinates() {
    constexpr MapSeed mapSeed{1};
    constexpr MatchSeed matchSeed{424242};
    const MatchState authoritative = MapGenerator::generate(mapSeed, matchSeed);
    auto debugSession = LocalGameSessionAdapter::createDebug(mapSeed, matchSeed);
    assert(debugSession.session != nullptr);
    assert(debugSession.mapProvider != nullptr);
    const PlayerFixedMapGeometry* geometry =
        debugSession.session->displayedMapGeometry();
    assert(geometry != nullptr);
    assert(!geometry->unknownExitEndpoints.empty());

    const DebugMapTruth& truth = debugSession.mapProvider->truth();

    for (const auto& [exit, endpoint] : geometry->unknownExitEndpoints) {
        assert(authoritative.world.contains(exit.source));
        const auto& connections =
            authoritative.world.cave(exit.source).connections;
        assert(exit.tunnel > 0 && exit.tunnel <= connections.size());
        const CaveId destination =
            connections[static_cast<std::size_t>(exit.tunnel - 1)];
        assert(truth.cavePositions.at(destination) == endpoint);
    }
}

} // namespace

int main() {
    revealContainsCompletePhysicalTopology();
    togglingRevealDoesNotMutatePlayerState();
    fixedHiddenEndpointsMatchDebugDestinationCoordinates();
    return 0;
}
