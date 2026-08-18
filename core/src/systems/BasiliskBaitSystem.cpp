#include "basilisk/systems/BasiliskBaitSystem.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>

namespace basilisk {
namespace {

bool basiliskMovedThisRound(const std::vector<GameEvent>& events) {
    return std::any_of(events.begin(), events.end(),
        [](const GameEvent& event) { return event.type == GameEventType::BasiliskMoved; });
}

bool livingPlayerInCave(const MatchState& state, CaveId cave) {
    return std::any_of(state.players.begin(), state.players.end(),
        [cave](const PlayerState& player) { return player.alive && player.cave == cave; });
}

std::optional<int> distanceTo(const WorldGraph& world, CaveId start, CaveId target) {
    if (!world.contains(start) || !world.contains(target)) return std::nullopt;
    if (start == target) return 0;

    std::queue<CaveId> q;
    std::unordered_map<CaveId, int> distance;
    q.push(start);
    distance.emplace(start, 0);

    while (!q.empty()) {
        const CaveId current = q.front(); q.pop();
        const int d = distance.at(current);
        for (const CaveId next : world.cave(current).connections) {
            if (distance.contains(next)) continue;
            if (next == target) return d + 1;
            distance.emplace(next, d + 1);
            q.push(next);
        }
    }
    return std::nullopt;
}

} // namespace

void BasiliskBaitSystem::resolve(
    MatchState& state,
    RandomGenerator& rng,
    const std::vector<GameEvent>& eventsThisRound,
    std::vector<GameEvent>& events) {

    if (!state.basiliskBaitCave.has_value() || state.basiliskBaitRounds <= 0) return;

    const CaveId baitCave = *state.basiliskBaitCave;
    const bool canInfluence = state.result.status == MatchStatus::Active &&
        state.basilisk.alive && !basiliskMovedThisRound(eventsThisRound) &&
        state.basilisk.cave != baitCave;

    if (canInfluence && rng.chance(
            state.rules.bloodBaitAttractionNumerator,
            state.rules.bloodBaitAttractionDenominator)) {

        int bestDistance = std::numeric_limits<int>::max();
        std::vector<CaveId> best;
        for (const CaveId next : state.world.cave(state.basilisk.cave).connections) {
            if (livingPlayerInCave(state, next)) continue;
            const auto distance = distanceTo(state.world, next, baitCave);
            if (!distance.has_value()) continue;
            if (*distance < bestDistance) {
                bestDistance = *distance;
                best = {next};
            } else if (*distance == bestDistance) {
                best.push_back(next);
            }
        }

        if (!best.empty()) {
            const auto index = static_cast<std::size_t>(
                rng.range(0, static_cast<int>(best.size()) - 1));
            const CaveId destination = best[index];
            const CaveId oldCave = state.basilisk.cave;
            state.basilisk.lastCave = oldCave;
            state.basilisk.cave = destination;
            state.basilisk.roundsSinceMove = 0;

            events.push_back(GameEvent{
                GameEventType::BasiliskBaitInfluencedMove,
                std::nullopt,
                std::nullopt,
                destination,
                static_cast<int>(oldCave),
                state.basilisk.behavior,
                ItemType::BloodBait
            });
            events.push_back(GameEvent{
                GameEventType::BasiliskMoved,
                std::nullopt,
                std::nullopt,
                destination,
                static_cast<int>(oldCave),
                state.basilisk.behavior
            });
        }
    }

    --state.basiliskBaitRounds;
    if (state.basiliskBaitRounds <= 0) {
        state.basiliskBaitRounds = 0;
        state.basiliskBaitCave.reset();
    }
}

} // namespace basilisk
