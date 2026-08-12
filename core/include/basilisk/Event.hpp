#pragma once

#include <optional>

#include "basilisk/Types.hpp"
#include "basilisk/actors/Basilisk.hpp"

namespace basilisk {

enum class GameEventType {
    PlayerMoved,
    ArrowFired,
    ArrowHitPlayer,
    ArrowReachedBasilisk,
    ArrowHitJackal,
    ArrowMissed,
    PlayerDamaged,
    PlayerKilled,
    JackalStunned,
    SearchCompleted,
    BasiliskEvaded,
    BasiliskBehaviorChanged,
    BasiliskMoved,
    BasiliskKilled
};

struct GameEvent {
    GameEventType type{};
    std::optional<PlayerId> actor;
    std::optional<PlayerId> targetPlayer;
    std::optional<CaveId> cave;
    int amount{0};
    std::optional<BasiliskBehavior> basiliskBehavior;
};

} // namespace basilisk
