#pragma once

#include <optional>
#include <vector>

#include "basilisk/StatusEffect.hpp"
#include "basilisk/Types.hpp"

namespace basilisk {

struct JackalState {
    CaveId cave{};
    std::optional<CaveId> lastCave;
    std::vector<StatusEffect> statuses;
    std::optional<CaveId> fleeOrigin;
    std::optional<PlayerId> protectedHunter;
    int fleeRoundsRemaining{0};
};

} // namespace basilisk
