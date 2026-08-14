#pragma once

#include "basilisk/MatchState.hpp"

namespace basilisk {

[[nodiscard]] bool isExtractionVisibleTo(
    const MatchState& state,
    const PlayerState& player);

} // namespace basilisk
