#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

std::set<CaveId> connectionsOf(const MatchState& state, CaveId cave) {
    const auto& connections = state.world.cave(cave).connections;
    return std::set<CaveId>(connections.begin(), connections.end());
}

void mixSignatureWord(std::uint64_t& signature, std::uint64_t value) {
    constexpr std::uint64_t fnvPrime = 1099511628211ULL;
    // Explicit byte order keeps the signature independent of host endianness.
    for (int byte = 0; byte < 8; ++byte) {
        signature ^= value & 0xffU;
        signature *= fnvPrime;
        value >>= 8U;
    }
}

std::uint64_t generatedWorldSignature(const MatchState& state) {
    std::uint64_t signature = 14695981039346656037ULL;
    const std::vector<CaveId> caves = state.world.caveIds();
    mixSignatureWord(signature, caves.size());
    for (const CaveId cave : caves) {
        std::vector<CaveId> connections = state.world.cave(cave).connections;
        std::sort(connections.begin(), connections.end());
        mixSignatureWord(signature, cave);
        mixSignatureWord(signature, connections.size());
        for (const CaveId destination : connections) {
            mixSignatureWord(signature, destination);
        }
    }

    mixSignatureWord(signature, state.players.size());
    for (const PlayerState& player : state.players) {
        mixSignatureWord(signature, player.id);
        mixSignatureWord(signature, player.cave);
    }
    mixSignatureWord(signature, state.basilisk.cave);

    std::vector<CaveId> pits;
    for (const PitState& pit : state.pits) pits.push_back(pit.cave);
    std::sort(pits.begin(), pits.end());
    mixSignatureWord(signature, pits.size());
    for (const CaveId cave : pits) mixSignatureWord(signature, cave);

    std::vector<CaveId> jackals;
    for (const JackalState& jackal : state.jackals) {
        jackals.push_back(jackal.cave);
    }
    std::sort(jackals.begin(), jackals.end());
    mixSignatureWord(signature, jackals.size());
    for (const CaveId cave : jackals) mixSignatureWord(signature, cave);
    return signature;
}

int distanceBetween(const WorldGraph& world, CaveId start, CaveId target) {
    std::queue<CaveId> frontier;
    std::unordered_map<CaveId, int> distance;
    frontier.push(start);
    distance.emplace(start, 0);

    while (!frontier.empty()) {
        const CaveId current = frontier.front();
        frontier.pop();
        if (current == target) return distance.at(current);

        for (const CaveId next : world.cave(current).connections) {
            if (distance.contains(next)) continue;
            distance.emplace(next, distance.at(current) + 1);
            frontier.push(next);
        }
    }

    return -1;
}

bool nonPitCavesRemainConnected(const MatchState& state) {
    std::unordered_set<CaveId> blocked;
    for (const auto& pit : state.pits) {
        if (pit.active) blocked.insert(pit.cave);
    }

    const auto ids = state.world.caveIds();
    const auto start = std::find_if(ids.begin(), ids.end(), [&](CaveId cave) {
        return !blocked.contains(cave);
    });
    if (start == ids.end()) return true;

    std::queue<CaveId> frontier;
    std::unordered_set<CaveId> visited;
    frontier.push(*start);
    visited.insert(*start);

    while (!frontier.empty()) {
        const CaveId current = frontier.front();
        frontier.pop();
        for (const CaveId next : state.world.cave(current).connections) {
            if (blocked.contains(next) || visited.contains(next)) continue;
            visited.insert(next);
            frontier.push(next);
        }
    }

    std::size_t remaining = 0;
    for (const CaveId cave : ids) {
        if (!blocked.contains(cave)) ++remaining;
    }
    return visited.size() == remaining;
}

