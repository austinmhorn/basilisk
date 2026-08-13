#include "basilisk/systems/MapDiscoverySystem.hpp"

#include <algorithm>
#include <cstdint>

namespace basilisk {
namespace {

std::uint64_t connectionKey(CaveId a, CaveId b) {
    const auto low = std::min(a, b);
    const auto high = std::max(a, b);
    return (static_cast<std::uint64_t>(low) << 32U) |
           static_cast<std::uint64_t>(high);
}

bool connectionIsKnownOrInferred(
    const PlayerState& player,
    CaveId a,
    CaveId b) {

    return player.discovery.knownConnections.contains(connectionKey(a, b)) ||
           (player.discovery.knownCaves.contains(a) &&
            player.discovery.knownCaves.contains(b));
}

} // namespace

void MapDiscoverySystem::initializePlayer(MatchState& state, PlayerState& player) {
    if (state.rules.mapDiscoveryMode == MapDiscoveryMode::FullMap) {
        for (const CaveId cave : state.world.caveIds()) {
            player.discovery.knownCaves.insert(cave);
            for (const CaveId destination : state.world.cave(cave).connections) {
                player.discovery.knownConnections.insert(connectionKey(cave, destination));
            }
        }
        return;
    }
    player.discovery.knownCaves.insert(player.cave);
}

std::optional<CaveId> MapDiscoverySystem::resolveMoveDestination(
    const MatchState& state,
    const PlayerState& player,
    const PlayerAction& action) {

    if (!state.world.contains(player.cave)) return std::nullopt;

    if (action.targetTunnel.has_value()) {
        const auto& connections = state.world.cave(player.cave).connections;
        const TunnelId tunnel = *action.targetTunnel;
        if (tunnel == 0 || tunnel > connections.size()) return std::nullopt;
        return connections[static_cast<std::size_t>(tunnel - 1)];
    }

    if (!action.targetCave.has_value() ||
        !state.world.areConnected(player.cave, *action.targetCave)) {
        return std::nullopt;
    }

    if (state.rules.mapDiscoveryMode == MapDiscoveryMode::FogOfWar &&
        !connectionIsKnownOrInferred(player, player.cave, *action.targetCave)) {
        return std::nullopt;
    }

    return *action.targetCave;
}

void MapDiscoverySystem::discoverTraversal(
    PlayerState& player,
    CaveId from,
    CaveId to,
    std::vector<GameEvent>& events) {

    const bool caveWasNew = player.discovery.knownCaves.insert(to).second;
    const bool connectionWasNew =
        player.discovery.knownConnections.insert(connectionKey(from, to)).second;

    if (caveWasNew) {
        events.push_back(GameEvent{GameEventType::CaveDiscovered, player.id, std::nullopt, to});
    }

    if (connectionWasNew) {
        events.push_back(GameEvent{
            GameEventType::TunnelDestinationRevealed,
            player.id,
            std::nullopt,
            to,
            static_cast<int>(from)
        });
    }
}

void MapDiscoverySystem::discoverCave(
    PlayerState& player,
    CaveId cave,
    std::vector<GameEvent>& events) {

    if (!player.discovery.knownCaves.insert(cave).second) return;
    events.push_back(GameEvent{GameEventType::CaveDiscovered, player.id, std::nullopt, cave});
}

bool MapDiscoverySystem::revealTunnelDestination(
    const MatchState& state,
    PlayerState& player,
    CaveId from,
    TunnelId tunnel,
    std::vector<GameEvent>& events) {

    if (!state.world.contains(from) || tunnel == 0) return false;
    const auto& connections = state.world.cave(from).connections;
    if (tunnel > connections.size()) return false;

    const CaveId destination = connections[static_cast<std::size_t>(tunnel - 1)];
    const auto key = connectionKey(from, destination);
    if (player.discovery.knownConnections.contains(key)) return false;

    player.discovery.knownConnections.insert(key);
    const bool caveWasNew = player.discovery.knownCaves.insert(destination).second;
    if (caveWasNew) {
        events.push_back(GameEvent{GameEventType::CaveDiscovered, player.id, std::nullopt, destination});
    }

    GameEvent reveal{GameEventType::TunnelDestinationRevealed, player.id, std::nullopt, destination};
    reveal.amount = static_cast<int>(from);
    reveal.tunnel = tunnel;
    events.push_back(reveal);
    return true;
}

PlayerMapView MapDiscoverySystem::buildView(
    const MatchState& state,
    const PlayerState& player) {

    PlayerMapView view;
    view.currentCave = player.cave;

    std::vector<CaveId> knownCaves(
        player.discovery.knownCaves.begin(),
        player.discovery.knownCaves.end());
    std::sort(knownCaves.begin(), knownCaves.end());

    for (const CaveId caveId : knownCaves) {
        if (!state.world.contains(caveId)) continue;

        DiscoveredCaveView caveView;
        caveView.cave = caveId;
        const auto& connections = state.world.cave(caveId).connections;
        const auto pitClue = player.knownPitTunnels.find(caveId);

        for (std::size_t index = 0; index < connections.size(); ++index) {
            const CaveId destination = connections[index];
            const bool destinationIsKnown =
                state.rules.mapDiscoveryMode == MapDiscoveryMode::FullMap ||
                connectionIsKnownOrInferred(player, caveId, destination);

            // Reaching a fatal cave still records the cave and the tunnel used,
            // but death must not reveal its previously unseen exits.
            if (!player.alive && caveId == player.cave && !destinationIsKnown) {
                continue;
            }

            TunnelView tunnel;
            tunnel.id = static_cast<TunnelId>(index + 1);
            tunnel.strongColdDraft = pitClue != player.knownPitTunnels.end() && pitClue->second == tunnel.id;

            if (destinationIsKnown) {
                tunnel.destination = destination;
            }

            caveView.exits.push_back(tunnel);
        }

        view.caves.push_back(std::move(caveView));
    }

    return view;
}

bool MapDiscoverySystem::knowsConnection(
    const PlayerState& player,
    CaveId a,
    CaveId b) {
    return player.discovery.knownConnections.contains(connectionKey(a, b));
}

} // namespace basilisk
