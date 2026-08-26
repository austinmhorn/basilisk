#pragma once

#include <cstddef>
#include <cstdint>

namespace basilisk {

enum class MapDiscoveryMode {
    FullMap,
    FogOfWar
};

struct Rules {
    int maxHealth{100};
    int arrowDamage{50};
    int maxArrows{5};
    int startingArrows{3};
    std::size_t maxInventoryItems{3};
    int healingAmount{50};
    int jackalStunPhases{3};
    int cavesPerJackal{15};

    std::uint64_t multiplayerReserveMs{300000};
    std::uint64_t disconnectGraceMs{30000};
    int clashDamage{20};
    std::uint64_t clashTimeoutMs{8000};

    std::uint32_t looseArrowSpawnIntervalRounds{5};
    std::size_t maxLooseArrows{8};

    MapDiscoveryMode mapDiscoveryMode{MapDiscoveryMode::FullMap};

    std::uint32_t pitInvestigationNumerator{3};
    std::uint32_t pitInvestigationDenominator{4};

    int oldMinersMapRevealRounds{3};
    int jackalRepellentRounds{3};
    int bloodBaitRounds{5};
    int surveyFragmentRevealMin{3};
    int surveyFragmentRevealMax{5};
    std::uint32_t bloodBaitAttractionNumerator{1};
    std::uint32_t bloodBaitAttractionDenominator{2};

    bool jackalDamageEnabled{true};
    int jackalDamageMin{5};
    int jackalDamageMax{5};

    // Search loot uses ONE weighted ordinary reward roll. V3.13 adds Old
    // Hunter's Map at 5 weight by moving 5 points from Nothing; existing item
    // odds remain unchanged.
    std::uint32_t searchNothingWeight{60};
    std::uint32_t searchHealingWeight{12};
    std::uint32_t searchJackalRepellentWeight{9};
    std::uint32_t searchOldMinersMapWeight{7};
    std::uint32_t searchSurveyFragmentWeight{5};
    std::uint32_t searchBloodBaitWeight{2};
    std::uint32_t searchOldHuntersMapWeight{5};

    std::uint32_t searchExoticNumerator{1};
    std::uint32_t searchExoticDenominator{1000};
};

} // namespace basilisk
