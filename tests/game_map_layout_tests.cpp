#include <algorithm>
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <utility>

#include "MapLayout.hpp"
#include "MapPresentation.hpp"
#include "basilisk/world/MapGenerator.hpp"

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

PlayerMapView fullPhysicalMap(const MatchState& state) {
    PlayerMapView map;
    map.currentCave = state.world.caveIds().front();
    for (const CaveId caveId : state.world.caveIds()) {
        DiscoveredCaveView view;
        view.cave = caveId;
        const auto& connections = state.world.cave(caveId).connections;
        for (std::size_t index = 0; index < connections.size(); ++index) {
            view.exits.push_back(TunnelView{
                static_cast<TunnelId>(index + 1),
                connections[index],
            });
        }
        map.caves.push_back(std::move(view));
    }
    return map;
}

double minimumCaveSeparation(
    const PlayerMapLayout& layout,
    const PlayerMapView& map) {

    double minimum = std::numeric_limits<double>::max();
    for (std::size_t first = 0; first < map.caves.size(); ++first) {
        for (std::size_t second = first + 1; second < map.caves.size(); ++second) {
            minimum = std::min(
                minimum,
                std::sqrt(distanceSquared(
                    requireCave(layout, map.caves[first].cave),
                    requireCave(layout, map.caves[second].cave))));
        }
    }
    return minimum;
}

std::pair<double, double> connectedEdgeLengths(
    const PlayerMapLayout& layout,
    const PlayerMapView& map) {

    double total = 0.0;
    double maximum = 0.0;
    std::size_t count = 0;
    for (const DiscoveredCaveView& caveView : map.caves) {
        for (const TunnelView& exit : caveView.exits) {
            if (!exit.destination.has_value() ||
                caveView.cave >= *exit.destination) {
                continue;
            }
            const double length = std::sqrt(distanceSquared(
                requireCave(layout, caveView.cave),
                requireCave(layout, *exit.destination)));
            total += length;
            maximum = std::max(maximum, length);
            ++count;
        }
    }
    return {total / static_cast<double>(count), maximum};
}

using CaveEdge = std::pair<CaveId, CaveId>;

std::vector<CaveEdge> knownEdges(const PlayerMapView& map) {
    std::vector<CaveEdge> edges;
    for (const DiscoveredCaveView& caveView : map.caves) {
        for (const TunnelView& exit : caveView.exits) {
            if (exit.destination.has_value() &&
                caveView.cave < *exit.destination) {
                edges.emplace_back(caveView.cave, *exit.destination);
            }
        }
    }
    return edges;
}

double pointSegmentDistance(
    LogicalPoint point,
    LogicalPoint start,
    LogicalPoint end) {

    const LogicalPoint segment = subtract(end, start);
    const double lengthSquared = distanceSquared(start, end);
    if (lengthSquared <= kTolerance) {
        return std::sqrt(distanceSquared(point, start));
    }
    const double projection = std::clamp(
        dot(subtract(point, start), segment) / lengthSquared,
        0.0,
        1.0);
    const LogicalPoint closest{
        start.x + segment.x * projection,
        start.y + segment.y * projection,
    };
    return std::sqrt(distanceSquared(point, closest));
}

double minimumNonIncidentNodeEdgeDistance(
    const PlayerMapLayout& layout,
    const PlayerMapView& map) {

    double minimum = std::numeric_limits<double>::max();
    for (const CaveEdge& edge : knownEdges(map)) {
        const LogicalPoint start = requireCave(layout, edge.first);
        const LogicalPoint end = requireCave(layout, edge.second);
        for (const DiscoveredCaveView& caveView : map.caves) {
            if (caveView.cave == edge.first || caveView.cave == edge.second) {
                continue;
            }
            minimum = std::min(
                minimum,
                pointSegmentDistance(
                    requireCave(layout, caveView.cave), start, end));
        }
    }
    return minimum;
}

