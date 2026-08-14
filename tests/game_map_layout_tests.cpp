#include <cassert>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <optional>

#include "MapLayout.hpp"

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
    std::cout << "game map layout tests passed\n";
}
