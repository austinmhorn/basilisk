#include "basilisk/world/MapGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "basilisk/Random.hpp"

namespace basilisk {
namespace {

constexpr std::uint64_t kTopologySalt = 0xA24BAED4963EE407ULL;
constexpr std::uint64_t kSpawnSalt    = 0x9FB21C651E98DF25ULL;
constexpr std::uint64_t kHazardSalt   = 0xC13FA9A902A6328FULL;
constexpr std::uint64_t kAiSalt       = 0x91E10DA5C79E7B1DULL;

std::uint64_t mixSeed(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

std::uint64_t derivedSeed(std::uint64_t base, std::uint64_t salt) {
    return mixSeed(base ^ salt);
}

template <typename T>
void shuffle(std::vector<T>& values, RandomGenerator& rng) {
    if (values.size() < 2) return;
    for (std::size_t i = values.size() - 1; i > 0; --i) {
        const auto j = static_cast<std::size_t>(rng.range(0, static_cast<int>(i)));
        std::swap(values[i], values[j]);
    }
}

std::unordered_map<CaveId, int> distancesFrom(const WorldGraph& world, CaveId start) {
    std::unordered_map<CaveId, int> distance;
    if (!world.contains(start)) return distance;

    std::queue<CaveId> frontier;
    frontier.push(start);
    distance.emplace(start, 0);

    while (!frontier.empty()) {
        const CaveId current = frontier.front();
        frontier.pop();

        for (const CaveId next : world.cave(current).connections) {
            if (distance.contains(next)) continue;
            distance.emplace(next, distance.at(current) + 1);
            frontier.push(next);
        }
    }

    return distance;
}

std::optional<int> distanceBetween(const WorldGraph& world, CaveId start, CaveId target) {
    const auto distances = distancesFrom(world, start);
    const auto it = distances.find(target);
    if (it == distances.end()) return std::nullopt;
    return it->second;
}

bool isDeadEnd(const WorldGraph& world, CaveId cave) {
    return world.cave(cave).connections.size() == 1;
}

bool atLeastDistance(
    const WorldGraph& world,
    CaveId a,
    CaveId b,
    int minimum) {

    const auto distance = distanceBetween(world, a, b);
    return distance.has_value() && *distance >= minimum;
}

bool farEnoughFromAll(
    const WorldGraph& world,
    CaveId cave,
    const std::vector<CaveId>& others,
    int minimum) {

    return std::all_of(others.begin(), others.end(), [&](CaveId other) {
        return atLeastDistance(world, cave, other, minimum);
    });
}

WorldGraph generateTopology(
    const ProceduralMapConfig& config,
    RandomGenerator& rng) {

    if (config.caveCount < 6) {
        throw std::invalid_argument("Procedural maps require at least six caves.");
    }
    if (config.minDegree < 1 || config.maxDegree < config.minDegree) {
        throw std::invalid_argument("Invalid procedural map degree constraints.");
    }
    if (config.maxDegree < 3) {
        throw std::invalid_argument("Organic procedural maps require maxDegree >= 3.");
    }
    if (config.targetDeadEnds >= config.caveCount - 2) {
        throw std::invalid_argument("Too many dead ends requested for cave count.");
    }

    WorldGraph world;
    std::vector<CaveId> order;
    order.reserve(config.caveCount);

    for (std::size_t i = 0; i < config.caveCount; ++i) {
        const CaveId id = static_cast<CaveId>(i + 1);
        world.addCave(id);
        order.push_back(id);
    }

    shuffle(order, rng);

    const std::size_t branchCount = config.targetDeadEnds;
    const std::size_t coreCount = config.caveCount - branchCount;

    if (coreCount < 3) {
        throw std::invalid_argument("Organic map core requires at least three caves.");
    }

    std::vector<CaveId> core(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(coreCount));
    std::vector<CaveId> branches(order.begin() + static_cast<std::ptrdiff_t>(coreCount), order.end());

    for (std::size_t i = 0; i < core.size(); ++i) {
        world.connect(core[i], core[(i + 1) % core.size()]);
    }

    auto branchParents = core;
    shuffle(branchParents, rng);

    std::size_t parentIndex = 0;
    for (const CaveId branch : branches) {
        while (parentIndex < branchParents.size() &&
               world.cave(branchParents[parentIndex]).connections.size() >= config.maxDegree) {
            ++parentIndex;
        }
        if (parentIndex >= branchParents.size()) {
            throw std::runtime_error("Unable to attach requested dead-end branches.");
        }

        world.connect(branchParents[parentIndex], branch);
        ++parentIndex;
    }

    std::size_t added = 0;
    const std::size_t maxTries = config.caveCount * config.caveCount * 12;
    for (std::size_t tries = 0; tries < maxTries && added < config.extraConnections; ++tries) {
        const CaveId a = core[static_cast<std::size_t>(
            rng.range(0, static_cast<int>(core.size()) - 1))];
        const CaveId b = core[static_cast<std::size_t>(
            rng.range(0, static_cast<int>(core.size()) - 1))];

        if (a == b || world.areConnected(a, b)) continue;
        if (world.cave(a).connections.size() >= config.maxDegree) continue;
        if (world.cave(b).connections.size() >= config.maxDegree) continue;

        world.connect(a, b);
        ++added;
    }

    if (added < config.extraConnections) {
        throw std::runtime_error("Unable to add requested extra map connections.");
    }

    return world;
}

bool isReserved(CaveId cave, const std::unordered_set<CaveId>& reserved) {
    return reserved.contains(cave);
}

std::optional<std::pair<CaveId, CaveId>> chooseHunterSpawns(
    const WorldGraph& world,
    const ProceduralMapConfig& config,
    RandomGenerator& rng) {

    auto caves = world.caveIds();
    shuffle(caves, rng);

    std::vector<std::pair<CaveId, CaveId>> candidates;
    for (std::size_t i = 0; i < caves.size(); ++i) {
        if (!config.allowHunterSpawnInDeadEnd && isDeadEnd(world, caves[i])) continue;

        for (std::size_t j = i + 1; j < caves.size(); ++j) {
            if (!config.allowHunterSpawnInDeadEnd && isDeadEnd(world, caves[j])) continue;

            const auto distance = distanceBetween(world, caves[i], caves[j]);
            if (distance.has_value() && *distance >= config.minHunterSeparation) {
                candidates.emplace_back(caves[i], caves[j]);
            }
        }
    }

    if (candidates.empty()) return std::nullopt;
    const auto index = static_cast<std::size_t>(
        rng.range(0, static_cast<int>(candidates.size()) - 1));
    return candidates[index];
}

std::optional<CaveId> chooseBasiliskSpawn(
    const WorldGraph& world,
    CaveId hunterA,
    CaveId hunterB,
    const ProceduralMapConfig& config,
    RandomGenerator& rng) {

    std::vector<CaveId> candidates;
    for (const CaveId cave : world.caveIds()) {
        if (cave == hunterA || cave == hunterB) continue;
        if (!config.allowBasiliskInDeadEnd && isDeadEnd(world, cave)) continue;

        const auto aDistance = distanceBetween(world, hunterA, cave);
        const auto bDistance = distanceBetween(world, hunterB, cave);
        if (!aDistance.has_value() || !bDistance.has_value()) continue;
        if (*aDistance < config.minHunterBasiliskDistance ||
            *bDistance < config.minHunterBasiliskDistance) continue;
        if (std::abs(*aDistance - *bDistance) > config.maxHunterBasiliskDistanceDelta) continue;

        candidates.push_back(cave);
    }

    if (candidates.empty()) return std::nullopt;
    const auto index = static_cast<std::size_t>(
        rng.range(0, static_cast<int>(candidates.size()) - 1));
    return candidates[index];
}

void placePits(
    MatchState& state,
    const ProceduralMapConfig& config,
    RandomGenerator& rng,
    std::unordered_set<CaveId>& reserved) {

    auto candidates = state.world.caveIds();
    shuffle(candidates, rng);

    std::vector<CaveId> placedPits;
    for (const CaveId cave : candidates) {
        if (state.pits.size() >= config.pitCount) break;
        if (isReserved(cave, reserved)) continue;
        if (!farEnoughFromAll(
                state.world,
                cave,
                {state.players[0].cave, state.players[1].cave},
                config.minHunterPitDistance)) continue;
        if (!atLeastDistance(
                state.world,
                cave,
                state.basilisk.cave,
                config.minBasiliskPitDistance)) continue;
        if (!farEnoughFromAll(
                state.world,
                cave,
                placedPits,
                config.minPitSeparation)) continue;

        state.pits.push_back(PitState{cave, true});
        placedPits.push_back(cave);
        reserved.insert(cave);
    }

    if (state.pits.size() != config.pitCount) {
        throw std::runtime_error("Unable to place requested Pit count with quality constraints.");
    }
}

void placeJackals(
    MatchState& state,
    const ProceduralMapConfig& config,
    RandomGenerator& rng,
    std::unordered_set<CaveId>& reserved) {

    const int cavesPerJackal = std::max(1, state.rules.cavesPerJackal);
    const std::size_t count = std::max<std::size_t>(
        1,
        (state.world.size() + static_cast<std::size_t>(cavesPerJackal) - 1) /
            static_cast<std::size_t>(cavesPerJackal));

    auto candidates = state.world.caveIds();
    shuffle(candidates, rng);

    std::vector<CaveId> placedJackals;
    for (const CaveId cave : candidates) {
        if (state.jackals.size() >= count) break;
        if (isReserved(cave, reserved)) continue;
        if (!farEnoughFromAll(
                state.world,
                cave,
                {state.players[0].cave, state.players[1].cave},
                config.minHunterJackalDistance)) continue;
        if (!farEnoughFromAll(
                state.world,
                cave,
                placedJackals,
                config.minJackalSeparation)) continue;

        JackalState jackal;
        jackal.cave = cave;
        state.jackals.push_back(jackal);
        placedJackals.push_back(cave);
        reserved.insert(cave);
    }

    if (state.jackals.size() != count) {
        throw std::runtime_error("Unable to place requested Jackal count with quality constraints.");
    }
}

MatchState populateMatch(
    WorldGraph world,
    MapSeed mapSeed,
    MatchSeed matchSeed,
    const Rules& rules,
    const ProceduralMapConfig& config,
    RandomGenerator& spawnRng,
    RandomGenerator& hazardRng,
    RandomGenerator& aiRng) {

    MatchState state;
    state.mapSeed = mapSeed;
    state.matchSeed = matchSeed;
    state.rules = rules;
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FogOfWar;
    state.world = std::move(world);

    const auto hunterSpawns = chooseHunterSpawns(state.world, config, spawnRng);
    if (!hunterSpawns.has_value()) {
        throw std::runtime_error("No valid hunter spawn pair found.");
    }

    const auto basiliskSpawn = chooseBasiliskSpawn(
        state.world,
        hunterSpawns->first,
        hunterSpawns->second,
        config,
        spawnRng);
    if (!basiliskSpawn.has_value()) {
        throw std::runtime_error("No fair Basilisk spawn found.");
    }

    PlayerState playerA;
    playerA.id = 1;
    playerA.cave = hunterSpawns->first;
    playerA.health = rules.maxHealth;
    playerA.arrows = rules.startingArrows;

    PlayerState playerB;
    playerB.id = 2;
    playerB.cave = hunterSpawns->second;
    playerB.health = rules.maxHealth;
    playerB.arrows = rules.startingArrows;

    state.players = {playerA, playerB};
    state.basilisk.cave = *basiliskSpawn;

    std::unordered_set<CaveId> reserved{
        playerA.cave,
        playerB.cave,
        state.basilisk.cave
    };

    placePits(state, config, hazardRng, reserved);
    placeJackals(state, config, aiRng, reserved);

    return state;
}

} // namespace

MatchState MapGenerator::generate(
    MapSeed mapSeed,
    MatchSeed matchSeed,
    const Rules& rules,
    const ProceduralMapConfig& config) {

    if (config.maxGenerationAttempts == 0) {
        throw std::invalid_argument("maxGenerationAttempts must be greater than zero.");
    }

    for (std::size_t attempt = 0; attempt < config.maxGenerationAttempts; ++attempt) {
        const auto attemptSeed = mapSeed + static_cast<MapSeed>(attempt);
        RandomGenerator topologyRng{derivedSeed(attemptSeed, kTopologySalt)};

        try {
            auto world = generateTopology(config, topologyRng);
            if (!validateTopology(world, config)) continue;

            RandomGenerator spawnRng{derivedSeed(matchSeed ^ attemptSeed, kSpawnSalt)};
            RandomGenerator hazardRng{derivedSeed(matchSeed ^ attemptSeed, kHazardSalt)};
            RandomGenerator aiRng{derivedSeed(matchSeed ^ attemptSeed, kAiSalt)};

            auto state = populateMatch(
                std::move(world),
                mapSeed,
                matchSeed,
                rules,
                config,
                spawnRng,
                hazardRng,
                aiRng);

            if (validateFairness(state, config)) return state;
        } catch (const std::runtime_error&) {
            // Deterministically try the next topology candidate.
        }
    }

    throw std::runtime_error("Unable to generate a valid procedural map within attempt limit.");
}

MapTopologyMetrics MapGenerator::analyzeTopology(const WorldGraph& world) {
    MapTopologyMetrics metrics;
    metrics.caveCount = world.size();
    if (world.size() == 0) return metrics;

    std::size_t degreeSum = 0;
    for (const CaveId cave : world.caveIds()) {
        const std::size_t degree = world.cave(cave).connections.size();
        degreeSum += degree;
        if (degree == 1) ++metrics.deadEndCount;
    }

    metrics.edgeCount = degreeSum / 2;
    metrics.averageDegree = static_cast<double>(degreeSum) /
        static_cast<double>(world.size());

    if (metrics.edgeCount >= metrics.caveCount - 1) {
        metrics.loopCount = metrics.edgeCount - metrics.caveCount + 1;
    }

    for (const CaveId start : world.caveIds()) {
        const auto distances = distancesFrom(world, start);
        for (const auto& [cave, distance] : distances) {
            (void)cave;
            metrics.diameter = std::max(metrics.diameter, distance);
        }
    }

    return metrics;
}

bool MapGenerator::validateTopology(
    const WorldGraph& world,
    const ProceduralMapConfig& config) {

    if (world.size() != config.caveCount || world.size() == 0) return false;

    const auto ids = world.caveIds();
    for (const CaveId cave : ids) {
        const auto degree = world.cave(cave).connections.size();
        if (degree < config.minDegree || degree > config.maxDegree) return false;
    }

    const auto visited = distancesFrom(world, ids.front());
    if (visited.size() != world.size()) return false;

    const auto metrics = analyzeTopology(world);
    if (metrics.deadEndCount < config.minDeadEnds ||
        metrics.deadEndCount > config.maxDeadEnds) return false;
    if (metrics.loopCount < config.minLoopCount) return false;
    if (metrics.diameter < config.minDiameter ||
        metrics.diameter > config.maxDiameter) return false;

    return true;
}

bool MapGenerator::validateFairness(
    const MatchState& state,
    const ProceduralMapConfig& config) {

    if (state.players.size() != 2 || !state.basilisk.alive) return false;

    const auto& hunterA = state.players[0];
    const auto& hunterB = state.players[1];

    if (!config.allowHunterSpawnInDeadEnd &&
        (isDeadEnd(state.world, hunterA.cave) || isDeadEnd(state.world, hunterB.cave))) {
        return false;
    }
    if (!config.allowBasiliskInDeadEnd && isDeadEnd(state.world, state.basilisk.cave)) {
        return false;
    }

    const auto hunterDistance = distanceBetween(state.world, hunterA.cave, hunterB.cave);
    if (!hunterDistance.has_value() || *hunterDistance < config.minHunterSeparation) return false;

    const auto aDistance = distanceBetween(state.world, hunterA.cave, state.basilisk.cave);
    const auto bDistance = distanceBetween(state.world, hunterB.cave, state.basilisk.cave);
    if (!aDistance.has_value() || !bDistance.has_value()) return false;
    if (*aDistance < config.minHunterBasiliskDistance ||
        *bDistance < config.minHunterBasiliskDistance) return false;
    if (std::abs(*aDistance - *bDistance) > config.maxHunterBasiliskDistanceDelta) return false;

    std::vector<CaveId> pitCaves;
    for (const auto& pit : state.pits) {
        if (!pit.active) continue;
        if (!atLeastDistance(state.world, hunterA.cave, pit.cave, config.minHunterPitDistance) ||
            !atLeastDistance(state.world, hunterB.cave, pit.cave, config.minHunterPitDistance) ||
            !atLeastDistance(state.world, state.basilisk.cave, pit.cave, config.minBasiliskPitDistance) ||
            !farEnoughFromAll(state.world, pit.cave, pitCaves, config.minPitSeparation)) {
            return false;
        }
        pitCaves.push_back(pit.cave);
    }

    std::vector<CaveId> jackalCaves;
    for (const auto& jackal : state.jackals) {
        if (!atLeastDistance(state.world, hunterA.cave, jackal.cave, config.minHunterJackalDistance) ||
            !atLeastDistance(state.world, hunterB.cave, jackal.cave, config.minHunterJackalDistance) ||
            !farEnoughFromAll(state.world, jackal.cave, jackalCaves, config.minJackalSeparation)) {
            return false;
        }
        jackalCaves.push_back(jackal.cave);
    }

    return true;
}

} // namespace basilisk