int orientation(LogicalPoint a, LogicalPoint b, LogicalPoint c) {
    const double value = cross(subtract(b, a), subtract(c, a));
    if (value > kTolerance) return 1;
    if (value < -kTolerance) return -1;
    return 0;
}

bool edgesCross(
    LogicalPoint firstStart,
    LogicalPoint firstEnd,
    LogicalPoint secondStart,
    LogicalPoint secondEnd) {

    const int a = orientation(firstStart, firstEnd, secondStart);
    const int b = orientation(firstStart, firstEnd, secondEnd);
    const int c = orientation(secondStart, secondEnd, firstStart);
    const int d = orientation(secondStart, secondEnd, firstEnd);
    return a * b < 0 && c * d < 0;
}

std::size_t edgeCrossingCount(
    const PlayerMapLayout& layout,
    const PlayerMapView& map) {

    const std::vector<CaveEdge> edges = knownEdges(map);
    std::size_t crossings = 0;
    for (std::size_t first = 0; first < edges.size(); ++first) {
        for (std::size_t second = first + 1; second < edges.size(); ++second) {
            const CaveEdge& a = edges[first];
            const CaveEdge& b = edges[second];
            if (a.first == b.first || a.first == b.second ||
                a.second == b.first || a.second == b.second) {
                continue;
            }
            if (edgesCross(
                    requireCave(layout, a.first),
                    requireCave(layout, a.second),
                    requireCave(layout, b.first),
                    requireCave(layout, b.second))) {
                ++crossings;
            }
        }
    }
    return crossings;
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

void denseFullMapLayoutIsDeterministicAndSeparated() {
    const MatchState state = MapGenerator::generate(
        MapSeed{20260816}, MatchSeed{424242});
    const PlayerMapView map = fullPhysicalMap(state);
    PlayerMapLayout raw;
    PlayerMapLayout first;
    PlayerMapLayout second;
    raw.update(map);
    first.update(map);
    first.finalizeFullLayout(map, 1.4, 2.5);
    second.update(map);
    second.finalizeFullLayout(map, 1.4, 2.5);

    for (const DiscoveredCaveView& caveView : map.caves) {
        assert(requireCave(first, caveView.cave) ==
               requireCave(second, caveView.cave));
    }
    const MapPresentationGeometry geometry = buildMapPresentationGeometry(
        map, first, {}, {0.0, 0.0, 1000.0, 700.0}, 50.0, 1.0);
    const auto [averageEdge, maximumEdge] = connectedEdgeLengths(first, map);
    const double minimum = minimumCaveSeparation(first, map);
    const double minimumNodeEdge =
        minimumNonIncidentNodeEdgeDistance(first, map);
    const std::size_t crossings = edgeCrossingCount(first, map);
    const std::size_t unrelaxedCrossings = edgeCrossingCount(raw, map);
    const LogicalBounds balancedBounds = first.positionedBounds();
    const double balancedAspect =
        (balancedBounds.maximumX - balancedBounds.minimumX) /
        (balancedBounds.maximumY - balancedBounds.minimumY);
    assert(std::abs(balancedAspect - 1.4) <= kTolerance);
    assert(minimum >= 4.0);
    assert(minimum * geometry.transform.pixelsPerLogicalUnit >= 35.0);
    assert(minimumNodeEdge >= 2.0);
    assert(crossings < unrelaxedCrossings);
    assert(crossings <= 30);
    assert(averageEdge <= 12.0);
    assert(maximumEdge <= 25.0);

}

void representativeGeneratedLayoutsMeetQualityThresholds() {
    constexpr std::array<MapSeed, 5> seeds{1, 57, 68, 71, 87};
    for (const MapSeed seed : seeds) {
        const MatchState state = MapGenerator::generate(
            seed, MatchSeed{424242});
        const PlayerMapView map = fullPhysicalMap(state);
        PlayerMapLayout first;
        PlayerMapLayout second;
        first.update(map);
        first.finalizeFullLayout(map);
        second.update(map);
        second.finalizeFullLayout(map);

        for (const DiscoveredCaveView& caveView : map.caves) {
            assert(requireCave(first, caveView.cave) ==
                   requireCave(second, caveView.cave));
        }

        const auto [averageEdge, maximumEdge] =
            connectedEdgeLengths(first, map);
        const LogicalBounds bounds = first.positionedBounds();
        const double aspect =
            (bounds.maximumX - bounds.minimumX) /
            (bounds.maximumY - bounds.minimumY);
        assert(minimumCaveSeparation(first, map) >= 4.0);
        assert(minimumNonIncidentNodeEdgeDistance(first, map) >= 2.0);
        assert(edgeCrossingCount(first, map) <= 30);
        assert(averageEdge <= 12.0);
        assert(maximumEdge <= 25.0);
        assert(std::abs(aspect - 1.4) <= 0.1);
    }
}

void fixedGeometryKeepsDiscoveryAndViewportStable() {
    const LogicalBounds fullBounds{-8.0, -6.0, 10.0, 7.0, true};
    PlayerFixedMapGeometry initial;
    initial.fullBounds = fullBounds;
    initial.discoveredCaves.emplace(CaveId{1}, LogicalPoint{0.0, 0.0});
    initial.unknownExitEndpoints.emplace(
        MapExitKey{CaveId{1}, TunnelId{1}}, LogicalPoint{4.0, 2.0});

    PlayerMapLayout layout;
    layout.updateFixed(initial);
    const LogicalPoint caveOneBefore = requireCave(layout, CaveId{1});
    const LogicalPoint unknownBefore = requireStub(
        layout, CaveId{1}, TunnelId{1});
    const PlayerMapView before{
        CaveId{1},
        {cave(1, {{TunnelId{1}, std::nullopt}})}};
    const MapPresentationGeometry frameBefore = buildMapPresentationGeometry(
        before, layout, {}, {0.0, 0.0, 900.0, 600.0}, 50.0, 1.0);

    PlayerFixedMapGeometry expanded;
    expanded.fullBounds = fullBounds;
    expanded.discoveredCaves.emplace(CaveId{1}, LogicalPoint{0.0, 0.0});
    expanded.discoveredCaves.emplace(CaveId{2}, unknownBefore);
    layout.updateFixed(expanded);
    const PlayerMapView after{
        CaveId{2},
        {
            cave(1, {{TunnelId{1}, CaveId{2}}}),
            cave(2, {{TunnelId{1}, CaveId{1}}}),
        }};
    const MapPresentationGeometry frameAfter = buildMapPresentationGeometry(
        after, layout, {}, {0.0, 0.0, 900.0, 600.0}, 50.0, 1.0);

    assert(requireCave(layout, CaveId{1}) == caveOneBefore);
    assert(requireCave(layout, CaveId{2}) == unknownBefore);
    assert(layout.fixedBounds() == fullBounds);
    assert(frameBefore.transform.logicalCenter == frameAfter.transform.logicalCenter);
    assert(frameBefore.transform.pixelsPerLogicalUnit ==
           frameAfter.transform.pixelsPerLogicalUnit);
}

void fixedPitRevealOverlaysUnknownEndpointThenExpires() {
    const LogicalPoint caveSixPosition{-4.0, 1.0};
    const LogicalPoint hiddenCaveSevenPosition{5.0, -2.0};
    PlayerFixedMapGeometry fixed;
    fixed.fullBounds = {-12.0, -8.0, 12.0, 8.0, true};
    fixed.discoveredCaves.emplace(CaveId{6}, caveSixPosition);
    fixed.unknownExitEndpoints.emplace(
        MapExitKey{CaveId{6}, TunnelId{2}}, hiddenCaveSevenPosition);

    PlayerMapLayout layout;
    layout.updateFixed(fixed);
    const PlayerMapView playerMap{
        CaveId{6},
        {DiscoveredCaveView{
            CaveId{6},
            {TunnelView{TunnelId{2}, std::nullopt, true}},
        }},
    };
    const MapPresentationGeometry hidden = buildMapPresentationGeometry(
        playerMap,
        layout,
        {},
        {0.0, 0.0, 900.0, 600.0},
        40.0,
        1.0);

    assert(!layout.cavePosition(CaveId{7}).has_value());
    assert(requireStub(layout, CaveId{6}, TunnelId{2}) ==
           hiddenCaveSevenPosition);
    assert(playerMap.caves.front().exits.front().strongColdDraft);
    assert(hidden.temporaryPitPositions.empty());
    const MapHitTarget hit = hitTestPlayerKnownMap(
        playerMap,
        layout,
        hidden,
        projectMapPoint(hiddenCaveSevenPosition, hidden.transform));
    assert(hit.kind == MapHitKind::UnknownExit);
    assert((hit.unknownExit ==
            UnknownExitKey{CaveId{6}, TunnelId{2}}));
    assert(!hit.cave.has_value());

    PlayerFixedMapGeometry revealed = fixed;
    revealed.temporarilyRevealedCaves.emplace(
        CaveId{7}, hiddenCaveSevenPosition);
    layout.updateFixed(revealed);
    const MapPresentationGeometry visible = buildMapPresentationGeometry(
        playerMap,
        layout,
        {CaveId{7}},
        {0.0, 0.0, 900.0, 600.0},
        40.0,
        1.0);
    assert(!layout.cavePosition(CaveId{7}).has_value());
    assert(visible.temporaryPitPositions.at(CaveId{7}) ==
           hiddenCaveSevenPosition);
    assert(visible.transform.logicalCenter == hidden.transform.logicalCenter);
    assert(visible.transform.pixelsPerLogicalUnit ==
           hidden.transform.pixelsPerLogicalUnit);

    layout.updateFixed(fixed);
    const MapPresentationGeometry expired = buildMapPresentationGeometry(
        playerMap,
        layout,
        {},
        {0.0, 0.0, 900.0, 600.0},
        40.0,
        1.0);
    assert(expired.temporaryPitPositions.empty());
    assert(!layout.temporarilyRevealedCavePosition(CaveId{7}).has_value());
    assert(!layout.cavePosition(CaveId{7}).has_value());
    assert(requireStub(layout, CaveId{6}, TunnelId{2}) ==
           hiddenCaveSevenPosition);
    assert(expired.transform.logicalCenter == hidden.transform.logicalCenter);
    assert(expired.transform.pixelsPerLogicalUnit ==
           hidden.transform.pixelsPerLogicalUnit);
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

void actionHoverMapsOnlyLiteralSpatialTargets() {
    AvailableAction moveKnown;
    moveKnown.type = ActionType::Move;
    moveKnown.targetCave = CaveId{12};
    const MapHitTarget moveKnownHit = mapHoverTargetForAction(
        moveKnown, CaveId{7});
    assert(moveKnownHit.kind == MapHitKind::DiscoveredCave);
    assert(moveKnownHit.cave == CaveId{12});
    assert(!moveKnownHit.unknownExit.has_value());

    AvailableAction shootKnown = moveKnown;
    shootKnown.type = ActionType::Shoot;
    assert(mapHoverTargetForAction(shootKnown, CaveId{7}).cave ==
           CaveId{12});

    AvailableAction enterUnknown;
    enterUnknown.type = ActionType::Move;
    enterUnknown.targetTunnel = TunnelId{4};
    const MapHitTarget enterUnknownHit = mapHoverTargetForAction(
        enterUnknown, CaveId{7});
    assert(enterUnknownHit.kind == MapHitKind::UnknownExit);
    assert((enterUnknownHit.unknownExit ==
            UnknownExitKey{CaveId{7}, TunnelId{4}}));
    assert(!enterUnknownHit.cave.has_value());

    AvailableAction shootUnknown = enterUnknown;
    shootUnknown.type = ActionType::Shoot;
    assert((mapHoverTargetForAction(shootUnknown, CaveId{9}).unknownExit ==
            UnknownExitKey{CaveId{9}, TunnelId{4}}));

    AvailableAction survey = enterUnknown;
    survey.type = ActionType::UseItem;
    survey.targetItem = ItemType::SurveyFragment;
    assert((mapHoverTargetForAction(survey, CaveId{7}).unknownExit ==
            UnknownExitKey{CaveId{7}, TunnelId{4}}));

    AvailableAction search;
    search.type = ActionType::Search;
    assert(mapHoverTargetForAction(search, CaveId{7}).kind ==
           MapHitKind::None);
    AvailableAction nonSpatialItem;
    nonSpatialItem.type = ActionType::UseItem;
    nonSpatialItem.targetItem = ItemType::OldHuntersMap;
    assert(mapHoverTargetForAction(nonSpatialItem, CaveId{7}).kind ==
           MapHitKind::None);
}

void overlappingUnknownExitPrefersCurrentCaveProvenance() {
    PlayerMapLayout layout;
    PlayerFixedMapGeometry fixed;
    fixed.fullBounds = {-20.0, -20.0, 40.0, 20.0, true};
    fixed.discoveredCaves.emplace(CaveId{18}, LogicalPoint{-10.0, 0.0});
    fixed.discoveredCaves.emplace(CaveId{19}, LogicalPoint{10.0, 0.0});
    const LogicalPoint sharedEndpoint{0.0, 10.0};
    fixed.unknownExitEndpoints.emplace(
        MapExitKey{CaveId{18}, TunnelId{6}}, sharedEndpoint);
    fixed.unknownExitEndpoints.emplace(
        MapExitKey{CaveId{19}, TunnelId{6}}, sharedEndpoint);
    layout.updateFixed(fixed);

    const auto mapFrom = [](CaveId current) {
        return PlayerMapView{
            current,
            {
                cave(18, {{TunnelId{6}, std::nullopt}}),
                cave(19, {{TunnelId{6}, std::nullopt}}),
            }};
    };
    const PlayerMapView fromA = mapFrom(CaveId{18});
    const MapPresentationGeometry geometry = buildMapPresentationGeometry(
        fromA, layout, {}, {0.0, 0.0, 800.0, 600.0}, 40.0, 1.0);
    const PresentationPoint pointer = projectMapPoint(
        sharedEndpoint, geometry.transform);

    const MapHitTarget hitFromA = hitTestPlayerKnownMap(
        fromA, layout, geometry, pointer);
    assert(hitFromA.kind == MapHitKind::UnknownExit);
    assert((hitFromA.unknownExit ==
            UnknownExitKey{CaveId{18}, TunnelId{6}}));

    const PlayerMapView fromB = mapFrom(CaveId{19});
    const MapHitTarget hitFromB = hitTestPlayerKnownMap(
        fromB, layout, geometry, pointer);
    assert(hitFromB.kind == MapHitKind::UnknownExit);
    assert((hitFromB.unknownExit ==
            UnknownExitKey{CaveId{19}, TunnelId{6}}));
}

void routeSelectionUsesOnlyDiscoveredKnownConnections() {
    PlayerMapLayout layout;
    const PlayerMapView map = presentationMap();
    layout.update(map);
    MapPresentationState state;

    assert(selectRouteDestination(state, map, CaveId{3}));
    const auto route = selectedRoute(state, map);
    assert(route.status == client_navigation::KnownRouteStatus::Reachable);
    assert((route.caves == std::vector<CaveId>{1, 2, 3}));

    assert(!selectRouteDestination(state, map, CaveId{99}));
    assert(!selectRouteFromHit(
        state,
        map,
        MapHitTarget{
            MapHitKind::UnknownExit,
            std::nullopt,
            UnknownExitKey{CaveId{1}, TunnelId{2}}}));

    assert(!selectRouteDestination(state, map, CaveId{8}));
    assert(selectedRoute(state, map).caves.empty());

    assert(selectRouteDestination(state, map, CaveId{1}));
    assert(!state.routeDestination.has_value());
    assert(selectedRoute(state, map).caves.empty());
}

void snapshotMapRefreshClearsInvalidDestination() {
    const PlayerMapView connected{
        CaveId{1},
        {
            cave(1, {{TunnelId{1}, CaveId{2}}}),
            cave(2, {
                {TunnelId{1}, CaveId{1}},
                {TunnelId{2}, CaveId{3}},
            }),
            cave(3, {{TunnelId{1}, CaveId{2}}}),
        }};
    MapPresentationState state;
    assert(selectRouteDestination(state, connected, CaveId{3}));

    const PlayerMapView destinationNoLongerVisible{
        CaveId{2},
        {
            cave(1, {{TunnelId{1}, CaveId{2}}}),
            cave(2, {{TunnelId{1}, CaveId{1}}}),
        }};
    refreshSelectedRoute(state, destinationNoLongerVisible);
    assert(!state.routeDestination.has_value());
    assert(selectedRoute(state, destinationNoLongerVisible).caves.empty());
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
    const auto cave21Route = selectedRoute(state, map);
    assert((cave21Route.caves == std::vector<CaveId>{7, 16, 21}));
    const RouteEdgeSet cave21Edges = routeEdges(cave21Route);
    assert(containsRouteEdge(cave21Edges, CaveId{7}, CaveId{16}));
    assert(containsRouteEdge(cave21Edges, CaveId{16}, CaveId{7}));
    assert(containsRouteEdge(cave21Edges, CaveId{16}, CaveId{21}));
    assert(!containsRouteEdge(cave21Edges, CaveId{7}, CaveId{12}));

    assert(selectRouteDestination(state, map, CaveId{12}));
    const auto cave12Route = selectedRoute(state, map);
    assert((cave12Route.caves == std::vector<CaveId>{7, 12}));
    const RouteEdgeSet cave12Edges = routeEdges(cave12Route);
    assert(containsRouteEdge(cave12Edges, CaveId{7}, CaveId{12}));
    assert(!containsRouteEdge(cave12Edges, CaveId{7}, CaveId{16}));
    assert(!containsRouteEdge(cave12Edges, CaveId{16}, CaveId{21}));
}

void selectedRouteImmediatelyUsesNewKnownShortcut() {
    const PlayerMapView longRoute{
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
            cave(3, {
                {TunnelId{1}, CaveId{2}},
                {TunnelId{2}, CaveId{4}},
            }),
            cave(4, {
                {TunnelId{1}, CaveId{3}},
                {TunnelId{2}, std::nullopt},
            }),
        }};
    MapPresentationState state;
    assert(selectRouteDestination(state, longRoute, CaveId{4}));
    const auto initial = selectedRoute(state, longRoute);
    assert((initial.caves == std::vector<CaveId>{1, 2, 3, 4}));
    assert(!containsRouteEdge(routeEdges(initial), CaveId{1}, CaveId{4}));

    const PlayerMapView shortcutKnown{
        CaveId{1},
        {
            cave(1, {
                {TunnelId{1}, CaveId{2}},
                {TunnelId{2}, CaveId{4}},
            }),
            cave(2, {
                {TunnelId{1}, CaveId{1}},
                {TunnelId{2}, CaveId{3}},
            }),
            cave(3, {
                {TunnelId{1}, CaveId{2}},
                {TunnelId{2}, CaveId{4}},
            }),
            cave(4, {
                {TunnelId{1}, CaveId{3}},
                {TunnelId{2}, CaveId{1}},
            }),
        }};
    const auto refreshed = selectedRoute(state, shortcutKnown);
    assert((refreshed.caves == std::vector<CaveId>{1, 4}));
    const RouteEdgeSet refreshedEdges = routeEdges(refreshed);
    assert(containsRouteEdge(refreshedEdges, CaveId{1}, CaveId{4}));
    assert(!containsRouteEdge(refreshedEdges, CaveId{1}, CaveId{2}));
    assert(!containsRouteEdge(refreshedEdges, CaveId{2}, CaveId{3}));
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
    assert((selectedRoute(state, map).caves == std::vector<CaveId>{1, 2, 3}));
    assert(destinationControlForCave(
               map, CaveId{3}, state.routeDestination, false) ==
        DestinationControl::Clear);
    assert(applyDestinationControl(
        state, map, CaveId{3}, DestinationControl::Clear));
    assert(!state.routeDestination.has_value());
    assert(selectedRoute(state, map).caves.empty());
}

void gpsEligibilityRecalculatesAfterRelocation() {
    const auto relocatedMap = [](CaveId current) {
        return PlayerMapView{
            current,
            {
                cave(1, {{TunnelId{1}, CaveId{2}}}),
                cave(2, {
                    {TunnelId{1}, CaveId{1}},
                    {TunnelId{2}, CaveId{3}},
                    {TunnelId{3}, CaveId{5}},
                }),
                cave(3, {
                    {TunnelId{1}, CaveId{2}},
                    {TunnelId{2}, CaveId{4}},
                }),
                cave(4, {{TunnelId{1}, CaveId{3}}}),
                cave(5, {
                    {TunnelId{1}, CaveId{2}},
                    {TunnelId{2}, std::nullopt},
                }),
                cave(6, {{TunnelId{1}, std::nullopt}}),
            }};
    };

    const PlayerMapView before = relocatedMap(CaveId{1});
    MapPresentationState state;
    assert(applyDestinationControl(
        state, before, CaveId{4}, DestinationControl::Mark));
    assert((selectedRoute(state, before).caves ==
            std::vector<CaveId>{1, 2, 3, 4}));
    assert(destinationControlForCave(
               before, CaveId{3}, state.routeDestination, false) ==
        DestinationControl::Mark);

    // A relocation changes only currentCave. GPS eligibility and the active
    // route must be derived afresh from that latest player-safe map.
    const PlayerMapView after = relocatedMap(CaveId{5});
    assert((selectedRoute(state, after).caves ==
            std::vector<CaveId>{5, 2, 3, 4}));
    assert(destinationControlForCave(
               after, CaveId{5}, state.routeDestination, false) ==
        DestinationControl::None);
    assert(destinationControlForCave(
               after, CaveId{2}, state.routeDestination, false) ==
        DestinationControl::None);
    assert(destinationControlForCave(
               after, CaveId{1}, state.routeDestination, false) ==
        DestinationControl::Mark);
    assert(destinationControlForCave(
               after, CaveId{3}, state.routeDestination, false) ==
        DestinationControl::Mark);
    assert(destinationControlForCave(
               after, CaveId{3}, state.routeDestination, true) ==
        DestinationControl::None);
    assert(destinationControlForCave(
               after, CaveId{4}, state.routeDestination, false) ==
        DestinationControl::Clear);
    assert(destinationControlForCave(
               after, CaveId{6}, state.routeDestination, false) ==
        DestinationControl::None);
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
    denseFullMapLayoutIsDeterministicAndSeparated();
    representativeGeneratedLayoutsMeetQualityThresholds();
    fixedGeometryKeepsDiscoveryAndViewportStable();
    fixedPitRevealOverlaysUnknownEndpointThenExpires();
    framingContainsOnlyPlayerKnownPresentationPoints();
    hitTestingDistinguishesCavesAndUnknownExits();
    actionHoverMapsOnlyLiteralSpatialTargets();
    overlappingUnknownExitPrefersCurrentCaveProvenance();
    routeSelectionUsesOnlyDiscoveredKnownConnections();
    routeEdgesContainOnlyConsecutivePlanCaves();
    selectedRouteImmediatelyUsesNewKnownShortcut();
    destinationControlsMatchWebDebugGpsRules();
    gpsEligibilityRecalculatesAfterRelocation();
    snapshotMapRefreshClearsInvalidDestination();
    std::cout << "game map layout tests passed\n";
}
