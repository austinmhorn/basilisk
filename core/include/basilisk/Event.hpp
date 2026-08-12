#pragma once

#include <optional>

#include "basilisk/Types.hpp"
#include "basilisk/actors/Basilisk.hpp"
#include "basilisk/items/Item.hpp"
#include "basilisk/world/DiscoveryState.hpp"

namespace basilisk {

enum class GameEventType {
    PlayerMoved,
    CaveDiscovered,
    TunnelDestinationRevealed,
    ArrowFired,
    ArrowHitPlayer,
    ArrowReachedBasilisk,
    ArrowHitJackal,
    ArrowMissed,
    PlayerDamaged,
    PlayerKilled,
    PlayerDisconnected,
    PlayerReconnected,
    PlayerReserveExpired,
    PlayerDisconnectTimedOut,
    BodyCreated,
    BodyFound,
    SigilEjected,
    SigilAcquired,
    ExtractionActivated,
    EscapeAvailable,
    PlayerEscaped,
    MatchDrawn,
    PitTriggered,
    PitInvestigationSucceeded,
    PitInvestigationInconclusive,
    JackalMoved,
    JackalStunned,
    JackalRepelled,
    JackalRobbedArrow,
    JackalScaredPlayer,
    JackalKnockedOutPlayer,
    SearchCompleted,
    CaveAlreadySearched,
    LooseArrowSpawned,
    ArrowFound,
    ItemFound,
    InventoryFull,
    ExoticCallingCardFound,
    ItemUsed,
    PlayerHealed,
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
    std::optional<ItemType> itemType;
    std::optional<TunnelId> tunnel;
};

} // namespace basilisk
