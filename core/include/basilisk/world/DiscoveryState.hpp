#pragma once

#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

#include "basilisk/Types.hpp"

namespace basilisk {

// TunnelId is intentionally local to a cave. It lets a client choose an
// unexplored tunnel without learning the authoritative destination CaveId.
using TunnelId = std::uint32_t;

struct DiscoveryState {
    std::unordered_set<CaveId> knownCaves;

    // Canonical undirected edge keys for tunnels whose destination has been
    // learned by this player. Unknown exits are derived from WorldGraph and
    // intentionally omit their destination in PlayerMapView.
    std::unordered_set<std::uint64_t> knownConnections;
};

struct TunnelView {
    TunnelId id{};
    std::optional<CaveId> destination;
};

struct DiscoveredCaveView {
    CaveId cave{};
    std::vector<TunnelView> exits;
};

struct PlayerMapView {
    CaveId currentCave{};
    std::vector<DiscoveredCaveView> caves;
};

} // namespace basilisk
