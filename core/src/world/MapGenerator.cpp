#include "basilisk/world/MapGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <span>
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

bool nonBlockedCavesRemainConnected(
    const WorldGraph& world,
    const std::unordered_set<CaveId>& blocked) {

    const auto ids = world.caveIds();
    auto start = std::find_if(ids.begin(), ids.end(), [&](CaveId cave) {
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

        for (const CaveId next : world.cave(current).connections) {
            if (blocked.contains(next) || visited.contains(next)) continue;
            visited.insert(next);
            frontier.push(next);
        }
    }

    const auto remaining = static_cast<std::size_t>(std::count_if(
        ids.begin(), ids.end(), [&](CaveId cave) { return !blocked.contains(cave); }));
    return visited.size() == remaining;
}

std::optional<WorldGraph> generateTopology(
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
            return std::nullopt;
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
        return std::nullopt;
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

using DistanceTable = std::unordered_map<CaveId, std::unordered_map<CaveId, int>>;

DistanceTable allDistances(const WorldGraph& world) {
    DistanceTable distances;
    for (const CaveId cave : world.caveIds())
        distances.emplace(cave, distancesFrom(world, cave));
    return distances;
}

std::vector<std::vector<CaveId>> chooseHunterSpawnSets(
    const WorldGraph& world,
    std::size_t count,
    const ProceduralMapConfig& config,
    RandomGenerator& rng,
    const DistanceTable& distances) {

    auto candidates = world.caveIds();
    shuffle(candidates, rng);
    std::erase_if(candidates, [&](CaveId cave) {
        return !config.allowHunterSpawnInDeadEnd && isDeadEnd(world, cave);
    });
    std::vector<std::vector<CaveId>> selections;
    std::set<std::vector<CaveId>> seen;
    if (candidates.size() < count) return selections;

    // A single greedy start can reject an otherwise valid topology. Try each
    // possible first spawn, retaining only distinct fair max-min selections.
    for (const CaveId first : candidates) {
        std::vector<CaveId> selected{first};
        while (selected.size() < count) {
            std::optional<CaveId> best;
            int bestMinimum = -1;
            for (const CaveId candidate : candidates) {
                if (std::find(selected.begin(), selected.end(), candidate) != selected.end())
                    continue;
                int minimum = std::numeric_limits<int>::max();
                for (const CaveId existing : selected) {
                    const auto distance = distances.at(existing).find(candidate);
                    if (distance == distances.at(existing).end()) { minimum = -1; break; }
                    minimum = std::min(minimum, distance->second);
                }
                if (minimum > bestMinimum) {
                    bestMinimum = minimum;
                    best = candidate;
                }
            }
            if (!best.has_value() || bestMinimum < config.minHunterSeparation) break;
            selected.push_back(*best);
        }
        if (selected.size() != count) continue;
        auto canonical = selected;
        std::ranges::sort(canonical);
        if (seen.insert(std::move(canonical)).second)
            selections.push_back(std::move(selected));
    }
    return selections;
}

std::optional<CaveId> chooseBasiliskSpawn(
    const WorldGraph& world,
    std::span<const CaveId> hunterSpawns,
    const ProceduralMapConfig& config,
    RandomGenerator& rng,
    const DistanceTable* distances = nullptr) {

    std::vector<CaveId> candidates;
    for (const CaveId cave : world.caveIds()) {
        if (std::find(hunterSpawns.begin(), hunterSpawns.end(), cave) != hunterSpawns.end()) continue;
        if (!config.allowBasiliskInDeadEnd && isDeadEnd(world, cave)) continue;

        int minimum = std::numeric_limits<int>::max();
        int maximum = 0;
        bool reachable = true;
        for (const CaveId spawn : hunterSpawns) {
            const auto distance = distances == nullptr
                ? distanceBetween(world, spawn, cave)
                : [&]() -> std::optional<int> {
                    const auto found = distances->at(spawn).find(cave);
                    if (found == distances->at(spawn).end()) return std::nullopt;
                    return found->second;
                }();
            if (!distance.has_value()) { reachable = false; break; }
            minimum = std::min(minimum, *distance);
            maximum = std::max(maximum, *distance);
        }
        if (!reachable || minimum < config.minHunterBasiliskDistance ||
            maximum - minimum > config.maxHunterBasiliskDistanceDelta) continue;

        candidates.push_back(cave);
    }

    if (candidates.empty()) return std::nullopt;
    const auto index = static_cast<std::size_t>(
        rng.range(0, static_cast<int>(candidates.size()) - 1));
    return candidates[index];
}

std::optional<std::pair<std::vector<CaveId>, CaveId>> chooseMultiHunterPlacement(
    const WorldGraph& world,
    std::size_t count,
    const ProceduralMapConfig& config,
    RandomGenerator& rng) {

    const DistanceTable distances = allDistances(world);
    std::vector<std::pair<std::vector<CaveId>, CaveId>> placements;
    for (auto& hunterSpawns : chooseHunterSpawnSets(
            world, count, config, rng, distances)) {
        const auto basiliskSpawn = chooseBasiliskSpawn(
            world, hunterSpawns, config, rng, &distances);
        if (basiliskSpawn.has_value())
            placements.emplace_back(std::move(hunterSpawns), *basiliskSpawn);
    }
    if (placements.empty()) return std::nullopt;
    return placements[static_cast<std::size_t>(
        rng.range(0, static_cast<int>(placements.size()) - 1))];
}

bool placePits(
    MatchState& state,
    const ProceduralMapConfig& config,
    RandomGenerator& rng,
    std::unordered_set<CaveId>& reserved) {

    auto candidates = state.world.caveIds();
    shuffle(candidates, rng);

    std::vector<CaveId> placedPits;
    std::vector<CaveId> hunterCaves;
    for (const auto& player : state.players) hunterCaves.push_back(player.cave);
    for (const CaveId cave : candidates) {
        if (state.pits.size() >= config.pitCount) break;
        if (isReserved(cave, reserved)) continue;
        if (!farEnoughFromAll(
                state.world,
                cave,
                hunterCaves,
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

        if (config.preventPitsBlockingRegions) {
            std::unordered_set<CaveId> blocked(placedPits.begin(), placedPits.end());
            blocked.insert(cave);
            if (!nonBlockedCavesRemainConnected(state.world, blocked)) continue;
        }

        state.pits.push_back(PitState{cave, true});
        placedPits.push_back(cave);
        reserved.insert(cave);
    }

    return state.pits.size() == config.pitCount;
}

bool placeJackals(
    MatchState& state,
    const ProceduralMapConfig& config,
    RandomGenerator& rng,
    std::unordered_set<CaveId>& reserved) {

    const int cavesPerJackal = std::max(1, state.rules.cavesPerJackal);
    const std::size_t count = config.jackalCount.value_or(std::max<std::size_t>(
        1,
        (state.world.size() + static_cast<std::size_t>(cavesPerJackal) - 1) /
            static_cast<std::size_t>(cavesPerJackal)));

    auto candidates = state.world.caveIds();
    shuffle(candidates, rng);

    std::vector<CaveId> placedJackals;
    std::vector<CaveId> hunterCaves;
    for (const auto& player : state.players) hunterCaves.push_back(player.cave);
    for (const CaveId cave : candidates) {
        if (state.jackals.size() >= count) break;
        if (isReserved(cave, reserved)) continue;
        if (!farEnoughFromAll(
                state.world,
                cave,
                hunterCaves,
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

    return state.jackals.size() == count;
}

std::optional<MatchState> populateMatch(
    WorldGraph world,
    MapSeed mapSeed,
    MatchSeed matchSeed,
    const Rules& rules,
    const ProceduralMapConfig& config,
    RandomGenerator& spawnRng,
    RandomGenerator& hazardRng,
    RandomGenerator& aiRng,
    std::span<const PlayerId> roster) {

    MatchState state;
    state.mapSeed = mapSeed;
    state.matchSeed = matchSeed;
    state.rules = rules;
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FogOfWar;
    state.world = std::move(world);

    std::vector<CaveId> hunterSpawns;
    std::optional<CaveId> basiliskSpawn;
    if (roster.size() == 2) {
        const auto pair = chooseHunterSpawns(state.world, config, spawnRng);
        if (!pair.has_value()) return std::nullopt;
        hunterSpawns = {pair->first, pair->second};
        basiliskSpawn = chooseBasiliskSpawn(
            state.world, hunterSpawns, config, spawnRng);
    } else {
        auto placement = chooseMultiHunterPlacement(
            state.world, roster.size(), config, spawnRng);
        if (!placement.has_value()) return std::nullopt;
        hunterSpawns = std::move(placement->first);
        basiliskSpawn = placement->second;
    }
    if (!basiliskSpawn.has_value()) return std::nullopt;

    for (std::size_t index = 0; index < roster.size(); ++index) {
        PlayerState player;
        player.id = roster[index];
        player.cave = hunterSpawns[index];
        player.health = rules.maxHealth;
        player.arrows = rules.startingArrows;
        state.players.push_back(std::move(player));
    }
    state.basilisk.cave = *basiliskSpawn;

    std::unordered_set<CaveId> reserved(hunterSpawns.begin(), hunterSpawns.end());
    reserved.insert(state.basilisk.cave);

    if (!placePits(state, config, hazardRng, reserved) ||
        !placeJackals(state, config, aiRng, reserved))
        return std::nullopt;

    return state;
}

} // namespace

MatchState MapGenerator::generate(
    MapSeed mapSeed,
    MatchSeed matchSeed,
    const Rules& rules,
    const ProceduralMapConfig& config) {

    const std::array<PlayerId, 2> roster{PlayerId{1}, PlayerId{2}};
    return generate(mapSeed, matchSeed, roster, rules, config);
}

MatchState MapGenerator::generate(
    MapSeed mapSeed,
    MatchSeed matchSeed,
    std::span<const PlayerId> roster,
    const Rules& rules,
    const ProceduralMapConfig& config) {

    if (roster.size() < 2 || roster.size() > 6) {
        throw std::invalid_argument("Procedural matches require two to six hunters.");
    }
    std::set<PlayerId> unique;
    for (const PlayerId player : roster) {
        if (player == 0 || !unique.insert(player).second)
            throw std::invalid_argument("Hunter roster requires unique nonzero player IDs.");
    }
    if (config.maxGenerationAttempts == 0) {
        throw std::invalid_argument("maxGenerationAttempts must be greater than zero.");
    }

    for (std::size_t attempt = 0; attempt < config.maxGenerationAttempts; ++attempt) {
        const auto attemptSeed = mapSeed + static_cast<MapSeed>(attempt);
        RandomGenerator topologyRng{derivedSeed(attemptSeed, kTopologySalt)};

        // Rejected candidates are normal during bounded procedural search.
        // Keep that path exception-free so Emscripten builds without C++
        // exception catching can advance to the next deterministic attempt.
        auto world = generateTopology(config, topologyRng);
        if (!world.has_value() || !validateTopology(*world, config)) continue;

        RandomGenerator spawnRng{derivedSeed(matchSeed ^ attemptSeed, kSpawnSalt)};
        RandomGenerator hazardRng{derivedSeed(matchSeed ^ attemptSeed, kHazardSalt)};
        RandomGenerator aiRng{derivedSeed(matchSeed ^ attemptSeed, kAiSalt)};

        auto state = populateMatch(
            std::move(*world),
            mapSeed,
            matchSeed,
            rules,
            config,
            spawnRng,
            hazardRng,
            aiRng,
            roster);

        if (state.has_value() && validateFairness(*state, config))
            return std::move(*state);
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

    if (state.players.size() < 2 || state.players.size() > 6 || !state.basilisk.alive) return false;
    std::set<PlayerId> playerIds;
    std::set<CaveId> starts;
    for (const auto& player : state.players) {
        if (player.id == 0 || !playerIds.insert(player.id).second ||
            !starts.insert(player.cave).second ||
            (!config.allowHunterSpawnInDeadEnd && isDeadEnd(state.world, player.cave)))
            return false;
    }
    if (!config.allowBasiliskInDeadEnd && isDeadEnd(state.world, state.basilisk.cave)) {
        return false;
    }

    int minimumBasiliskDistance = std::numeric_limits<int>::max();
    int maximumBasiliskDistance = 0;
    for (std::size_t i = 0; i < state.players.size(); ++i) {
        for (std::size_t j = i + 1; j < state.players.size(); ++j) {
            const auto distance = distanceBetween(
                state.world, state.players[i].cave, state.players[j].cave);
            if (!distance.has_value() || *distance < config.minHunterSeparation) return false;
        }
        const auto basiliskDistance = distanceBetween(
            state.world, state.players[i].cave, state.basilisk.cave);
        if (!basiliskDistance.has_value() ||
            *basiliskDistance < config.minHunterBasiliskDistance) return false;
        minimumBasiliskDistance = std::min(minimumBasiliskDistance, *basiliskDistance);
        maximumBasiliskDistance = std::max(maximumBasiliskDistance, *basiliskDistance);
    }
    if (maximumBasiliskDistance - minimumBasiliskDistance >
        config.maxHunterBasiliskDistanceDelta) return false;

    std::vector<CaveId> pitCaves;
    for (const auto& pit : state.pits) {
        if (!pit.active) continue;
        if (!std::all_of(state.players.begin(), state.players.end(), [&](const PlayerState& player) {
                return atLeastDistance(state.world, player.cave, pit.cave,
                    config.minHunterPitDistance);
            }) || !atLeastDistance(state.world, state.basilisk.cave, pit.cave, config.minBasiliskPitDistance) ||
            !farEnoughFromAll(state.world, pit.cave, pitCaves, config.minPitSeparation)) {
            return false;
        }
        pitCaves.push_back(pit.cave);
    }

    if (config.preventPitsBlockingRegions) {
        const std::unordered_set<CaveId> blocked(pitCaves.begin(), pitCaves.end());
        if (!nonBlockedCavesRemainConnected(state.world, blocked)) return false;
    }

    std::vector<CaveId> jackalCaves;
    for (const auto& jackal : state.jackals) {
        if (!std::all_of(state.players.begin(), state.players.end(), [&](const PlayerState& player) {
                return atLeastDistance(state.world, player.cave, jackal.cave,
                    config.minHunterJackalDistance);
            }) ||
            !farEnoughFromAll(state.world, jackal.cave, jackalCaves, config.minJackalSeparation)) {
            return false;
        }
        jackalCaves.push_back(jackal.cave);
    }

    return true;
}

} // namespace basilisk
