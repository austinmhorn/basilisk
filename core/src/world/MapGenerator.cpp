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

std::optional<int> distanceBetween(const WorldGraph& world, CaveId start, CaveId target) {
    if (!world.contains(start) || !world.contains(target)) return std::nullopt;
    if (start == target) return 0;

    std::queue<CaveId> frontier;
    std::unordered_map<CaveId, int> distance;
    frontier.push(start);
    distance.emplace(start, 0);

    while (!frontier.empty()) {
        const CaveId current = frontier.front();
        frontier.pop();

        for (const CaveId next : world.cave(current).connections) {
            if (distance.contains(next)) continue;
            const int nextDistance = distance.at(current) + 1;
            if (next == target) return nextDistance;
            distance.emplace(next, nextDistance);
            frontier.push(next);
        }
    }

    return std::nullopt;
}

WorldGraph generateTopology(
    const ProceduralMapConfig& config,
    RandomGenerator& rng) {

    if (config.caveCount < 4) {
        throw std::invalid_argument("Procedural maps require at least four caves.");
    }
    if (config.minDegree < 1 || config.maxDegree < config.minDegree) {
        throw std::invalid_argument("Invalid procedural map degree constraints.");
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

    // A randomized cycle gives every initial profile a connected backbone with
    // no isolated regions. Extra chords then create asymmetric route choices.
    for (std::size_t i = 0; i < order.size(); ++i) {
        world.connect(order[i], order[(i + 1) % order.size()]);
    }

    std::size_t added = 0;
    const std::size_t maxTries = config.caveCount * config.caveCount * 8;
    for (std::size_t tries = 0; tries < maxTries && added < config.extraConnections; ++tries) {
        const CaveId a = order[static_cast<std::size_t>(
            rng.range(0, static_cast<int>(order.size()) - 1))];
        const CaveId b = order[static_cast<std::size_t>(
            rng.range(0, static_cast<int>(order.size()) - 1))];

        if (a == b || world.areConnected(a, b)) continue;
        if (world.cave(a).connections.size() >= config.maxDegree) continue;
        if (world.cave(b).connections.size() >= config.maxDegree) continue;

        world.connect(a, b);
        ++added;
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
        for (std::size_t j = i + 1; j < caves.size(); ++j) {
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

    for (const CaveId cave : candidates) {
        if (state.pits.size() >= config.pitCount) break;
        if (isReserved(cave, reserved)) continue;
        state.pits.push_back(PitState{cave, true});
        reserved.insert(cave);
    }

    if (state.pits.size() != config.pitCount) {
        throw std::runtime_error("Unable to place requested Pit count.");
    }
}

void placeJackals(
    MatchState& state,
    RandomGenerator& rng,
    std::unordered_set<CaveId>& reserved) {

    const int cavesPerJackal = std::max(1, state.rules.cavesPerJackal);
    const std::size_t count = std::max<std::size_t>(
        1,
        (state.world.size() + static_cast<std::size_t>(cavesPerJackal) - 1) /
            static_cast<std::size_t>(cavesPerJackal));

    auto candidates = state.world.caveIds();
    shuffle(candidates, rng);

    for (const CaveId cave : candidates) {
        if (state.jackals.size() >= count) break;
        if (isReserved(cave, reserved)) continue;

        JackalState jackal;
        jackal.cave = cave;
        state.jackals.push_back(jackal);
        reserved.insert(cave);
    }

    if (state.jackals.size() != count) {
        throw std::runtime_error("Unable to place requested Jackal count.");
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
    placeJackals(state, aiRng, reserved);

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

        auto world = generateTopology(config, topologyRng);
        if (!validateTopology(world, config)) continue;

        RandomGenerator spawnRng{derivedSeed(matchSeed ^ attemptSeed, kSpawnSalt)};
        RandomGenerator hazardRng{derivedSeed(matchSeed ^ attemptSeed, kHazardSalt)};
        RandomGenerator aiRng{derivedSeed(matchSeed ^ attemptSeed, kAiSalt)};

        try {
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

bool MapGenerator::validateTopology(
    const WorldGraph& world,
    const ProceduralMapConfig& config) {

    if (world.size() != config.caveCount || world.size() == 0) return false;

    const auto ids = world.caveIds();
    for (const CaveId cave : ids) {
        const auto degree = world.cave(cave).connections.size();
        if (degree < config.minDegree || degree > config.maxDegree) return false;
    }

    std::unordered_set<CaveId> visited;
    std::queue<CaveId> frontier;
    frontier.push(ids.front());
    visited.insert(ids.front());

    while (!frontier.empty()) {
        const CaveId current = frontier.front();
        frontier.pop();

        for (const CaveId next : world.cave(current).connections) {
            if (visited.insert(next).second) frontier.push(next);
        }
    }

    return visited.size() == world.size();
}

bool MapGenerator::validateFairness(
    const MatchState& state,
    const ProceduralMapConfig& config) {

    if (state.players.size() != 2 || !state.basilisk.alive) return false;

    const auto hunterDistance = distanceBetween(
        state.world,
        state.players[0].cave,
        state.players[1].cave);
    if (!hunterDistance.has_value() || *hunterDistance < config.minHunterSeparation) return false;

    const auto aDistance = distanceBetween(
        state.world,
        state.players[0].cave,
        state.basilisk.cave);
    const auto bDistance = distanceBetween(
        state.world,
        state.players[1].cave,
        state.basilisk.cave);

    if (!aDistance.has_value() || !bDistance.has_value()) return false;
    if (*aDistance < config.minHunterBasiliskDistance ||
        *bDistance < config.minHunterBasiliskDistance) return false;

    return std::abs(*aDistance - *bDistance) <= config.maxHunterBasiliskDistanceDelta;
}

} // namespace basilisk
