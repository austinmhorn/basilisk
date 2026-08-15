#include <cassert>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <optional>

#include "MapLayout.hpp"
#include "MapPresentation.hpp"

using namespace basilisk;
using namespace basilisk::game;

namespace {

constexpr double kTolerance = 0.000001;

struct ExitSpec {
    TunnelId tunnel{};
    std::optional<CaveId> destination;
};

DiscoveredCaveView cave(CaveId id, std::initializer_list<ExitSpec> exits) {
    DiscoveredCaveView result;
    result.cave = id;
    for (const auto& exit : exits) {
        result.exits.push_back(TunnelView{exit.tunnel, exit.destination});
    }
    return result;
}

LogicalPoint requireCave(const PlayerMapLayout& layout, CaveId caveId) {
    const auto position = layout.cavePosition(caveId);
    assert(position.has_value());
    return *position;
}

LogicalPoint requireStub(
    const PlayerMapLayout& layout,
    CaveId source,
    TunnelId tunnel) {

    const auto position = layout.exitStubPosition(source, tunnel);
    assert(position.has_value());
    return *position;
}

double distanceSquared(LogicalPoint a, LogicalPoint b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

double cross(LogicalPoint a, LogicalPoint b) {
    return a.x * b.y - a.y * b.x;
}

double dot(LogicalPoint a, LogicalPoint b) {
    return a.x * b.x + a.y * b.y;
}

LogicalPoint subtract(LogicalPoint a, LogicalPoint b) {
    return {a.x - b.x, a.y - b.y};
}

void initialPlacementUsesCurrentCaveAsOrigin() {
    PlayerMapLayout layout;
    layout.update(PlayerMapView{
        CaveId{7},
        {cave(3, {}), cave(7, {{TunnelId{1}, std::nullopt}})}});

    assert(requireCave(layout, CaveId{7}) == LogicalPoint{});
    assert(layout.cavePosition(CaveId{3}).has_value());
    assert(layout.exitStubPosition(CaveId{7}, TunnelId{1}).has_value());
}

void existingCoordinatesStayStableAfterExpansion() {
    PlayerMapLayout layout;
    layout.update(PlayerMapView{
        CaveId{1},
        {cave(1, {{TunnelId{1}, std::nullopt}})}});
    const LogicalPoint before = requireCave(layout, CaveId{1});

    layout.update(PlayerMapView{
        CaveId{1},
        {
            cave(1, {{TunnelId{1}, CaveId{2}}}),
            cave(2, {{TunnelId{1}, CaveId{1}}}),
        }});

    assert(requireCave(layout, CaveId{1}) == before);
    assert(layout.cavePosition(CaveId{2}).has_value());
}

void unknownExitDirectionStaysStable() {
    PlayerMapLayout layout;
    layout.update(PlayerMapView{
        CaveId{1},
        {cave(1, {{TunnelId{2}, std::nullopt}})}});
    const LogicalPoint before = requireStub(layout, CaveId{1}, TunnelId{2});

    layout.update(PlayerMapView{
        CaveId{1},
        {cave(1, {
            {TunnelId{1}, std::nullopt},
            {TunnelId{2}, std::nullopt},
            {TunnelId{3}, std::nullopt},
        })}});

    assert(requireStub(layout, CaveId{1}, TunnelId{2}) == before);
}

void discoveredCaveFollowsPriorStubDirection() {
    PlayerMapLayout layout;
    layout.update(PlayerMapView{
        CaveId{1},
        {cave(1, {{TunnelId{1}, std::nullopt}})}});
    const LogicalPoint source = requireCave(layout, CaveId{1});
    const LogicalPoint stubDirection = subtract(
        requireStub(layout, CaveId{1}, TunnelId{1}), source);

    layout.update(PlayerMapView{
        CaveId{1},
        {
            cave(1, {{TunnelId{1}, CaveId{2}}}),
            cave(2, {{TunnelId{1}, CaveId{1}}}),
        }});
    const LogicalPoint caveDirection = subtract(requireCave(layout, CaveId{2}), source);

    assert(std::abs(cross(stubDirection, caveDirection)) < kTolerance);
    assert(dot(stubDirection, caveDirection) > 0.0);
}

void collisionResolutionContinuesAlongExitRay() {
    PlayerMapLayout layout;
    layout.update(PlayerMapView{
        CaveId{1},
        {
            cave(1, {{TunnelId{1}, CaveId{2}}}),
            cave(2, {
                {TunnelId{1}, CaveId{1}},
                {TunnelId{2}, std::nullopt},
            }),
        }});

    const LogicalPoint source = requireCave(layout, CaveId{2});
    const LogicalPoint priorStubDirection = subtract(
        requireStub(layout, CaveId{2}, TunnelId{2}), source);

    layout.update(PlayerMapView{
        CaveId{1},
        {
            cave(1, {{TunnelId{1}, CaveId{2}}}),
            cave(2, {
                {TunnelId{1}, CaveId{1}},
                {TunnelId{2}, CaveId{3}},
            }),
            cave(3, {{TunnelId{1}, CaveId{2}}}),
        }});

    const LogicalPoint destination = requireCave(layout, CaveId{3});
    const LogicalPoint resolvedDirection = subtract(destination, source);
    assert(std::abs(cross(priorStubDirection, resolvedDirection)) < kTolerance);
    assert(dot(priorStubDirection, resolvedDirection) > 0.0);
    assert(distanceSquared(destination, requireCave(layout, CaveId{1})) > 1.0);
    assert(distanceSquared(source, destination) > distanceSquared(source, requireCave(layout, CaveId{1})));
}

void disconnectedComponentsReceiveDeterministicPositions() {
    PlayerMapLayout first;
    PlayerMapLayout second;
    const PlayerMapView map{
        CaveId{1},
        {cave(8, {}), cave(1, {})}};

    first.update(map);
    second.update(map);

    assert(requireCave(first, CaveId{1}) == LogicalPoint{});
    assert(requireCave(first, CaveId{8}) == requireCave(second, CaveId{8}));
    assert(requireCave(first, CaveId{8}) != requireCave(first, CaveId{1}));
}

void undiscoveredEndpointIsIgnored() {
    PlayerMapLayout layout;
    layout.update(PlayerMapView{
        CaveId{1},
        {cave(1, {{TunnelId{4}, CaveId{99}}})}});

    assert(!layout.cavePosition(CaveId{99}).has_value());
    assert(layout.exitStubPosition(CaveId{1}, TunnelId{4}).has_value());
}

void repeatedUpdateIsIdentical() {
    PlayerMapLayout layout;
    const PlayerMapView map{
        CaveId{1},
        {
            cave(1, {
                {TunnelId{1}, CaveId{2}},
                {TunnelId{2}, std::nullopt},
            }),
            cave(2, {{TunnelId{1}, CaveId{1}}}),
        }};

    layout.update(map);
    const LogicalPoint caveOne = requireCave(layout, CaveId{1});
    const LogicalPoint caveTwo = requireCave(layout, CaveId{2});
    const LogicalPoint stub = requireStub(layout, CaveId{1}, TunnelId{2});

    layout.update(map);
    assert(requireCave(layout, CaveId{1}) == caveOne);
    assert(requireCave(layout, CaveId{2}) == caveTwo);
    assert(requireStub(layout, CaveId{1}, TunnelId{2}) == stub);
}

PlayerMapView presentationMap() {
    return PlayerMapView{
        CaveId{1},
        {
            cave(1, {
                {TunnelId{1}, CaveId{2}},
                {TunnelId{2}, std::nullopt},
            }),
            cave(2, {
                {TunnelId{1}, CaveId{1}},
                {TunnelId{2}, CaveId{3}},
            }),
            cave(3, {{TunnelId{1}, CaveId{2}}}),
            cave(8, {}),
        }};
}

void framingContainsOnlyPlayerKnownPresentationPoints() {
    PlayerMapLayout layout;
    const PlayerMapView map = presentationMap();
    layout.update(map);
    const PresentationRect bounds{20.0, 30.0, 900.0, 600.0};
    const MapPresentationGeometry geometry = buildMapPresentationGeometry(
        map, layout, {CaveId{99}}, bounds, 50.0, 1.0);

    assert(geometry.transform.pixelsPerLogicalUnit > 1.0);
    for (const DiscoveredCaveView& view : map.caves) {
        const PresentationPoint point = projectMapPoint(
            requireCave(layout, view.cave), geometry.transform);
        assert(point.x >= bounds.x + 50.0 - kTolerance &&
               point.x <= bounds.x + bounds.width - 50.0 + kTolerance);
        assert(point.y >= bounds.y + 50.0 - kTolerance &&
               point.y <= bounds.y + bounds.height - 50.0 + kTolerance);
    }
    assert(geometry.temporaryPitPositions.contains(CaveId{99}));
    assert(!layout.cavePosition(CaveId{99}).has_value());

    const PresentationPoint pitPoint = projectMapPoint(
        geometry.temporaryPitPositions.at(CaveId{99}), geometry.transform);
    assert(hitTestPlayerKnownMap(map, layout, geometry, pitPoint).kind ==
           MapHitKind::None);

    const MapPresentationGeometry resized = buildMapPresentationGeometry(
        map, layout, {CaveId{99}}, {0.0, 0.0, 600.0, 400.0}, 40.0, 1.0);
    assert(resized.transform.pixelsPerLogicalUnit <
           geometry.transform.pixelsPerLogicalUnit);
}

void hitTestingDistinguishesCavesAndUnknownExits() {
    PlayerMapLayout layout;
    const PlayerMapView map = presentationMap();
    layout.update(map);
    const MapPresentationGeometry geometry = buildMapPresentationGeometry(
        map, layout, {}, {0.0, 0.0, 800.0, 600.0}, 40.0, 1.0);

    const MapHitTarget caveHit = hitTestPlayerKnownMap(
        map,
        layout,
        geometry,
        projectMapPoint(requireCave(layout, CaveId{2}), geometry.transform));
    assert(caveHit.kind == MapHitKind::DiscoveredCave);
    assert(caveHit.cave == CaveId{2});

    MapPresentationState hover;
    updateMapHover(hover, caveHit);
    assert(hover.hoveredCave == CaveId{2});
    assert(!hover.hoveredUnknownExit.has_value());

    const MapHitTarget exitHit = hitTestPlayerKnownMap(
        map,
        layout,
        geometry,
        projectMapPoint(
            requireStub(layout, CaveId{1}, TunnelId{2}),
            geometry.transform));
    assert(exitHit.kind == MapHitKind::UnknownExit);
    assert((exitHit.unknownExit == UnknownExitKey{CaveId{1}, TunnelId{2}}));
    updateMapHover(hover, exitHit);
    assert(!hover.hoveredCave.has_value());
    assert(hover.hoveredUnknownExit == exitHit.unknownExit);
}

void routeSelectionUsesOnlyDiscoveredKnownConnections() {
    PlayerMapLayout layout;
    const PlayerMapView map = presentationMap();
    layout.update(map);
    MapPresentationState state;

    assert(selectRouteDestination(state, map, CaveId{3}));
    assert(state.route.status == client_navigation::KnownRouteStatus::Reachable);
    assert((state.route.caves == std::vector<CaveId>{1, 2, 3}));

    assert(!selectRouteDestination(state, map, CaveId{99}));
    assert(!selectRouteFromHit(
        state,
        map,
        MapHitTarget{
            MapHitKind::UnknownExit,
            std::nullopt,
            UnknownExitKey{CaveId{1}, TunnelId{2}}}));

    assert(selectRouteDestination(state, map, CaveId{8}));
    assert(state.route.status == client_navigation::KnownRouteStatus::Unreachable);
    assert(state.route.caves.empty());

    assert(selectRouteDestination(state, map, CaveId{1}));
    assert(!state.routeDestination.has_value());
    assert(state.route.caves.empty());
}

void routeEdgesContainOnlyConsecutivePlanCaves() {
    const PlayerMapView map{
        CaveId{7},
        {
            cave(7, {
                {TunnelId{1}, CaveId{12}},
                {TunnelId{2}, CaveId{16}},
            }),
            cave(12, {{TunnelId{1}, CaveId{7}}}),
            cave(16, {
                {TunnelId{1}, CaveId{7}},
                {TunnelId{2}, CaveId{21}},
            }),
            cave(21, {{TunnelId{1}, CaveId{16}}}),
        }};
    MapPresentationState state;

    assert(selectRouteDestination(state, map, CaveId{21}));
    assert((state.route.caves == std::vector<CaveId>{7, 16, 21}));
    const RouteEdgeSet cave21Edges = routeEdges(state.route);
    assert(containsRouteEdge(cave21Edges, CaveId{7}, CaveId{16}));
    assert(containsRouteEdge(cave21Edges, CaveId{16}, CaveId{7}));
    assert(containsRouteEdge(cave21Edges, CaveId{16}, CaveId{21}));
    assert(!containsRouteEdge(cave21Edges, CaveId{7}, CaveId{12}));

    assert(selectRouteDestination(state, map, CaveId{12}));
    assert((state.route.caves == std::vector<CaveId>{7, 12}));
    const RouteEdgeSet cave12Edges = routeEdges(state.route);
    assert(containsRouteEdge(cave12Edges, CaveId{7}, CaveId{12}));
    assert(!containsRouteEdge(cave12Edges, CaveId{7}, CaveId{16}));
    assert(!containsRouteEdge(cave12Edges, CaveId{16}, CaveId{21}));
}

void destinationControlsMatchWebDebugGpsRules() {
    const PlayerMapView map{
        CaveId{1},
        {
            cave(1, {
                {TunnelId{1}, CaveId{2}},
                {TunnelId{2}, std::nullopt},
            }),
            cave(2, {
                {TunnelId{1}, CaveId{1}},
                {TunnelId{2}, CaveId{3}},
            }),
            cave(3, {{TunnelId{1}, CaveId{2}}}),
            cave(4, {}),
            cave(5, {{TunnelId{1}, std::nullopt}}),
        }};

    assert(destinationControlForCave(
               map, CaveId{1}, std::nullopt, false) == DestinationControl::None);
    assert(destinationControlForCave(
               map, CaveId{2}, std::nullopt, false) == DestinationControl::None);
    assert(destinationControlForCave(
               map, CaveId{3}, std::nullopt, false) == DestinationControl::Mark);
    assert(destinationControlForCave(
               map, CaveId{3}, std::nullopt, true) == DestinationControl::None);
    assert(destinationControlForCave(
               map, CaveId{4}, std::nullopt, false) == DestinationControl::None);
    assert(destinationControlForCave(
               map, CaveId{5}, std::nullopt, false) == DestinationControl::None);

    MapPresentationState state;
    assert(applyDestinationControl(
        state, map, CaveId{3}, DestinationControl::Mark));
    assert(state.routeDestination == CaveId{3});
    assert((state.route.caves == std::vector<CaveId>{1, 2, 3}));
    assert(destinationControlForCave(
               map, CaveId{3}, state.routeDestination, false) ==
        DestinationControl::Clear);
    assert(applyDestinationControl(
        state, map, CaveId{3}, DestinationControl::Clear));
    assert(!state.routeDestination.has_value());
    assert(state.route.caves.empty());
}

} // namespace

int main() {
    initialPlacementUsesCurrentCaveAsOrigin();
    existingCoordinatesStayStableAfterExpansion();
    unknownExitDirectionStaysStable();
    discoveredCaveFollowsPriorStubDirection();
    collisionResolutionContinuesAlongExitRay();
    disconnectedComponentsReceiveDeterministicPositions();
    undiscoveredEndpointIsIgnored();
    repeatedUpdateIsIdentical();
    framingContainsOnlyPlayerKnownPresentationPoints();
    hitTestingDistinguishesCavesAndUnknownExits();
    routeSelectionUsesOnlyDiscoveredKnownConnections();
    routeEdgesContainOnlyConsecutivePlanCaves();
    destinationControlsMatchWebDebugGpsRules();
    std::cout << "game map layout tests passed\n";
}
