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

CaveId chooseExtractionCave(const MatchState& state, CaveId from) {
    CaveId best = from;
    int bestDistance = -1;
    const auto distances = distancesFrom(state, from);
    for (const CaveId cave : state.world.caveIds()) {
        if (cave == from || activePit(state, cave)) continue;
        const auto distance = distances.find(cave);
        if (distance == distances.end()) continue;
        if (distance->second > bestDistance ||
            (distance->second == bestDistance && cave < best)) {
            bestDistance = distance->second;
            best = cave;
        }
    }
    return best;
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

void recoverSigilAtCurrentCave(
    MatchState& state, PlayerState& player, std::vector<GameEvent>& events) {
    if (!player.alive || player.heldSigilFrom.has_value() ||
        state.extraction.sigilHolder.has_value()) return;
    for (auto& body : state.bodies) {
        if (!body.sigilAvailable || body.owner == player.id) continue;
        const CaveId sigilCave = body.sigilCave.value_or(body.cave);
        if (sigilCave != player.cave) continue;
        if (body.cave == player.cave) {
            events.push_back(GameEvent{
                GameEventType::BodyFound, player.id, body.owner, body.cave});
        }
        body.sigilAvailable = false;
        player.heldSigilFrom = body.owner;
        events.push_back(GameEvent{
            GameEventType::SigilAcquired, player.id, body.owner, sigilCave});
        state.extraction.active = true;
        state.extraction.sigilHolder = player.id;
        state.extraction.cave = chooseExtractionCave(state, player.cave);
        events.push_back(GameEvent{
            GameEventType::ExtractionActivated, player.id, std::nullopt,
            state.extraction.cave});
        return;
    }
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
