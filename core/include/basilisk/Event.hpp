#pragma once

#include <optional>

#include "basilisk/Types.hpp"

namespace basilisk {

enum class GameEventType {
    PlayerMoved,
    ArrowFired,
    ArrowHitPlayer,
    ArrowMissed,
    PlayerDamaged,
    PlayerKilled,
    SearchCompleted
};

struct GameEvent {
    GameEventType type{};
    std::optional<PlayerId> actor;
    std::optional<PlayerId> targetPlayer;
    std::optional<CaveId> cave;
    int amount{0};
};

} // namespace basilisk
