#include "basilisk/systems/PitInvestigationSystem.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "basilisk/Random.hpp"

namespace basilisk {
namespace {

constexpr std::uint64_t kPitInvestigationSalt = 0xA24BAED4963EE407ULL;

std::uint64_t investigationSeed(const MatchState& state) {
    return state.matchSeed ^ kPitInvestigationSalt ^
        (static_cast<std::uint64_t>(state.round) * 0x9E3779B97F4A7C15ULL);
}

PlayerState* findPlayer(MatchState& state, PlayerId id) {
    const auto it = std::find_if(state.players.begin(), state.players.end(),
        [id](const PlayerState& player) { return player.id == id; });
    return it == state.players.end() ? nullptr : &*it;
}

std::optional<TunnelId> adjacentPitTunnel(const MatchState& state, CaveId cave) {
    if (!state.world.contains(cave)) return std::nullopt;
    const auto& connections = state.world.cave(cave).connections;

    for (std::size_t index = 0; index < connections.size(); ++index) {
        const CaveId destination = connections[index];
        const bool hasPit = std::any_of(state.pits.begin(), state.pits.end(),
            [destination](const PitState& pit) {
                return pit.active && pit.cave == destination;
            });
        if (hasPit) return static_cast<TunnelId>(index + 1);
    }
    return std::nullopt;
}

} // namespace

std::vector<GameEvent> PitInvestigationSystem::resolve(
    MatchState& state,
    const std::vector<PlayerAction>& actions) {

    std::vector<GameEvent> events;
    RandomGenerator rng(investigationSeed(state));

    for (const auto& action : actions) {
        if (action.type != ActionType::Search) continue;
        auto* player = findPlayer(state, action.player);
        if (player == nullptr || !player->alive) continue;

        const auto pitTunnel = adjacentPitTunnel(state, player->cave);
        if (!pitTunnel.has_value()) continue;

        const auto known = player->knownPitTunnels.find(player->cave);
        if (known != player->knownPitTunnels.end()) {
            GameEvent event{GameEventType::PitInvestigationSucceeded, player->id,
                std::nullopt, player->cave};
            event.tunnel = known->second;
            events.push_back(event);
            continue;
        }

        if (rng.chance(state.rules.pitInvestigationNumerator,
                       state.rules.pitInvestigationDenominator)) {
            player->knownPitTunnels[player->cave] = *pitTunnel;
            GameEvent event{GameEventType::PitInvestigationSucceeded, player->id,
                std::nullopt, player->cave};
            event.tunnel = *pitTunnel;
            events.push_back(event);
        } else {
            events.push_back(GameEvent{GameEventType::PitInvestigationInconclusive,
                player->id, std::nullopt, player->cave});
        }
    }

    return events;
}

} // namespace basilisk
