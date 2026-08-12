#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>
#include <vector>

#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

std::set<CaveId> connectionsOf(const MatchState& state, CaveId cave) {
    const auto& connections = state.world.cave(cave).connections;
    return std::set<CaveId>(connections.begin(), connections.end());
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

    assert(state.world.size() == 30);
    assert(MapGenerator::validateTopology(state.world, config));
    assert(MapGenerator::validateFairness(state, config));
    assert(state.rules.mapDiscoveryMode == MapDiscoveryMode::FogOfWar);

    // Locked gameplay rule: one Jackal per 15 caves.
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
}

void manySeedsRemainValid() {
    ProceduralMapConfig config;

    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
        const auto state = MapGenerator::generate(seed, seed * 991U, {}, config);
        assert(MapGenerator::validateTopology(state.world, config));
        assert(MapGenerator::validateFairness(state, config));
        assert(state.world.size() == config.caveCount);

        for (const CaveId cave : state.world.caveIds()) {
            const auto degree = state.world.cave(cave).connections.size();
            assert(degree >= config.minDegree);
            assert(degree <= config.maxDegree);
        }
    }
}

void jackalScalingUsesCaveCount() {
    ProceduralMapConfig config;
    config.caveCount = 45;
    config.extraConnections = 12;
    config.minHunterSeparation = 7;

    const auto state = MapGenerator::generate(77777, 88888, {}, config);
    assert(state.jackals.size() == 3);
}

void pitCountRemainsConfigurable() {
    ProceduralMapConfig config;
    config.pitCount = 3;

    const auto state = MapGenerator::generate(13579, 24680, {}, config);
    assert(state.pits.size() == 3);
}

} // namespace

int main() {
    defaultThirtyCaveMapHasExpectedShapeAndActors();
    initialPlacementsDoNotOverlap();
    identicalSeedsReproduceEntireGeneratedWorld();
    manySeedsRemainValid();
    jackalScalingUsesCaveCount();
    pitCountRemainsConfigurable();

    std::cout << "Basilisk procedural map generator tests passed.\n";
    return 0;
}
