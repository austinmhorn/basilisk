#pragma once

#include <optional>

#include "basilisk/Action.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Random.hpp"
#include "basilisk/world/DiscoveryState.hpp"

namespace basilisk {

class MapDiscoverySystem {
public:
    static void initializePlayer(MatchState& state, PlayerState& player);

    [[nodiscard]] static std::optional<CaveId> resolveMoveDestination(
        const MatchState& state,
        const PlayerState& player,
        const PlayerAction& action);

    static void discoverTraversal(
        PlayerState& player,
        CaveId from,
        CaveId to,
        std::vector<GameEvent>& events);

    static void discoverCave(
        PlayerState& player,
        CaveId cave,
        std::vector<GameEvent>& events);

    [[nodiscard]] static bool hasSurveyFrontier(
        const MatchState& state,
        const PlayerState& player);

    [[nodiscard]] static std::size_t surveyFrontier(
        const MatchState& state,
        PlayerState& player,
        std::size_t revealCount,
        RandomGenerator& random,
        std::vector<GameEvent>& events);

    [[nodiscard]] static bool revealTunnelDestination(
        const MatchState& state,
        PlayerState& player,
        CaveId from,
        TunnelId tunnel,
        std::vector<GameEvent>& events);

    [[nodiscard]] static PlayerMapView buildView(
        const MatchState& state,
        const PlayerState& player);

    [[nodiscard]] static bool knowsConnection(
        const PlayerState& player,
        CaveId a,
        CaveId b);
};

} // namespace basilisk
