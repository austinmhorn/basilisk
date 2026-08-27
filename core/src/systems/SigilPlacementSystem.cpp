#include "basilisk/systems/SigilPlacementSystem.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <set>

namespace basilisk {
namespace {

bool activePit(const MatchState& state, CaveId cave) {
    return std::any_of(state.pits.begin(), state.pits.end(),
        [cave](const PitState& pit) { return pit.active && pit.cave == cave; });
}

std::set<CaveId> reachableWithoutBasilisk(const MatchState& state) {
    std::set<CaveId> reachable;
    std::queue<CaveId> pending;
    for (const PlayerState& player : state.players) {
        if (!player.alive || player.cave == state.basilisk.cave ||
            !state.world.contains(player.cave)) continue;
        if (reachable.insert(player.cave).second) pending.push(player.cave);
    }
    while (!pending.empty()) {
        const CaveId cave = pending.front();
        pending.pop();
        for (const CaveId next : state.world.cave(cave).connections) {
            if (next == state.basilisk.cave) continue;
            if (reachable.insert(next).second) pending.push(next);
        }
    }
    return reachable;
}

std::map<CaveId, int> distancesFrom(
    const MatchState& state, CaveId origin) {
    std::map<CaveId, int> distances;
    if (!state.world.contains(origin)) return distances;
    std::queue<CaveId> pending;
    distances.emplace(origin, 0);
    pending.push(origin);
    while (!pending.empty()) {
        const CaveId cave = pending.front();
        pending.pop();
        const int nextDistance = distances.at(cave) + 1;
        for (const CaveId next : state.world.cave(cave).connections) {
            if (distances.emplace(next, nextDistance).second) pending.push(next);
        }
    }
    return distances;
}

void emitRelocation(
    PlayerId owner, CaveId intended, CaveId placed,
    std::vector<GameEvent>& events) {
    if (placed != intended) {
        events.push_back(GameEvent{
            GameEventType::SigilEjected, std::nullopt, owner, placed});
    }
}

} // namespace

std::optional<CaveId> nearestRecoverableSigilCave(
    const MatchState& state, CaveId intendedCave) {
    const std::set<CaveId> reachable = reachableWithoutBasilisk(state);
    const std::map<CaveId, int> distances = distancesFrom(state, intendedCave);
    std::optional<std::pair<int, CaveId>> best;
    for (const CaveId cave : state.world.caveIds()) {
        if (cave == state.basilisk.cave || activePit(state, cave) ||
            !reachable.contains(cave)) continue;
        const auto distance = distances.find(cave);
        if (distance == distances.end()) continue;
        const std::pair candidate{distance->second, cave};
        if (!best || candidate < *best) best = candidate;
    }
    return best ? std::optional<CaveId>{best->second} : std::nullopt;
}

void placeSigilsForDeath(
    MatchState& state, PlayerState& player, CaveId intendedCave,
    std::vector<GameEvent>& events) {
    const std::optional<CaveId> placement =
        nearestRecoverableSigilCave(state, intendedCave);
    auto body = std::find_if(state.bodies.begin(), state.bodies.end(),
        [&](const BodyState& candidate) { return candidate.owner == player.id; });
    if (body == state.bodies.end()) {
        state.bodies.push_back(BodyState{
            player.id, player.cave, placement.has_value(), placement});
        events.push_back(GameEvent{
            GameEventType::BodyCreated, std::nullopt, player.id, player.cave});
        if (placement) emitRelocation(player.id, intendedCave, *placement, events);
    }

    if (player.heldSigilFrom.has_value()) {
        const PlayerId owner = *player.heldSigilFrom;
        auto carriedBody = std::find_if(state.bodies.begin(), state.bodies.end(),
            [&](const BodyState& candidate) { return candidate.owner == owner; });
        if (carriedBody != state.bodies.end()) {
            carriedBody->sigilAvailable = placement.has_value();
            carriedBody->sigilCave = placement;
            if (placement) emitRelocation(owner, intendedCave, *placement, events);
        }
        player.heldSigilFrom.reset();
        state.extraction.active = false;
        state.extraction.cave.reset();
        state.extraction.sigilHolder.reset();
    }
}

} // namespace basilisk
