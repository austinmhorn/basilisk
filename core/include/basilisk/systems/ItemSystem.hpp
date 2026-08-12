#pragma once

#include <vector>

#include "basilisk/Event.hpp"
#include "basilisk/Player.hpp"
#include "basilisk/Rules.hpp"
#include "basilisk/items/Item.hpp"

namespace basilisk {

class ItemSystem {
public:
    [[nodiscard]] static std::vector<GameEvent> use(
        PlayerState& player,
        ItemType item,
        const Rules& rules);
};

} // namespace basilisk
