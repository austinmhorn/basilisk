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

    // Original-game loose arrow economy: one world arrow every seven rounds.
    // V2.6 balance experiment raises the simultaneous loose-arrow pool from
    // four to eight while leaving spawn frequency unchanged.
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
