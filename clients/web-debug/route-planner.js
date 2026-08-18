(function (root, factory) {
    const api = factory();
    if (typeof module === 'object' && module.exports) {
        module.exports = api;
    } else {
        root.BasiliskRoutePlanner = api;
    }
}(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';

    function caveId(cave) {
        return cave.id ?? cave.cave;
    }

    function buildKnownGraph(caves) {
        const knownCaves = Array.isArray(caves) ? caves : [];
        const caveIds = new Set(knownCaves.map(caveId));
        const graph = new Map(
            [...caveIds]
                .sort((a, b) => a - b)
                .map(id => [id, new Set()])
        );

        for (const cave of knownCaves) {
            const source = caveId(cave);
            if (!graph.has(source)) continue;

            for (const exit of cave.exits ?? []) {
                const destination = exit.destination;
                if (destination == null || !graph.has(destination)) continue;
                graph.get(source).add(destination);
                graph.get(destination).add(source);
            }
        }

        return new Map(
            [...graph.entries()].map(([id, neighbors]) => [
                id,
                [...neighbors].sort((a, b) => a - b)
            ])
        );
    }

    function shortestKnownRoute(caves, start, destination) {
        const graph = buildKnownGraph(caves);
        if (!graph.has(start) || !graph.has(destination)) return null;
        if (start === destination) return [start];

        const queue = [start];
        const previous = new Map([[start, null]]);

        for (let index = 0; index < queue.length; ++index) {
            const cave = queue[index];
            for (const neighbor of graph.get(cave)) {
                if (previous.has(neighbor)) continue;
                previous.set(neighbor, cave);
                if (neighbor === destination) {
                    const route = [destination];
                    let cursor = cave;
                    while (cursor != null) {
                        route.push(cursor);
                        cursor = previous.get(cursor);
                    }
                    return route.reverse();
                }
                queue.push(neighbor);
            }
        }

        return null;
    }

    function routeEdgeKey(a, b) {
        return `${Math.min(a, b)}-${Math.max(a, b)}`;
    }

    function routeEdgeKeys(route) {
        const keys = new Set();
        for (let index = 1; index < (route?.length ?? 0); ++index) {
            keys.add(routeEdgeKey(route[index - 1], route[index]));
        }
        return keys;
    }

    function planningActionLabel(
        caves,
        currentCave,
        targetCave,
        markedDestination,
        hasLegalActions
    ) {
        if (hasLegalActions || currentCave === targetCave) return null;
        const route = shortestKnownRoute(caves, currentCave, targetCave);
        if (!route || route.length <= 2) return null;
        return markedDestination === targetCave
            ? 'Clear Destination'
            : 'Mark Destination';
    }

    function createDestinationState() {
        const destinations = new Map();

        return Object.freeze({
            destinationFor(player) {
                return destinations.has(player) ? destinations.get(player) : null;
            },

            mark(player, cave) {
                destinations.set(player, cave);
            },

            clear(player) {
                destinations.delete(player);
            },

            routeFor(player, currentCave, caves) {
                const destination = destinations.get(player);
                if (destination == null) {
                    return {destination: null, route: null, reached: false};
                }
                if (destination === currentCave) {
                    destinations.delete(player);
                    return {destination: null, route: null, reached: true};
                }
                return {
                    destination,
                    route: shortestKnownRoute(caves, currentCave, destination),
                    reached: false
                };
            }
        });
    }

    return Object.freeze({
        buildKnownGraph,
        shortestKnownRoute,
        routeEdgeKey,
        routeEdgeKeys,
        planningActionLabel,
        createDestinationState
    });
}));
