#pragma once

#include <optional>

#include "basilisk/Types.hpp"
#include "basilisk/actors/Basilisk.hpp"
#include "basilisk/items/Item.hpp"

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
    BodyCreated,
    BodyFound,
    SigilAcquired,
    ExtractionActivated,
    EscapeAvailable,
    PlayerEscaped,
    MatchDrawn,
    PitTriggered,
    JackalMoved,
    JackalStunned,
    JackalRobbedArrow,
    JackalScaredPlayer,
    JackalKnockedOutPlayer,
    SearchCompleted,
    CaveAlreadySearched,
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
};

} // namespace basilisk
