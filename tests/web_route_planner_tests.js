'use strict';

const assert = require('node:assert/strict');
const {
    buildKnownGraph,
    shortestKnownRoute,
    planningActionLabel,
    createDestinationState
} = require('../clients/web-debug/route-planner.js');

function cave(id, destinations) {
    return {
        id,
        exits: destinations.map(destination => ({destination}))
    };
}

function shortestKnownRouteWins() {
    const caves = [
        cave(1, [2, 3]),
        cave(2, [1, 4]),
        cave(3, [1, 5]),
        cave(4, [2, 5]),
        cave(5, [3, 4])
    ];

    assert.deepEqual(shortestKnownRoute(caves, 1, 5), [1, 3, 5]);
}

function unknownExitsAreExcluded() {
    const caves = [
        cave(1, [2, null]),
        cave(2, [1, null]),
        cave(9, [null])
    ];
    const graph = buildKnownGraph(caves);

    assert.deepEqual(graph.get(1), [2]);
    assert.deepEqual(graph.get(2), [1]);
    assert.deepEqual(graph.get(9), []);
    assert.equal(shortestKnownRoute(caves, 1, 9), null);
}

function truthOnlyShortcutIsExcluded() {
    const playerCaves = [
        cave(1, [2]),
        cave(2, [1, 3]),
        cave(3, [2, 4]),
        cave(4, [3])
    ];
    const debugTruthOnlyShortcut = [
        cave(1, [2, 9]),
        cave(9, [1, 4]),
        cave(4, [3, 9])
    ];

    assert.ok(debugTruthOnlyShortcut.some(entry => entry.id === 9));
    assert.deepEqual(shortestKnownRoute(playerCaves, 1, 4), [1, 2, 3, 4]);
}

function equalRoutesUseAscendingCaveIds() {
    const caves = [
        cave(4, [3, 2]),
        cave(3, [4, 1]),
        cave(2, [4, 1]),
        cave(1, [3, 2])
    ];

    assert.deepEqual(shortestKnownRoute(caves, 1, 4), [1, 2, 4]);
}

function destinationStateIsLocalAndReplaceable() {
    const state = createDestinationState();
    const snapshot = {player: 1, round: 7, actions: [{index: 1}]};
    const unchangedSnapshot = JSON.parse(JSON.stringify(snapshot));

    state.mark(1, 8);
    assert.equal(state.destinationFor(1), 8);
    state.mark(1, 12);
    assert.equal(state.destinationFor(1), 12);
    assert.equal(state.destinationFor(2), null);
    state.clear(1);
    assert.equal(state.destinationFor(1), null);
    assert.deepEqual(snapshot, unchangedSnapshot);
}

function routeRecalculatesAndClearsOnArrival() {
    const state = createDestinationState();
    const caves = [
        cave(1, [2]),
        cave(2, [1, 3, 5]),
        cave(3, [2, 4]),
        cave(4, [3, 5]),
        cave(5, [2, 4])
    ];

    state.mark(1, 4);
    assert.deepEqual(state.routeFor(1, 1, caves).route, [1, 2, 3, 4]);
    assert.deepEqual(state.routeFor(1, 5, caves).route, [5, 4]);
    assert.equal(state.routeFor(1, 4, caves).reached, true);
    assert.equal(state.destinationFor(1), null);
}

function planningDoesNotReplaceCurrentOrAdjacentActions() {
    const caves = [
        cave(1, [2]),
        cave(2, [1, 3]),
        cave(3, [2])
    ];

    assert.equal(planningActionLabel(caves, 1, 1, null, false), null);
    assert.equal(planningActionLabel(caves, 1, 2, null, false), null);
    assert.equal(planningActionLabel(caves, 1, 3, null, true), null);
    assert.equal(planningActionLabel(caves, 1, 3, null, false), 'Mark Destination');
    assert.equal(planningActionLabel(caves, 1, 3, 3, false), 'Clear Destination');
}

shortestKnownRouteWins();
unknownExitsAreExcluded();
truthOnlyShortcutIsExcluded();
equalRoutesUseAscendingCaveIds();
destinationStateIsLocalAndReplaceable();
routeRecalculatesAndClearsOnArrival();
planningDoesNotReplaceCurrentOrAdjacentActions();

console.log('Basilisk web route planner tests passed.');
