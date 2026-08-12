#pragma once

#include <optional>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/Observation.hpp"
#include "basilisk/Types.hpp"
#include "basilisk/items/Item.hpp"
#include "basilisk/world/DiscoveryState.hpp"

namespace basilisk {

struct AvailableAction {
    ActionType type{ActionType::Search};
    std::optional<CaveId> targetCave;
    std::optional<TunnelId> targetTunnel;
    std::optional<ItemType> targetItem;
    std::optional<ContextualActionType> contextualAction;
};

struct InventoryView {
    std::vector<ItemType> items;
    std::size_t capacity{0};
};

struct PlayerRoundSnapshot {
    PlayerId player{};
    RoundNumber round{};

    int health{0};
    int maxHealth{0};
    int arrows{0};
    int maxArrows{0};
    bool alive{false};

    InventoryView inventory;
    CaveId currentCave{};
    PlayerMapView map;

    // Temporary exact Pit markers supplied by utility items such as the Old
    // Miner's Map. These do not mutate permanent fog-of-war discovery.
    std::vector<CaveId> temporarilyRevealedPitCaves;

    std::vector<AvailableAction> availableActions;
    std::vector<PlayerObservation> observations;

    bool hasHunterSigil{false};
    std::optional<CaveId> extractionCave;

    MatchStatus matchStatus{MatchStatus::Active};
    MatchOutcome matchOutcome{MatchOutcome::None};
    std::optional<PlayerId> winner;
};

} // namespace basilisk
