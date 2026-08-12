#include "basilisk/systems/SoloCoordinator.hpp"

#include <algorithm>

#include "basilisk/MatchResult.hpp"
#include "basilisk/systems/RoundController.hpp"

namespace basilisk {

bool SoloCoordinator::submitAction(const PlayerAction& action) {
    lastEvents_.clear();
    if (state_.result.status != MatchStatus::Active) return false;

    const auto player = std::find_if(state_.players.begin(), state_.players.end(),
        [&](const PlayerState& candidate) { return candidate.id == action.player; });
    if (player == state_.players.end() || !player->alive) return false;

    RoundController controller;
    lastEvents_ = controller.resolve(state_, {action});
    return true;
}

} // namespace basilisk
