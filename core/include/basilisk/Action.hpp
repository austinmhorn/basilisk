#pragma once

#include <optional>

#include "basilisk/Types.hpp"
#include "basilisk/items/Item.hpp"
#include "basilisk/world/DiscoveryState.hpp"

namespace basilisk {

enum class ActionType {
    Move,
    Search,
    Shoot,
    UseItem,
    Contextual
};

enum class ContextualActionType {
    Escape
};

struct PlayerAction {
    PlayerId player{};
    ActionType type{ActionType::Search};
    std::optional<CaveId> targetCave;
    std::optional<ItemType> targetItem;
    std::optional<ContextualActionType> contextualAction;

    // Used when the destination is intentionally hidden by fog of war. The
    // value identifies an exit from the player's current cave without exposing
    // the destination CaveId to the client.
    std::optional<TunnelId> targetTunnel;
};

} // namespace basilisk
