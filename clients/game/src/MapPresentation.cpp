#include "MapPresentation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace basilisk::game {
namespace {

constexpr double kMinimumFramingSpan = 6.0;
constexpr double kMaximumPixelsPerLogicalUnit = 78.0;
constexpr double kTemporaryPitOffset = 3.0;
constexpr double kTemporaryPitSpacing = 3.0;
constexpr double kCaveHitRadius = 18.0;
constexpr double kUnknownExitHitRadius = 17.0;

bool isDiscovered(const PlayerMapView& map, CaveId cave) {
    return std::any_of(
        map.caves.begin(), map.caves.end(),
        [cave](const DiscoveredCaveView& view) { return view.cave == cave; });
}

double distanceSquared(PresentationPoint a, PresentationPoint b) {
    const double x = a.x - b.x;
    const double y = a.y - b.y;
    return x * x + y * y;
}

struct LogicalBounds {
    double minimumX{std::numeric_limits<double>::max()};
    double minimumY{std::numeric_limits<double>::max()};
    double maximumX{std::numeric_limits<double>::lowest()};
    double maximumY{std::numeric_limits<double>::lowest()};
    bool populated{false};

    void include(LogicalPoint point) {
        minimumX = std::min(minimumX, point.x);
        minimumY = std::min(minimumY, point.y);
        maximumX = std::max(maximumX, point.x);
        maximumY = std::max(maximumY, point.y);
        populated = true;
    }
};

LogicalBounds visibleLogicalBounds(
    const PlayerMapView& map,
    const PlayerMapLayout& layout) {

    LogicalBounds bounds;
    std::set<CaveId> discovered;
    for (const DiscoveredCaveView& cave : map.caves) discovered.insert(cave.cave);

    for (const DiscoveredCaveView& cave : map.caves) {
        if (const auto position = layout.cavePosition(cave.cave)) bounds.include(*position);
        for (const TunnelView& exit : cave.exits) {
            if (exit.destination.has_value() && discovered.contains(*exit.destination)) {
                continue;
            }
            if (const auto stub = layout.exitStubPosition(cave.cave, exit.id)) {
                bounds.include(*stub);
            }
        }
    }
    return bounds;
}

} // namespace

MapPresentationGeometry buildMapPresentationGeometry(
    const PlayerMapView& map,
    const PlayerMapLayout& layout,
    const std::vector<CaveId>& temporarilyRevealedPitCaves,
    PresentationRect bounds,
    double padding,
    double uiScale) {

    MapPresentationGeometry result;
    result.transform.bounds = bounds;
    result.transform.uiScale = std::max(0.01, uiScale);

    LogicalBounds logicalBounds = visibleLogicalBounds(map, layout);
    if (!logicalBounds.populated) logicalBounds.include(LogicalPoint{});

    std::vector<CaveId> disconnectedPits;
    for (CaveId pit : temporarilyRevealedPitCaves) {
        if (!isDiscovered(map, pit)) disconnectedPits.push_back(pit);
    }
    std::sort(disconnectedPits.begin(), disconnectedPits.end());
    disconnectedPits.erase(
        std::unique(disconnectedPits.begin(), disconnectedPits.end()),
        disconnectedPits.end());

    if (!disconnectedPits.empty()) {
        const double totalWidth =
            static_cast<double>(disconnectedPits.size() - 1) * kTemporaryPitSpacing;
        const double startX =
            (logicalBounds.minimumX + logicalBounds.maximumX - totalWidth) * 0.5;
        const double y = logicalBounds.maximumY + kTemporaryPitOffset;
        for (std::size_t index = 0; index < disconnectedPits.size(); ++index) {
            const LogicalPoint position{
                startX + static_cast<double>(index) * kTemporaryPitSpacing,
                y};
            result.temporaryPitPositions.emplace(disconnectedPits[index], position);
            logicalBounds.include(position);
        }
    }

    result.transform.logicalCenter = LogicalPoint{
        (logicalBounds.minimumX + logicalBounds.maximumX) * 0.5,
        (logicalBounds.minimumY + logicalBounds.maximumY) * 0.5};

    const double usableWidth = std::max(1.0, bounds.width - padding * 2.0);
    const double usableHeight = std::max(1.0, bounds.height - padding * 2.0);
    const double logicalWidth = std::max(
        kMinimumFramingSpan, logicalBounds.maximumX - logicalBounds.minimumX);
    const double logicalHeight = std::max(
        kMinimumFramingSpan, logicalBounds.maximumY - logicalBounds.minimumY);
    result.transform.pixelsPerLogicalUnit = std::max(
        0.01,
        std::min({
            usableWidth / logicalWidth,
            usableHeight / logicalHeight,
            kMaximumPixelsPerLogicalUnit * result.transform.uiScale}));
    return result;
}

