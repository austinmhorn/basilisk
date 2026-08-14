#pragma once

#include "basilisk/ClientSnapshot.hpp"

namespace basilisk::game::demo {

// Development-only player-safe data for manually exercising map rendering.
[[nodiscard]] PlayerRoundSnapshot makeDemoMapSnapshot();

} // namespace basilisk::game::demo
