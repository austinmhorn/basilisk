#pragma once

#include <optional>

#include "basilisk/Types.hpp"
#include "basilisk/actors/Basilisk.hpp"
#include "basilisk/items/Item.hpp"

namespace basilisk {

enum class ObservationType {
    RivalNearby,
    PitNearby,
    JackalNearby,
    BasiliskNearby,
    BasiliskNearbySubtle,
    RestlessBasiliskNoise,
    EnragedLastKnownCave,

    ArrowHitYou,
    YouWereDamaged,
    YouKilledRival,
    YouDied,
    FellIntoPit,
    RivalDied,
    ItemFound,
    ArrowFound,
    ExoticCallingCardFound,
    SigilAcquired,
    ExtractionRevealed,
    EscapeAvailable,
    BasiliskEvaded,
    BasiliskBehaviorChanged,
    BasiliskKilled,
    MatchDrawn
};

struct PlayerObservation {
    ObservationType type{};
    PlayerId viewer{};
    std::optional<CaveId> cave;
    std::optional<PlayerId> otherPlayer;
    int amount{0};
    std::optional<BasiliskBehavior> basiliskBehavior;
    std::optional<ItemType> itemType;
};

} // namespace basilisk
