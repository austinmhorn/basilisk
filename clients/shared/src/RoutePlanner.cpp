#include "basilisk/client/RoutePlanner.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <utility>

namespace basilisk::client_navigation {
namespace {

using KnownGraph = std::map<CaveId, std::set<CaveId>>;

KnownGraph buildKnownGraph(const PlayerMapView& map) {
    KnownGraph graph;
    for (const auto& cave : map.caves) graph.try_emplace(cave.cave);

    for (const auto& cave : map.caves) {
        for (const auto& exit : cave.exits) {
            if (!exit.destination.has_value()) continue;

            const CaveId destination = *exit.destination;
            if (!graph.contains(destination)) continue;

            // TunnelId is local to its source cave. Known paths are instead
            // identified by their discovered CaveId endpoints.
            graph[cave.cave].insert(destination);
            graph[destination].insert(cave.cave);
        }
    }

    return graph;
}

} // namespace

KnownRoutePlan planKnownRoute(const PlayerMapView& map, CaveId destination) {
    const KnownGraph graph = buildKnownGraph(map);
    if (!graph.contains(destination)) {
        return {KnownRouteStatus::DestinationUndiscovered, {}};
    }
    if (!graph.contains(map.currentCave)) {
        return {KnownRouteStatus::Unreachable, {}};
    }
    if (map.currentCave == destination) {
        return {KnownRouteStatus::Arrived, {map.currentCave}};
    }

    std::queue<CaveId> pending;
    std::map<CaveId, CaveId> previous;
    std::set<CaveId> visited;

    pending.push(map.currentCave);
    visited.insert(map.currentCave);

    while (!pending.empty()) {
        const CaveId cave = pending.front();
        pending.pop();

        for (const CaveId neighbor : graph.at(cave)) {
            if (!visited.insert(neighbor).second) continue;
            previous.emplace(neighbor, cave);

            if (neighbor == destination) {
                std::vector<CaveId> route{destination};
                CaveId cursor = destination;
                while (cursor != map.currentCave) {
                    cursor = previous.at(cursor);
                    route.push_back(cursor);
                }
                std::reverse(route.begin(), route.end());
                return {KnownRouteStatus::Reachable, std::move(route)};
            }

            pending.push(neighbor);
        }
    }

    return {KnownRouteStatus::Unreachable, {}};
}

} // namespace basilisk::client_navigation