void assertPlacementQuality(const MatchState& state, const ProceduralMapConfig& config) {
    assert(state.players.size() == 2);

    for (const auto& player : state.players) {
        if (!config.allowHunterSpawnInDeadEnd) {
            assert(state.world.cave(player.cave).connections.size() > 1);
        }

        for (const auto& pit : state.pits) {
            assert(distanceBetween(state.world, player.cave, pit.cave) >=
                   config.minHunterPitDistance);
        }
        for (const auto& jackal : state.jackals) {
            assert(distanceBetween(state.world, player.cave, jackal.cave) >=
                   config.minHunterJackalDistance);
        }
    }

    if (!config.allowBasiliskInDeadEnd) {
        assert(state.world.cave(state.basilisk.cave).connections.size() > 1);
    }

    for (const auto& pit : state.pits) {
        assert(distanceBetween(state.world, state.basilisk.cave, pit.cave) >=
               config.minBasiliskPitDistance);
    }

    for (std::size_t i = 0; i < state.pits.size(); ++i) {
        for (std::size_t j = i + 1; j < state.pits.size(); ++j) {
            assert(distanceBetween(state.world, state.pits[i].cave, state.pits[j].cave) >=
                   config.minPitSeparation);
        }
    }

    if (config.preventPitsBlockingRegions) {
        assert(nonPitCavesRemainConnected(state));
    }

    for (std::size_t i = 0; i < state.jackals.size(); ++i) {
        for (std::size_t j = i + 1; j < state.jackals.size(); ++j) {
            assert(distanceBetween(state.world, state.jackals[i].cave, state.jackals[j].cave) >=
                   config.minJackalSeparation);
        }
    }
}

void assertSameGeneratedWorld(const MatchState& a, const MatchState& b) {
    assert(a.world.size() == b.world.size());
    assert(a.world.caveIds() == b.world.caveIds());

    for (const CaveId cave : a.world.caveIds()) {
        assert(connectionsOf(a, cave) == connectionsOf(b, cave));
    }

    assert(a.players.size() == b.players.size());
    for (std::size_t i = 0; i < a.players.size(); ++i) {
        assert(a.players[i].id == b.players[i].id);
        assert(a.players[i].cave == b.players[i].cave);
    }

    assert(a.basilisk.cave == b.basilisk.cave);
    assert(a.pits.size() == b.pits.size());
    assert(a.jackals.size() == b.jackals.size());

    for (std::size_t i = 0; i < a.pits.size(); ++i) {
        assert(a.pits[i].cave == b.pits[i].cave);
    }
    for (std::size_t i = 0; i < a.jackals.size(); ++i) {
        assert(a.jackals[i].cave == b.jackals[i].cave);
    }
}

void defaultThirtyCaveMapHasExpectedShapeAndActors() {
    const auto state = MapGenerator::generate(12345, 67890);
    const ProceduralMapConfig config;
    const auto metrics = MapGenerator::analyzeTopology(state.world);

    assert(state.world.size() == 30);
    assert(MapGenerator::validateTopology(state.world, config));
    assert(MapGenerator::validateFairness(state, config));
    assert(state.rules.mapDiscoveryMode == MapDiscoveryMode::FogOfWar);
    assertPlacementQuality(state, config);

    assert(metrics.deadEndCount >= config.minDeadEnds);
    assert(metrics.deadEndCount <= config.maxDeadEnds);
    assert(metrics.loopCount >= config.minLoopCount);
    assert(metrics.diameter >= config.minDiameter);
    assert(metrics.diameter <= config.maxDiameter);
    assert(metrics.averageDegree > 2.0);

    assert(state.jackals.size() == 2);
    assert(state.pits.size() == 1);
    assert(state.players.size() == 2);
}

void initialPlacementsDoNotOverlap() {
    const auto state = MapGenerator::generate(22222, 33333);

    std::set<CaveId> occupied;
    occupied.insert(state.players[0].cave);
    occupied.insert(state.players[1].cave);
    occupied.insert(state.basilisk.cave);

    for (const auto& pit : state.pits) {
        assert(!occupied.contains(pit.cave));
        occupied.insert(pit.cave);
    }

    for (const auto& jackal : state.jackals) {
        assert(!occupied.contains(jackal.cave));
        occupied.insert(jackal.cave);
    }
}

