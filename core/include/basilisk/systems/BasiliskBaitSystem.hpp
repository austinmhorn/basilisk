#pragma once

#include <vector>

#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Random.hpp"

namespace basilisk {

class BasiliskBaitSystem {
public:
    static void resolve(
        MatchState& state,
        RandomGenerator& rng,
        const std::vector<GameEvent>& eventsThisRound,
        std::vector<GameEvent>& events);
};

} // namespace basilisk
