#pragma once

#include <optional>

#include "basilisk/Types.hpp"

namespace basilisk {

struct BodyState {
    PlayerId owner{};
    CaveId cave{};
    bool sigilAvailable{true};

    // Normally the Sigil remains with the body. Hazard deaths such as falling
    // into a Pit may eject it into a different, reachable cave.
    std::optional<CaveId> sigilCave;
};

} // namespace basilisk
