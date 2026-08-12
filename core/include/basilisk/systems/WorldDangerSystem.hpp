#pragma once

#include <vector>

#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Random.hpp"

namespace basilisk {

class WorldDangerSystem {
public:
    static void resolvePits(
        MatchState& state,
        std::vector<GameEvent>& events);

    static void resolveJackals(
        MatchState& state,
        RandomGenerator& rng,
        std::vector<GameEvent>& events);
};

} // namespace basilisk
