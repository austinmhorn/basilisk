#include "DemoMap.hpp"

#include <optional>

namespace basilisk::game::demo {
namespace {

TunnelView knownExit(TunnelId tunnel, CaveId destination) {
    return TunnelView{tunnel, destination};
}

TunnelView unknownExit(TunnelId tunnel) {
    return TunnelView{tunnel, std::nullopt};
}

} // namespace

PlayerRoundSnapshot makeDemoMapSnapshot() {
    // This intentionally constructs only the public player-facing DTO. It is
    // not a generated match, simulation, or authoritative world definition.
    PlayerRoundSnapshot snapshot;
    snapshot.player = PlayerId{1};
    snapshot.round = RoundNumber{1};
    snapshot.health = 100;
    snapshot.maxHealth = 100;
    snapshot.arrows = 3;
    snapshot.maxArrows = 5;
    snapshot.alive = true;
    snapshot.currentCave = CaveId{1};
    snapshot.map.currentCave = snapshot.currentCave;
    snapshot.map.caves = {
        DiscoveredCaveView{
            CaveId{1},
            {
                knownExit(TunnelId{2}, CaveId{2}),
                unknownExit(TunnelId{6}),
                knownExit(TunnelId{10}, CaveId{3}),
            }},
        DiscoveredCaveView{
            CaveId{2},
            {
                knownExit(TunnelId{1}, CaveId{1}),
                knownExit(TunnelId{7}, CaveId{4}),
                unknownExit(TunnelId{11}),
            }},
        DiscoveredCaveView{
            CaveId{3},
            {
                knownExit(TunnelId{1}, CaveId{1}),
                knownExit(TunnelId{8}, CaveId{5}),
                unknownExit(TunnelId{12}),
            }},
        DiscoveredCaveView{
            CaveId{4},
            {
                knownExit(TunnelId{1}, CaveId{2}),
                knownExit(TunnelId{5}, CaveId{6}),
                unknownExit(TunnelId{9}),
            }},
        DiscoveredCaveView{
            CaveId{5},
            {
                knownExit(TunnelId{1}, CaveId{3}),
                knownExit(TunnelId{2}, CaveId{6}),
                unknownExit(TunnelId{13}),
            }},
        DiscoveredCaveView{
            CaveId{6},
            {
                knownExit(TunnelId{1}, CaveId{4}),
                knownExit(TunnelId{2}, CaveId{5}),
                unknownExit(TunnelId{5}),
            }},
    };
    return snapshot;
}

} // namespace basilisk::game::demo
