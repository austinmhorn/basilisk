#pragma once

#include <optional>
#include <unordered_map>
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

    // Successful Pit investigations persist as player knowledge. The TunnelId
    // is local to the keyed cave and does not reveal the hidden destination.
    std::unordered_map<CaveId, TunnelId> knownPitTunnels;

    // Temporary utility effects. Positive values mean the effect is active for
    // the current round; TurnResolver ticks them down after round resolution.
    int pitMapRevealRounds{0};
    int jackalRepellentRounds{0};
};

} // namespace basilisk
