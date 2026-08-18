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
};

} // namespace basilisk
