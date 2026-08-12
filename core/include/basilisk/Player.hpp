#pragma once

#include "basilisk/Types.hpp"

namespace basilisk {

struct PlayerState {
    PlayerId id{};
    CaveId cave{};
    int health{100};
    int arrows{3};
    bool alive{true};
};

} // namespace basilisk
