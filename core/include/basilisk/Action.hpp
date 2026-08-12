#pragma once

#include <optional>

#include "basilisk/Types.hpp"

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
};

} // namespace basilisk
