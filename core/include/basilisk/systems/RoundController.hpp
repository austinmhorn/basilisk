#pragma once

#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"

namespace basilisk {

// Public round orchestration layer. TurnResolver remains concerned only with
// deterministic game-rule ordering; RoundController validates player-visible
// movement choices and updates player-specific knowledge around that resolver.
class RoundController {
public:
    // Resolves a Search/UseItem that is interrupted by an occupancy clash.
    // It deliberately does not advance the round or resolve world phases.
    [[nodiscard]] std::vector<GameEvent> resolveStationaryAction(
        MatchState& state, const PlayerAction& action) const;
    [[nodiscard]] std::vector<GameEvent> resolve(
        MatchState& state,
        const std::vector<PlayerAction>& actions) const;
};

} // namespace basilisk
