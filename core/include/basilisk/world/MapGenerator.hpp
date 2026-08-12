#pragma once

#include <cstddef>

#include "basilisk/MatchState.hpp"

namespace basilisk {

struct ProceduralMapConfig {
    std::size_t caveCount{30};
    std::size_t minDegree{2};
    std::size_t maxDegree{3};
    std::size_t extraConnections{8};
    std::size_t pitCount{1};

    int minHunterSeparation{6};
    int minHunterBasiliskDistance{4};
    int maxHunterBasiliskDistanceDelta{2};

    std::size_t maxGenerationAttempts{128};
};

class MapGenerator {
public:
    [[nodiscard]] static MatchState generate(
        MapSeed mapSeed,
        MatchSeed matchSeed,
        const Rules& rules = {},
        const ProceduralMapConfig& config = {});

    [[nodiscard]] static bool validateTopology(
        const WorldGraph& world,
        const ProceduralMapConfig& config);

    [[nodiscard]] static bool validateFairness(
        const MatchState& state,
        const ProceduralMapConfig& config);
};

} // namespace basilisk
