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

    // Reserved Jackal damage capability. Disabled by default so current
    // gameplay remains faithful to the classic ROB / SCARE / KNOCKOUT model.
    // These values let us introduce a damaging Jackal attack later without
    // changing the public rules/state shape.
    bool jackalDamageEnabled{false};
    int jackalDamageMin{5};
    int jackalDamageMax{10};

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
