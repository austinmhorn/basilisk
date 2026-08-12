#pragma once

#include <vector>

#include "basilisk/StatusEffect.hpp"
#include "basilisk/Types.hpp"

namespace basilisk {

struct JackalState {
    CaveId cave{};
    std::vector<StatusEffect> statuses;
};

} // namespace basilisk
