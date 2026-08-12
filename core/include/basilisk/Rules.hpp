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
    int arrowDamage{40};
    int maxArrows{5};
    int startingArrows{3};
    std::size_t maxInventoryItems{3};
    int healingAmount{40};
    int jackalStunPhases{3};
    int cavesPerJackal{15};

    // Multiplayer timing. Reserve is consumed only while another living hunter
    // is already locked and waiting. Disconnect grace is intentionally
    // configurable; 30 seconds is the initial prototype value.
    std::uint64_t multiplayerReserveMs{300000};
    std::uint64_t disconnectGraceMs{30000};

    // Loose-arrow economy: one world arrow every seven rounds, with up to
    // eight arrows waiting on the map at once during the current prototype.
    std::uint32_t looseArrowSpawnIntervalRounds{7};
    std::size_t maxLooseArrows{8};

    // Static/classic maps can expose their complete topology while procedural
    // maps use player-specific discovery and opaque unknown tunnel choices.
    MapDiscoveryMode mapDiscoveryMode{MapDiscoveryMode::FullMap};

    // Searching while a Pit is adjacent can identify the dangerous local
    // tunnel. Initial balance: 75% correct directional clue, 25% inconclusive,
    // and never a deliberately false clue.
    std::uint32_t pitInvestigationNumerator{3};
    std::uint32_t pitInvestigationDenominator{4};

    // Temporary utility-item durations. Effects begin immediately during the
    // item-use phase and then tick down once at the end of each resolved round.
    int oldMinersMapRevealRounds{3};
    int jackalRepellentRounds{3};

    // Reserved Jackal damage capability. Disabled by default so current
    // gameplay remains faithful to the classic ROB / SCARE / KNOCKOUT model.
    bool jackalDamageEnabled{false};
    int jackalDamageMin{5};
    int jackalDamageMax{10};

    // Search loot. Loose world arrows remain the primary ammunition source, so
    // direct Search arrows stay disabled for this balance pass. Item rolls are
    // independent and inventory capacity still applies.
    std::uint32_t searchArrowNumerator{0};
    std::uint32_t searchArrowDenominator{1};
    std::uint32_t searchHealingNumerator{1};
    std::uint32_t searchHealingDenominator{12};
    std::uint32_t searchOldMinersMapNumerator{1};
    std::uint32_t searchOldMinersMapDenominator{20};
    std::uint32_t searchJackalRepellentNumerator{1};
    std::uint32_t searchJackalRepellentDenominator{16};
    std::uint32_t searchExoticNumerator{1};
    std::uint32_t searchExoticDenominator{1000};
};

} // namespace basilisk
