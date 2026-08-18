#pragma once

#include <vector>

#include "basilisk/world/DiscoveryState.hpp"

namespace basilisk::client_navigation {

enum class KnownRouteStatus {
    Reachable,
    Arrived,
    DestinationUndiscovered,
    Unreachable
};

struct KnownRoutePlan {
    KnownRouteStatus status{KnownRouteStatus::Unreachable};
    std::vector<CaveId> caves;

    [[nodiscard]] bool arrived() const noexcept {
        return status == KnownRouteStatus::Arrived;
    }
};

// Plans only across destinations exposed by PlayerMapView. The caller owns
// any selected destination and can clear it when arrived() becomes true.
[[nodiscard]] KnownRoutePlan planKnownRoute(
    const PlayerMapView& map,
    CaveId destination);

} // namespace basilisk::client_navigation
