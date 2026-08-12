#pragma once

#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"
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

    [[nodiscard]] static std::vector<GameEvent> use(
        MatchState& state,
        PlayerState& player,
        const PlayerAction& action);
};

} // namespace basilisk
