#include "basilisk/systems/RoundController.hpp"

#include <algorithm>
#include <unordered_map>

#include "basilisk/systems/MapDiscoverySystem.hpp"
#include "basilisk/systems/TurnResolver.hpp"

namespace basilisk {

std::vector<GameEvent> RoundController::resolve(
    MatchState& state,
    const std::vector<PlayerAction>& actions) const {

    for (auto& player : state.players) {
        MapDiscoverySystem::initializePlayer(state, player);
    }

    std::unordered_map<PlayerId, CaveId> trackedCaves;
    for (const auto& player : state.players) {
        trackedCaves[player.id] = player.cave;
    }

    std::vector<PlayerAction> prepared = actions;
    for (auto& action : prepared) {
        if (action.type != ActionType::Move && action.type != ActionType::Shoot) {
            continue;
        }

        const auto it = std::find_if(state.players.begin(), state.players.end(),
            [&](const PlayerState& player) { return player.id == action.player; });
        if (it == state.players.end()) continue;

        const auto destination =
            MapDiscoverySystem::resolveMoveDestination(state, *it, action);

        if (!destination.has_value()) {
            action.targetCave.reset();
            action.targetTunnel.reset();
            continue;
        }

        // The authoritative destination is materialized only for the low-level
        // resolver. The client may have supplied only an opaque TunnelId.
        action.targetCave = *destination;
        action.targetTunnel.reset();
    }

    TurnResolver resolver;
    const auto resolvedEvents = resolver.resolve(state, prepared);

    std::vector<GameEvent> events;
    events.reserve(resolvedEvents.size() * 2);

    for (const auto& event : resolvedEvents) {
        events.push_back(event);

        if (event.type == GameEventType::PlayerMoved &&
            event.actor.has_value() && event.cave.has_value()) {

            auto playerIt = std::find_if(state.players.begin(), state.players.end(),
                [&](const PlayerState& player) { return player.id == *event.actor; });
            if (playerIt == state.players.end()) continue;

            const CaveId from = trackedCaves[*event.actor];
            const CaveId to = *event.cave;

            MapDiscoverySystem::discoverTraversal(*playerIt, from, to, events);
            trackedCaves[*event.actor] = to;
            continue;
        }

        if (event.type == GameEventType::JackalScaredPlayer &&
            event.targetPlayer.has_value() && event.cave.has_value()) {

            auto playerIt = std::find_if(state.players.begin(), state.players.end(),
                [&](const PlayerState& player) { return player.id == *event.targetPlayer; });
            if (playerIt == state.players.end()) continue;

            const CaveId from = trackedCaves[*event.targetPlayer];
            const CaveId to = *event.cave;

            MapDiscoverySystem::discoverTraversal(*playerIt, from, to, events);
            trackedCaves[*event.targetPlayer] = to;
            continue;
        }

        if (event.type == GameEventType::JackalKnockedOutPlayer &&
            event.targetPlayer.has_value() && event.cave.has_value()) {

            auto playerIt = std::find_if(state.players.begin(), state.players.end(),
                [&](const PlayerState& player) { return player.id == *event.targetPlayer; });
            if (playerIt == state.players.end()) continue;

            MapDiscoverySystem::discoverCave(*playerIt, *event.cave, events);
            trackedCaves[*event.targetPlayer] = *event.cave;
        }
    }

    return events;
}

} // namespace basilisk
