#pragma once

#include <vector>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"

namespace basilisk {

class SnapshotSystem {
public:
    [[nodiscard]] static PlayerRoundSnapshot buildForPlayer(
        const MatchState& state,
        PlayerId viewer,
        const std::vector<GameEvent>& events);
};

} // namespace basilisk
