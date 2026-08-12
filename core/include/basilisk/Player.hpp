#pragma once

#include <unordered_set>

#include "basilisk/Types.hpp"
#include "basilisk/items/Inventory.hpp"

namespace basilisk {

struct PlayerState {
    PlayerId id{};
    CaveId cave{};
    int health{100};
    int arrows{3};
    bool alive{true};
    Inventory inventory;
    std::unordered_set<CaveId> searchedCaves;
};

} // namespace basilisk
