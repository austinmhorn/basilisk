#pragma once

#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"

namespace basilisk {

// Network-agnostic solo orchestration. Solo play has no reserve clock and no
// opponent lock-step: a valid action resolves immediately as one game round.
class SoloCoordinator {
public:
    explicit SoloCoordinator(MatchState& state) : state_(state) {}

    [[nodiscard]] bool submitAction(const PlayerAction& action);
    [[nodiscard]] const std::vector<GameEvent>& lastEvents() const { return lastEvents_; }

private:
    MatchState& state_;
    std::vector<GameEvent> lastEvents_;
};

} // namespace basilisk
