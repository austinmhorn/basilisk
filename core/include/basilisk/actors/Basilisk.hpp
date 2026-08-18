#pragma once

#include <optional>

#include "basilisk/Types.hpp"

namespace basilisk {

enum class BasiliskBehavior {
    Normal,
    Restless,
    Lurker,
    Skittish,
    Territorial,
    Enraged
};

struct BasiliskState {
    CaveId cave{};
    bool alive{true};

    // A true encounter occurs only when a hunter shoots into the cave
    // currently occupied by the Basilisk.
    int trueEncounters{0};
    BasiliskBehavior behavior{BasiliskBehavior::Normal};

    // Used by movement behaviors and the Enraged last-known-location clue.
    int roundsSinceMove{0};
    std::optional<CaveId> lastCave;
};

} // namespace basilisk
