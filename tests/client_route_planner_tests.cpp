#include <cassert>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <vector>

#include "basilisk/client/RoutePlanner.hpp"

using namespace basilisk;
using namespace basilisk::client_navigation;

namespace {

DiscoveredCaveView cave(
    CaveId id,
    std::initializer_list<std::optional<CaveId>> destinations) {

    DiscoveredCaveView result;
    result.cave = id;
    TunnelId tunnel = 1;
    for (const auto destination : destinations) {
        result.exits.push_back(TunnelView{tunnel, destination});
        ++tunnel;
    }
    return result;
}

void directRouteUsesKnownConnection() {
    const PlayerMapView map{
        CaveId{1},
        {cave(1, {CaveId{2}, CaveId{2}}), cave(2, {})}};

    const auto route = planKnownRoute(map, CaveId{2});
    assert(route.status == KnownRouteStatus::Reachable);
    assert((route.caves == std::vector<CaveId>{1, 2}));
}

void multiHopRouteIsShortestAndDeterministic() {
    const PlayerMapView map{
        CaveId{1},
        {
            cave(1, {CaveId{3}, CaveId{2}}),
            cave(2, {CaveId{1}, CaveId{4}}),
            cave(3, {CaveId{1}, CaveId{4}}),
            cave(4, {CaveId{2}, CaveId{3}, CaveId{5}}),
            cave(5, {CaveId{4}}),
        }};

    const auto route = planKnownRoute(map, CaveId{5});
    assert(route.status == KnownRouteStatus::Reachable);
    assert((route.caves == std::vector<CaveId>{1, 2, 4, 5}));
}

void unknownExitsAreExcluded() {
    const PlayerMapView map{
        CaveId{1},
        {cave(1, {std::nullopt}), cave(2, {std::nullopt})}};

    const auto route = planKnownRoute(map, CaveId{2});
    assert(route.status == KnownRouteStatus::Unreachable);
    assert(route.caves.empty());
}

void undiscoveredShortcutCannotEnterKnownGraph() {
    const PlayerMapView map{
        CaveId{1},
        {
            cave(1, {CaveId{2}, CaveId{9}}),
            cave(2, {CaveId{1}, CaveId{3}}),
            cave(3, {CaveId{2}, CaveId{4}}),
            cave(4, {CaveId{3}, CaveId{9}}),
        }};

    const auto route = planKnownRoute(map, CaveId{4});
    assert(route.status == KnownRouteStatus::Reachable);
    assert((route.caves == std::vector<CaveId>{1, 2, 3, 4}));
}

void undiscoveredDestinationIsRejected() {
    const PlayerMapView map{CaveId{1}, {cave(1, {std::nullopt})}};

    const auto route = planKnownRoute(map, CaveId{7});
    assert(route.status == KnownRouteStatus::DestinationUndiscovered);
    assert(route.caves.empty());
}

void disconnectedDestinationIsRejected() {
    const PlayerMapView map{
        CaveId{1},
        {cave(1, {CaveId{2}}), cave(2, {CaveId{1}}), cave(8, {})}};

    const auto route = planKnownRoute(map, CaveId{8});
    assert(route.status == KnownRouteStatus::Unreachable);
    assert(route.caves.empty());
}

void routeRecalculatesAfterMapExpansion() {
    PlayerMapView map{
        CaveId{1},
        {cave(1, {CaveId{2}}), cave(2, {CaveId{1}}), cave(4, {})}};

    const auto before = planKnownRoute(map, CaveId{4});
    assert(before.status == KnownRouteStatus::Unreachable);

    map.caves[1].exits.push_back(TunnelView{TunnelId{2}, CaveId{3}});
    map.caves.push_back(cave(3, {CaveId{2}, CaveId{4}}));

    const auto after = planKnownRoute(map, CaveId{4});
    assert(after.status == KnownRouteStatus::Reachable);
    assert((after.caves == std::vector<CaveId>{1, 2, 3, 4}));
}

void arrivalSignalsCallerToClearDestination() {
    const PlayerMapView map{CaveId{4}, {cave(4, {})}};

    const auto route = planKnownRoute(map, CaveId{4});
    assert(route.status == KnownRouteStatus::Arrived);
    assert(route.arrived());
    assert((route.caves == std::vector<CaveId>{4}));
}

} // namespace

int main() {
    directRouteUsesKnownConnection();
    multiHopRouteIsShortestAndDeterministic();
    unknownExitsAreExcluded();
    undiscoveredShortcutCannotEnterKnownGraph();
    undiscoveredDestinationIsRejected();
    disconnectedDestinationIsRejected();
    routeRecalculatesAfterMapExpansion();
    arrivalSignalsCallerToClearDestination();
    std::cout << "client route planner tests passed\n";
}
