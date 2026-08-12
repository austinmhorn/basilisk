#include "basilisk/systems/LooseArrowSystem.hpp"

#include <algorithm>

namespace basilisk {
namespace {

bool caveHasActivePit(const MatchState& state, CaveId cave) {
    return std::any_of(state.pits.begin(), state.pits.end(),
        [cave](const PitState& pit) { return pit.active && pit.cave == cave; });
}

bool caveHasLivingPlayer(const MatchState& state, CaveId cave) {
    return std::any_of(state.players.begin(), state.players.end(),
        [cave](const PlayerState& player) { return player.alive && player.cave == cave; });
}

} // namespace

void LooseArrowSystem::collectForPlayers(MatchState& state, std::vector<GameEvent>& events) {
    for (auto& player : state.players) {
        if (!player.alive || player.arrows >= state.rules.maxArrows) continue;

        const auto it = std::find(state.looseArrows.begin(), state.looseArrows.end(), player.cave);
        if (it == state.looseArrows.end()) continue;

        ++player.arrows;
        state.looseArrows.erase(it);
        events.push_back(GameEvent{
            GameEventType::ArrowFound,
            player.id,
            std::nullopt,
            player.cave,
            1
        });
    }
}

void LooseArrowSystem::spawnForRound(
    MatchState& state,
    RandomGenerator& rng,
    std::vector<GameEvent>& events) {

    const auto interval = state.rules.looseArrowSpawnIntervalRounds;
    if (interval == 0 || state.round == 0 || state.round % interval != 0) return;
    if (state.looseArrows.size() >= state.rules.maxLooseArrows) return;

    std::vector<CaveId> candidates;
    for (const CaveId cave : state.world.caveIds()) {
        if (state.basilisk.alive && cave == state.basilisk.cave) continue;
        if (caveHasActivePit(state, cave)) continue;
        if (caveHasLivingPlayer(state, cave)) continue;
        if (std::find(state.looseArrows.begin(), state.looseArrows.end(), cave) != state.looseArrows.end()) continue;
        candidates.push_back(cave);
    }

    if (candidates.empty()) return;
    const auto index = static_cast<std::size_t>(
        rng.range(0, static_cast<int>(candidates.size()) - 1));
    const CaveId cave = candidates[index];
    state.looseArrows.push_back(cave);

    // The cave is authoritative information. Clients can choose to expose only
    // the original game's non-positional "magical chime" notification.
    events.push_back(GameEvent{GameEventType::LooseArrowSpawned, std::nullopt, std::nullopt, cave, 1});
}

} // namespace basilisk
