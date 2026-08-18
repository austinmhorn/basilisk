#pragma once

#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"

namespace basilisk {

class TurnResolver {
public:
    [[nodiscard]] std::vector<GameEvent> resolve(
        MatchState& state,
        const std::vector<PlayerAction>& actions) const;
};

} // namespace basilisk
