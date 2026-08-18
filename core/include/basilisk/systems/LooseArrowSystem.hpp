#pragma once

#include <vector>

#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Random.hpp"

namespace basilisk {

class LooseArrowSystem {
public:
    static void collectForPlayers(MatchState& state, std::vector<GameEvent>& events);
    static void spawnForRound(MatchState& state, RandomGenerator& rng,
                              std::vector<GameEvent>& events);
};

} // namespace basilisk