void identicalSeedsReproduceEntireGeneratedWorld() {
    const auto first = MapGenerator::generate(44444, 55555);
    const auto second = MapGenerator::generate(44444, 55555);
    assertSameGeneratedWorld(first, second);

    const auto firstMetrics = MapGenerator::analyzeTopology(first.world);
    const auto secondMetrics = MapGenerator::analyzeTopology(second.world);
    assert(firstMetrics.edgeCount == secondMetrics.edgeCount);
    assert(firstMetrics.deadEndCount == secondMetrics.deadEndCount);
    assert(firstMetrics.loopCount == secondMetrics.loopCount);
    assert(firstMetrics.diameter == secondMetrics.diameter);
}

void canonicalSeedHasPortableStructuralSignature() {
    const MatchState state = MapGenerator::generate(
        MapSeed{20260812}, MatchSeed{424242});
    assert(generatedWorldSignature(state) == 14569748892413728933ULL);
}

void manySeedsRemainValidOrganicAndWellPlaced() {
    ProceduralMapConfig config;

    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
        const auto state = MapGenerator::generate(seed, seed * 991U, {}, config);
        const auto metrics = MapGenerator::analyzeTopology(state.world);

        assert(MapGenerator::validateTopology(state.world, config));
        assert(MapGenerator::validateFairness(state, config));
        assertPlacementQuality(state, config);
        assert(state.world.size() == config.caveCount);
        assert(metrics.deadEndCount >= config.minDeadEnds);
        assert(metrics.deadEndCount <= config.maxDeadEnds);
        assert(metrics.loopCount >= config.minLoopCount);
        assert(metrics.diameter >= config.minDiameter);
        assert(metrics.diameter <= config.maxDiameter);

        for (const CaveId cave : state.world.caveIds()) {
            const auto degree = state.world.cave(cave).connections.size();
            assert(degree >= config.minDegree);
            assert(degree <= config.maxDegree);
        }
    }
}

void deadEndTargetIsConfigurable() {
    ProceduralMapConfig config;
    config.targetDeadEnds = 6;
    config.minDeadEnds = 6;
    config.maxDeadEnds = 6;

    const auto state = MapGenerator::generate(61616, 71717, {}, config);
    const auto metrics = MapGenerator::analyzeTopology(state.world);
    assert(metrics.deadEndCount == 6);
    assertPlacementQuality(state, config);
}

void jackalScalingUsesCaveCountAndSeparation() {
    ProceduralMapConfig config;
    config.caveCount = 45;
    config.extraConnections = 12;
    config.minHunterSeparation = 7;

    const auto state = MapGenerator::generate(77777, 88888, {}, config);
    assert(state.jackals.size() == 3);
    assertPlacementQuality(state, config);
}

void pitCountRemainsConfigurableAndSeparated() {
    ProceduralMapConfig config;
    config.pitCount = 3;

    const auto state = MapGenerator::generate(13579, 24680, {}, config);
    assert(state.pits.size() == 3);
    assertPlacementQuality(state, config);
    assert(nonPitCavesRemainConnected(state));
}

void pitsNeverSealOffRegionsAcrossManySeeds() {
    ProceduralMapConfig config;
    config.pitCount = 3;

    for (std::uint64_t seed = 100; seed < 150; ++seed) {
        const auto state = MapGenerator::generate(seed, seed * 313U, {}, config);
        assert(nonPitCavesRemainConnected(state));
        assert(MapGenerator::validateFairness(state, config));
    }
}

void deadEndSpawnPoliciesAreConfigurable() {
    ProceduralMapConfig config;
    config.allowHunterSpawnInDeadEnd = true;
    config.allowBasiliskInDeadEnd = true;

    const auto state = MapGenerator::generate(91919, 82828, {}, config);
    assert(MapGenerator::validateFairness(state, config));
}

} // namespace

int main() {
    defaultThirtyCaveMapHasExpectedShapeAndActors();
    initialPlacementsDoNotOverlap();
    identicalSeedsReproduceEntireGeneratedWorld();
    canonicalSeedHasPortableStructuralSignature();
    manySeedsRemainValidOrganicAndWellPlaced();
    deadEndTargetIsConfigurable();
    jackalScalingUsesCaveCountAndSeparation();
    pitCountRemainsConfigurableAndSeparated();
    pitsNeverSealOffRegionsAcrossManySeeds();
    deadEndSpawnPoliciesAreConfigurable();

    std::cout << "Basilisk procedural map generator tests passed.\n";
    return 0;
}
