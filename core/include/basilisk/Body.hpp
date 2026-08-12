#pragma once

#include "basilisk/Types.hpp"

namespace basilisk {

struct BodyState {
    PlayerId owner{};
    CaveId cave{};
    bool sigilAvailable{true};
};

} // namespace basilisk