PresentationPoint projectMapPoint(
    LogicalPoint point,
    const MapTransform& transform) {

    return PresentationPoint{
        transform.bounds.x + transform.bounds.width * 0.5 +
            (point.x - transform.logicalCenter.x) * transform.pixelsPerLogicalUnit,
        transform.bounds.y + transform.bounds.height * 0.5 +
            (point.y - transform.logicalCenter.y) * transform.pixelsPerLogicalUnit};
}

MapHitTarget hitTestPlayerKnownMap(
    const PlayerMapView& map,
    const PlayerMapLayout& layout,
    const MapPresentationGeometry& geometry,
    PresentationPoint pointer) {

    const double caveRadius = kCaveHitRadius * geometry.transform.uiScale;
    for (const DiscoveredCaveView& cave : map.caves) {
        const auto position = layout.cavePosition(cave.cave);
        if (!position.has_value()) continue;
        if (distanceSquared(projectMapPoint(*position, geometry.transform), pointer) <=
            caveRadius * caveRadius) {
            return {MapHitKind::DiscoveredCave, cave.cave, std::nullopt};
        }
    }

    const double exitRadius = kUnknownExitHitRadius * geometry.transform.uiScale;
    for (const DiscoveredCaveView& cave : map.caves) {
        for (const TunnelView& exit : cave.exits) {
            if (exit.destination.has_value()) continue;
            const auto position = layout.exitStubPosition(cave.cave, exit.id);
            if (!position.has_value()) continue;
            if (distanceSquared(projectMapPoint(*position, geometry.transform), pointer) <=
                exitRadius * exitRadius) {
                return {
                    MapHitKind::UnknownExit,
                    std::nullopt,
                    UnknownExitKey{cave.cave, exit.id}};
            }
        }
    }
    return {};
}

void updateMapHover(MapPresentationState& state, const MapHitTarget& hit) {
    state.hoveredCave = hit.kind == MapHitKind::DiscoveredCave ? hit.cave : std::nullopt;
    state.hoveredUnknownExit =
        hit.kind == MapHitKind::UnknownExit ? hit.unknownExit : std::nullopt;
}

bool selectRouteDestination(
    MapPresentationState& state,
    const PlayerMapView& map,
    CaveId destination) {

    if (!isDiscovered(map, destination)) return false;
    state.routeDestination = destination;
    refreshSelectedRoute(state, map);
    return true;
}

bool selectRouteFromHit(
    MapPresentationState& state,
    const PlayerMapView& map,
    const MapHitTarget& hit) {

    return hit.kind == MapHitKind::DiscoveredCave && hit.cave.has_value() &&
        selectRouteDestination(state, map, *hit.cave);
}

void refreshSelectedRoute(
    MapPresentationState& state,
    const PlayerMapView& map) {

    if (!state.routeDestination.has_value()) {
        state.route = {};
        return;
    }
    state.route = client_navigation::planKnownRoute(map, *state.routeDestination);
    if (state.route.arrived()) {
        state.routeDestination.reset();
        state.route = {};
    }
}

RouteEdgeSet routeEdges(const client_navigation::KnownRoutePlan& route) {
    RouteEdgeSet result;
    if (route.status != client_navigation::KnownRouteStatus::Reachable) {
        return result;
    }
    for (std::size_t index = 1; index < route.caves.size(); ++index) {
        const CaveId a = route.caves[index - 1];
        const CaveId b = route.caves[index];
        if (a == b) continue;
        const auto [low, high] = std::minmax(a, b);
        result.insert(RouteEdge{low, high});
    }
    return result;
}

bool containsRouteEdge(
    const RouteEdgeSet& edges,
    CaveId a,
    CaveId b) {

    const auto [low, high] = std::minmax(a, b);
    return edges.contains(RouteEdge{low, high});
}

} // namespace basilisk::game
