#include "basilisk/systems/RoundController.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

#include "basilisk/Random.hpp"
#include "basilisk/systems/LooseArrowSystem.hpp"
#include "basilisk/systems/MapDiscoverySystem.hpp"
#include "basilisk/systems/PitInvestigationSystem.hpp"
#include "basilisk/systems/TurnResolver.hpp"

namespace basilisk {
namespace {

std::uint64_t looseArrowSeed(const MatchState& state) {
    return static_cast<std::uint64_t>(state.matchSeed) ^
        (static_cast<std::uint64_t>(state.mapSeed) << 1U) ^
        (static_cast<std::uint64_t>(state.round) * 0xD6E8FEB86659FD93ULL);
}

void tickTemporaryEffects(MatchState& state) {
    for (auto& player : state.players) {
        if (player.pitMapRevealRounds > 0) --player.pitMapRevealRounds;
        if (player.jackalRepellentRounds > 0) --player.jackalRepellentRounds;
    }
}

} // namespace

std::vector<GameEvent> RoundController::resolve(
    MatchState& state,
    const std::vector<PlayerAction>& actions) const {

    // Effects activated during the previous resolution remain visible until
    // the player commits their next round, then spend one remaining round.
    tickTemporaryEffects(state);

    for (auto& player : state.players) {
        MapDiscoverySystem::initializePlayer(state, player);
    }

    std::unordered_map<PlayerId, CaveId> trackedCaves;
    for (const auto& player : state.players) trackedCaves[player.id] = player.cave;

    std::vector<PlayerAction> prepared = actions;
    for (auto& action : prepared) {
        if (action.type != ActionType::Move && action.type != ActionType::Shoot) continue;

        const auto it = std::find_if(state.players.begin(), state.players.end(),
            [&](const PlayerState& player) { return player.id == action.player; });
        if (it == state.players.end()) continue;

        const auto destination = MapDiscoverySystem::resolveMoveDestination(state, *it, action);
        if (!destination.has_value()) {
            action.targetCave.reset();
            action.targetTunnel.reset();
            continue;
        }
        action.targetCave = *destination;
        action.targetTunnel.reset();
    }

    // Pit investigation is deliberately separate from ordinary cave loot.
    // A hunter may retry an inconclusive investigation on a previously searched
    // cave, and this system uses its own deterministic RNG stream.
    auto investigationEvents = PitInvestigationSystem::resolve(state, prepared);

    TurnResolver resolver;
    const auto resolvedEvents = resolver.resolve(state, prepared);

    std::vector<GameEvent> events;
    events.reserve(investigationEvents.size() + resolvedEvents.size() * 2 + 4);
    events.insert(events.end(), investigationEvents.begin(), investigationEvents.end());

    for (const auto& event : resolvedEvents) {
        events.push_back(event);

        if (event.type == GameEventType::PlayerMoved && event.actor.has_value() && event.cave.has_value()) {
            auto playerIt = std::find_if(state.players.begin(), state.players.end(),
                [&](const PlayerState& player) { return player.id == *event.actor; });
            if (playerIt == state.players.end()) continue;
            const CaveId from = trackedCaves[*event.actor];
            const CaveId to = *event.cave;
            MapDiscoverySystem::discoverTraversal(*playerIt, from, to, events);
            trackedCaves[*event.actor] = to;
            continue;
        }

        if (event.type == GameEventType::JackalScaredPlayer && event.targetPlayer.has_value() && event.cave.has_value()) {
            auto playerIt = std::find_if(state.players.begin(), state.players.end(),
                [&](const PlayerState& player) { return player.id == *event.targetPlayer; });
            if (playerIt == state.players.end()) continue;
            const CaveId from = trackedCaves[*event.targetPlayer];
            const CaveId to = *event.cave;
            MapDiscoverySystem::discoverTraversal(*playerIt, from, to, events);
            trackedCaves[*event.targetPlayer] = to;
            continue;
        }

        if (event.type == GameEventType::JackalKnockedOutPlayer && event.targetPlayer.has_value() && event.cave.has_value()) {
            auto playerIt = std::find_if(state.players.begin(), state.players.end(),
                [&](const PlayerState& player) { return player.id == *event.targetPlayer; });
            if (playerIt == state.players.end()) continue;
            MapDiscoverySystem::discoverCave(*playerIt, *event.cave, events);
            trackedCaves[*event.targetPlayer] = *event.cave;
        }
    }

    // Original-game loose arrows are collected at the hunter's final cave for
    // the round, including Jackal-forced movement, then a new arrow may spawn.
    LooseArrowSystem::collectForPlayers(state, events);
    RandomGenerator arrowRng{looseArrowSeed(state)};
    LooseArrowSystem::spawnForRound(state, arrowRng, events);

    return events;
}

} // namespace basilisk
