#pragma once

#include <cstddef>

namespace basilisk {

struct Rules {
    int maxHealth{100};
    int arrowDamage{40};
    int maxArrows{5};
    int startingArrows{3};
    std::size_t maxInventoryItems{3};
    int healingAmount{40};
    int jackalStunPhases{3};
    int cavesPerJackal{15};
};

} // namespace basilisk
