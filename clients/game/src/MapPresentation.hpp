#pragma once

#include <compare>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "MapLayout.hpp"
#include "basilisk/client/RoutePlanner.hpp"

namespace basilisk::game {

struct PresentationPoint {
    double x{0.0};
    double y{0.0};

    bool operator==(const PresentationPoint&) const = default;
};

struct PresentationRect {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
};

struct MapTransform {
    PresentationRect bounds;
    LogicalPoint logicalCenter;
    double pixelsPerLogicalUnit{1.0};
    double uiScale{1.0};
};

struct UnknownExitKey {
    CaveId source{};
    TunnelId tunnel{};

    auto operator<=>(const UnknownExitKey&) const = default;
};

struct RouteEdge {
    CaveId low{};
    CaveId high{};

    auto operator<=>(const RouteEdge&) const = default;
};

using RouteEdgeSet = std::set<RouteEdge>;

enum class MapHitKind {
    None,
    DiscoveredCave,
    UnknownExit
};

struct MapHitTarget {
    MapHitKind kind{MapHitKind::None};
    std::optional<CaveId> cave;
    std::optional<UnknownExitKey> unknownExit;
};

struct MapPresentationGeometry {
    MapTransform transform;

    // An undiscovered temporary Pit has an exact player-safe CaveId but no
    // player-safe geometry. These disconnected positions are presentation
    // slots only: they never become map edges, discovery, or route nodes.
    std::map<CaveId, LogicalPoint> temporaryPitPositions;
};

struct MapPresentationState {
    std::optional<CaveId> hoveredCave;
    std::optional<UnknownExitKey> hoveredUnknownExit;
    std::optional<CaveId> routeDestination;
    client_navigation::KnownRoutePlan route;
};

enum class DestinationControl {
    None,
    Mark,
    Clear
};

[[nodiscard]] MapPresentationGeometry buildMapPresentationGeometry(
    const PlayerMapView& map,
    const PlayerMapLayout& layout,
    const std::vector<CaveId>& temporarilyRevealedPitCaves,
    PresentationRect bounds,
    double padding,
    double uiScale);

[[nodiscard]] PresentationPoint projectMapPoint(
    LogicalPoint point,
    const MapTransform& transform);

[[nodiscard]] MapHitTarget hitTestPlayerKnownMap(
    const PlayerMapView& map,
    const PlayerMapLayout& layout,
    const MapPresentationGeometry& geometry,
    PresentationPoint pointer);

void updateMapHover(MapPresentationState& state, const MapHitTarget& hit);

[[nodiscard]] bool selectRouteDestination(
    MapPresentationState& state,
    const PlayerMapView& map,
    CaveId destination);

[[nodiscard]] bool selectRouteFromHit(
    MapPresentationState& state,
    const PlayerMapView& map,
    const MapHitTarget& hit);

// Mirrors the web-debug GPS rule: only known routes at least two hops long,
// without an immediate legal spatial action, receive a destination control.
[[nodiscard]] DestinationControl destinationControlForCave(
    const PlayerMapView& map,
    CaveId cave,
    std::optional<CaveId> markedDestination,
    bool hasMatchingLegalAction);

[[nodiscard]] bool applyDestinationControl(
    MapPresentationState& state,
    const PlayerMapView& map,
    CaveId cave,
    DestinationControl control);

void refreshSelectedRoute(
    MapPresentationState& state,
    const PlayerMapView& map);

// Only adjacent CaveIds in a reachable plan become highlighted edges.
[[nodiscard]] RouteEdgeSet routeEdges(
    const client_navigation::KnownRoutePlan& route);

[[nodiscard]] bool containsRouteEdge(
    const RouteEdgeSet& edges,
    CaveId a,
    CaveId b);

} // namespace basilisk::game
