#pragma once

#include <vector>

#include "basilisk/Types.hpp"

namespace basilisk {

struct Cave {
    CaveId id{};
    std::vector<CaveId> connections;
};

} // namespace basilisk
