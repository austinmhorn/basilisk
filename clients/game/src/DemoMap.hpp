#pragma once

#include "basilisk/ClientSnapshot.hpp"

namespace basilisk::game::demo {

enum class DemoSnapshotStage {
    NormalStart,
    RecoverableSigil,
    SecuredSigilHiddenExtraction,
    SecuredSigilVisibleExtraction,
    NextRound
};

// Development-only player-safe data for manually exercising map rendering.
[[nodiscard]] PlayerRoundSnapshot makeDemoMapSnapshot(
    DemoSnapshotStage stage = DemoSnapshotStage::NormalStart);
[[nodiscard]] PlayerRoundSnapshot makeDemoDefeatedSnapshot(bool matchCompleted);
[[nodiscard]] PlayerRoundSnapshot makeDemoSurvivorSnapshot(bool matchCompleted);

} // namespace basilisk::game::demo
