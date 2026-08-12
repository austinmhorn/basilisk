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

    // Multiplayer timing. Reserve is consumed only while another living hunter
    // is already locked and waiting. Disconnect grace is intentionally
    // configurable; 30 seconds is the initial prototype value.
    std::uint64_t multiplayerReserveMs{300000};
    std::uint64_t disconnectGraceMs{30000};

    // Loose-arrow economy: one world arrow every five rounds, with up to eight
    // arrows waiting on the map at once.
    std::uint32_t looseArrowSpawnIntervalRounds{5};
    std::size_t maxLooseArrows{8};

    // Static/classic maps can expose their complete topology while procedural
    // maps use player-specific discovery and opaque unknown tunnel choices.
    MapDiscoveryMode mapDiscoveryMode{MapDiscoveryMode::FullMap};

    // Searching while a Pit is adjacent can identify the dangerous local
    // tunnel. Initial balance: 75% correct directional clue, 25% inconclusive,
    // and never a deliberately false clue.
    std::uint32_t pitInvestigationNumerator{3};
    std::uint32_t pitInvestigationDenominator{4};

    // Temporary utility-item durations/effects.
    int oldMinersMapRevealRounds{3};
    int jackalRepellentRounds{3};
    int bloodBaitRounds{5};
    std::uint32_t bloodBaitAttractionNumerator{1};
    std::uint32_t bloodBaitAttractionDenominator{2};

    // Reserved Jackal damage capability. Disabled by default so current
    // gameplay remains faithful to the classic ROB / SCARE / KNOCKOUT model.
    bool jackalDamageEnabled{false};
    int jackalDamageMin{5};
    int jackalDamageMax{10};

    // Search loot uses ONE weighted ordinary reward roll. Weights sum to 100
    // by default but are treated as relative weights, so tuning does not depend
    // on that exact total. Loose world arrows remain the ammo economy.
    std::uint32_t searchNothingWeight{65};
    std::uint32_t searchHealingWeight{12};
    std::uint32_t searchJackalRepellentWeight{9};
    std::uint32_t searchOldMinersMapWeight{7};
    std::uint32_t searchSurveyFragmentWeight{5};
    std::uint32_t searchBloodBaitWeight{2};

    // Cosmetic progression is server-authoritative in production. Core keeps
    // this ultra-rare prototype trigger only for probability/event-pipeline
    // testing; it never grants permanent account ownership itself.
    std::uint32_t searchExoticNumerator{1};
    std::uint32_t searchExoticDenominator{1000};
};

} // namespace basilisk
