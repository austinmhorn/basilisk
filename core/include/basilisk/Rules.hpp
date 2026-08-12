#pragma once

#include <cstddef>
#include <cstdint>

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

    // Search loot probabilities are intentionally configurable and default to
    // disabled until balancing establishes production drop rates.
    std::uint32_t searchArrowNumerator{0};
    std::uint32_t searchArrowDenominator{1};
    std::uint32_t searchHealingNumerator{0};
    std::uint32_t searchHealingDenominator{1};
    std::uint32_t searchExoticNumerator{0};
    std::uint32_t searchExoticDenominator{1};
};

} // namespace basilisk
