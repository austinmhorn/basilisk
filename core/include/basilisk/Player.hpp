#pragma once

#include <optional>
#include <unordered_set>

#include "basilisk/Types.hpp"
#include "basilisk/items/Inventory.hpp"
#include "basilisk/world/DiscoveryState.hpp"

namespace basilisk {

struct PlayerState {
    PlayerId id{};
    CaveId cave{};
    int health{100};
    int arrows{3};
    bool alive{true};
    Inventory inventory;
    std::unordered_set<CaveId> searchedCaves;
    std::optional<PlayerId> heldSigilFrom;
    DiscoveryState discovery;
};

} // namespace basilisk
