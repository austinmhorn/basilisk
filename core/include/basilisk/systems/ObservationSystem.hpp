#pragma once

#include <vector>

#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Observation.hpp"

namespace basilisk {

class ObservationSystem {
public:
    [[nodiscard]] static std::vector<PlayerObservation> buildForPlayer(
        const MatchState& state,
        PlayerId viewer,
        const std::vector<GameEvent>& events);
};

} // namespace basilisk
