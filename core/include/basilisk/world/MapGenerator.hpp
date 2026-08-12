#pragma once

#include <cstddef>

#include "basilisk/MatchState.hpp"

namespace basilisk {

struct ProceduralMapConfig {
    std::size_t caveCount{30};
    std::size_t minDegree{1};
    std::size_t maxDegree{3};

    // Organic topology controls. The generator keeps a loop-rich core, then
    // attaches controlled dead-end branches and finally adds extra cross-links.
    std::size_t targetDeadEnds{4};
    std::size_t extraConnections{8};
    std::size_t minDeadEnds{3};
    std::size_t maxDeadEnds{6};
    std::size_t minLoopCount{6};
    int minDiameter{6};
    int maxDiameter{20};

    std::size_t pitCount{1};

    int minHunterSeparation{6};
    int minHunterBasiliskDistance{4};
    int maxHunterBasiliskDistanceDelta{2};

    std::size_t maxGenerationAttempts{128};
};

struct MapTopologyMetrics {
    std::size_t caveCount{0};
    std::size_t edgeCount{0};
    std::size_t deadEndCount{0};
    std::size_t loopCount{0};
    int diameter{0};
    double averageDegree{0.0};
};

class MapGenerator {
public:
    [[nodiscard]] static MatchState generate(
        MapSeed mapSeed,
        MatchSeed matchSeed,
        const Rules& rules = {},
        const ProceduralMapConfig& config = {});

    [[nodiscard]] static MapTopologyMetrics analyzeTopology(
        const WorldGraph& world);

    [[nodiscard]] static bool validateTopology(
        const WorldGraph& world,
        const ProceduralMapConfig& config);

    [[nodiscard]] static bool validateFairness(
        const MatchState& state,
        const ProceduralMapConfig& config);
};

} // namespace basilisk
