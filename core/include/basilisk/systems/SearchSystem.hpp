#pragma once

#include <vector>

#include "basilisk/Event.hpp"
#include "basilisk/Player.hpp"
#include "basilisk/Random.hpp"
#include "basilisk/Rules.hpp"

namespace basilisk {

class SearchSystem {
public:
    [[nodiscard]] static std::vector<GameEvent> search(
        PlayerState& player,
        const Rules& rules,
        RandomGenerator& rng);
};

} // namespace basilisk
