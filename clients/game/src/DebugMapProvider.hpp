#pragma once

#ifndef BASILISK_GAME_DEBUG_BUILD
#error "DebugMapProvider is available only to BasiliskGameDebug"
#endif

#include <compare>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "MapLayout.hpp"
#include "basilisk/Types.hpp"
#include "basilisk/actors/Basilisk.hpp"

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

struct DebugGameplayTruth {
    CaveId basiliskCave{};
    BasiliskBehavior basiliskBehavior{BasiliskBehavior::Normal};
    std::optional<CaveId> basiliskLastCave;
    int basiliskEncounterCount{0};
    std::vector<CaveId> pitCaves;
    std::vector<CaveId> jackalCaves;
    std::optional<CaveId> territorialSearchTarget;
};

class DebugMapProvider {
public:
    using GameplayTruthSource = std::function<DebugGameplayTruth()>;

    DebugMapProvider(
        DebugMapTruth mapTruth,
        GameplayTruthSource gameplayTruthSource);

    [[nodiscard]] const DebugMapTruth& mapTruth() const noexcept;
    [[nodiscard]] DebugGameplayTruth gameplayTruth() const;

private:
    DebugMapTruth mapTruth_;
    GameplayTruthSource gameplayTruthSource_;
};

class DebugMapRevealState {
public:
    void toggle() noexcept;
    [[nodiscard]] bool revealed() const noexcept;

private:
    bool revealed_{false};
};

} // namespace basilisk::game::debug
