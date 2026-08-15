#include "MapPresentation.hpp"

#include <algorithm>
#include <cmath>
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

void include(LogicalBounds& bounds, LogicalPoint point) {
    bounds.minimumX = std::min(bounds.minimumX, point.x);
    bounds.minimumY = std::min(bounds.minimumY, point.y);
    bounds.maximumX = std::max(bounds.maximumX, point.x);
    bounds.maximumY = std::max(bounds.maximumY, point.y);
    bounds.populated = true;
}

LogicalBounds visibleLogicalBounds(
    const PlayerMapView& map,
    const PlayerMapLayout& layout) {

    LogicalBounds bounds;
    std::set<CaveId> discovered;
    for (const DiscoveredCaveView& cave : map.caves) discovered.insert(cave.cave);

    for (const DiscoveredCaveView& cave : map.caves) {
        if (const auto position = layout.cavePosition(cave.cave)) include(bounds, *position);
        for (const TunnelView& exit : cave.exits) {
            if (exit.destination.has_value() && discovered.contains(*exit.destination)) {
                continue;
            }
            if (const auto stub = layout.exitStubPosition(cave.cave, exit.id)) {
                include(bounds, *stub);
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

    const std::optional<LogicalBounds> fixedBounds = layout.fixedBounds();
    LogicalBounds logicalBounds = fixedBounds.value_or(
        visibleLogicalBounds(map, layout));
    if (!logicalBounds.populated) include(logicalBounds, LogicalPoint{});

    std::vector<CaveId> disconnectedPits;
    for (CaveId pit : temporarilyRevealedPitCaves) {
        if (!isDiscovered(map, pit)) disconnectedPits.push_back(pit);
    }
    std::sort(disconnectedPits.begin(), disconnectedPits.end());
    disconnectedPits.erase(
        std::unique(disconnectedPits.begin(), disconnectedPits.end()),
        disconnectedPits.end());

    if (fixedBounds.has_value()) {
        for (const CaveId pit : disconnectedPits) {
            if (const auto position =
                    layout.temporarilyRevealedCavePosition(pit)) {
                result.temporaryPitPositions.emplace(pit, *position);
            }
        }
    } else if (!disconnectedPits.empty()) {
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
            include(logicalBounds, position);
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
    std::optional<MapHitTarget> historicalExit;
    for (const DiscoveredCaveView& cave : map.caves) {
        for (const TunnelView& exit : cave.exits) {
            if (exit.destination.has_value()) continue;
            const auto position = layout.exitStubPosition(cave.cave, exit.id);
            if (!position.has_value()) continue;
            if (distanceSquared(projectMapPoint(*position, geometry.transform), pointer) <=
                exitRadius * exitRadius) {
                MapHitTarget hit{
                    MapHitKind::UnknownExit,
                    std::nullopt,
                    UnknownExitKey{cave.cave, exit.id}};
                if (cave.cave == map.currentCave) return hit;
                if (!historicalExit.has_value()) historicalExit = hit;
            }
        }
    }
    return historicalExit.value_or(MapHitTarget{});
}

void updateMapHover(MapPresentationState& state, const MapHitTarget& hit) {
    state.hoveredCave = hit.kind == MapHitKind::DiscoveredCave ? hit.cave : std::nullopt;
    state.hoveredUnknownExit =
        hit.kind == MapHitKind::UnknownExit ? hit.unknownExit : std::nullopt;
}

MapHitTarget mapHoverTargetForAction(
    const AvailableAction& action,
    CaveId currentCave) {

    if (action.targetCave.has_value()) {
        return MapHitTarget{
            MapHitKind::DiscoveredCave,
            action.targetCave,
            std::nullopt};
    }
    if (action.targetTunnel.has_value()) {
        return MapHitTarget{
            MapHitKind::UnknownExit,
            std::nullopt,
            UnknownExitKey{currentCave, *action.targetTunnel}};
    }
    return {};
}

bool selectRouteDestination(
    MapPresentationState& state,
    const PlayerMapView& map,
    CaveId destination) {

    if (!isDiscovered(map, destination)) return false;
    state.routeDestination = destination;
    refreshSelectedRoute(state, map);
    return state.routeDestination == destination || destination == map.currentCave;
}

bool selectRouteFromHit(
    MapPresentationState& state,
    const PlayerMapView& map,
    const MapHitTarget& hit) {

    return hit.kind == MapHitKind::DiscoveredCave && hit.cave.has_value() &&
        selectRouteDestination(state, map, *hit.cave);
}

DestinationControl destinationControlForCave(
    const PlayerMapView& map,
    CaveId cave,
    std::optional<CaveId> markedDestination,
    bool hasMatchingLegalAction) {

    if (hasMatchingLegalAction || cave == map.currentCave) {
        return DestinationControl::None;
    }
    const client_navigation::KnownRoutePlan route =
        client_navigation::planKnownRoute(map, cave);
    if (route.status != client_navigation::KnownRouteStatus::Reachable ||
        route.caves.size() <= 2) {
        return DestinationControl::None;
    }
    return markedDestination == cave
        ? DestinationControl::Clear
        : DestinationControl::Mark;
}

bool applyDestinationControl(
    MapPresentationState& state,
    const PlayerMapView& map,
    CaveId cave,
    DestinationControl control) {

    if (control == DestinationControl::Mark) {
        return selectRouteDestination(state, map, cave);
    }
    if (control == DestinationControl::Clear && state.routeDestination == cave) {
        state.routeDestination.reset();
        return true;
    }
    return false;
}

void refreshSelectedRoute(
    MapPresentationState& state,
    const PlayerMapView& map) {

    if (!state.routeDestination.has_value()) {
        return;
    }
    const client_navigation::KnownRoutePlan route = selectedRoute(state, map);
    if (route.status != client_navigation::KnownRouteStatus::Reachable) {
        state.routeDestination.reset();
    }
}

client_navigation::KnownRoutePlan selectedRoute(
    const MapPresentationState& state,
    const PlayerMapView& map) {

    if (!state.routeDestination.has_value()) return {};
    return client_navigation::planKnownRoute(map, *state.routeDestination);
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
