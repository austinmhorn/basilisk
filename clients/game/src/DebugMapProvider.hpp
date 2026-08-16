#pragma once

#ifndef BASILISK_GAME_DEBUG_BUILD
#error "DebugMapProvider is available only to BasiliskGameDebug"
#endif

#include <compare>
#include <map>
#include <vector>

#include "MapLayout.hpp"
#include "basilisk/Types.hpp"

namespace basilisk::game::debug {

struct PhysicalTunnel {
    CaveId first{};
    CaveId second{};

    auto operator<=>(const PhysicalTunnel&) const = default;
};

struct DebugMapTruth {
    LogicalBounds fullBounds;
    std::map<CaveId, LogicalPoint> cavePositions;
    std::vector<PhysicalTunnel> tunnels;
};

class DebugMapProvider {
public:
    explicit DebugMapProvider(DebugMapTruth truth);

    [[nodiscard]] const DebugMapTruth& truth() const noexcept;

private:
    DebugMapTruth truth_;
};

class DebugMapRevealState {
public:
    void toggle() noexcept;
    [[nodiscard]] bool revealed() const noexcept;

private:
    bool revealed_{false};
};

} // namespace basilisk::game::debug
