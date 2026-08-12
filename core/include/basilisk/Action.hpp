#pragma once

#include <optional>

#include "basilisk/Types.hpp"
#include "basilisk/items/Item.hpp"

namespace basilisk {

enum class ActionType {
    Move,
    Search,
    Shoot,
    UseItem,
    Contextual
};

struct PlayerAction {
    PlayerId player{};
    ActionType type{ActionType::Search};
    std::optional<CaveId> targetCave;
    std::optional<ItemType> targetItem;
};

} // namespace basilisk
