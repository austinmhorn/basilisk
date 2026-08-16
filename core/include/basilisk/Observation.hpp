#pragma once

#include <optional>

#include "basilisk/Types.hpp"
#include "basilisk/actors/Basilisk.hpp"
#include "basilisk/items/Item.hpp"
#include "basilisk/world/DiscoveryState.hpp"

namespace basilisk {

enum class ObservationType {
    RivalNearby,
    PitNearby,
    PitInvestigationSucceeded,
    PitInvestigationInconclusive,
    JackalNearby,
    BasiliskNearby,
    BasiliskNearbySubtle,
    RestlessBasiliskNoise,
    EnragedLastKnownCave,

    ArrowMissed,
    YouHitRival,
    ArrowHitYou,
    YouWereDamaged,
    YouKilledRival,
    YouDied,
    BasiliskFoundYou,
    FellIntoPit,
    JackalRobbedYou,
    JackalScaredYou,
    JackalKnockedOutYou,
    JackalRepelled,
    JackalStunned,
    RivalDied,
    RivalDisconnected,
    RivalReconnected,
    RivalReserveExpired,
    RivalDisconnectTimedOut,
    CaveAlreadySearched,
    SearchEmpty,
    ItemFound,
    InventoryFull,
    ItemUsed,
    PlayerHealed,
    ArrowFound,
    ExoticCallingCardFound,
    OldHuntersMapFound,
    OldHuntersMapDistance,
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
    std::optional<TunnelId> tunnel;
};

} // namespace basilisk
