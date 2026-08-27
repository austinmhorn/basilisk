#pragma once

#ifndef BASILISK_GAME_DEBUG_BUILD
#error "DebugMapProvider is available only to BasiliskGameDebug"
#endif

#include <compare>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "MapLayout.hpp"
#include "basilisk/Types.hpp"
#include "basilisk/actors/Basilisk.hpp"
#include "basilisk/items/Item.hpp"

namespace basilisk { struct MatchState; }

namespace basilisk::game::debug {

enum class DebugKillTarget { Host, Ai };

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
    bool basiliskAlive{true};
    BasiliskBehavior basiliskBehavior{BasiliskBehavior::Normal};
    std::optional<CaveId> basiliskLastCave;
    int basiliskEncounterCount{0};
    int basiliskRoundsSinceMove{0};
    std::vector<CaveId> pitCaves;
    std::vector<CaveId> jackalCaves;
    std::optional<CaveId> territorialSearchTarget;
    struct Hunter {
        PlayerId player{};
        CaveId cave{};
        std::string label;
        int health{0};
        int arrows{0};
    };
    std::vector<Hunter> hunters;
    enum class SigilState { OnMap, Carried };
    struct Sigil {
        PlayerId owner{};
        CaveId cave{};
        SigilState state{SigilState::OnMap};
        std::optional<PlayerId> carrier;
    };
    std::vector<Sigil> sigils;
    std::vector<std::string> aiDecisionTrace;
};

struct DebugHunterLabel {
    PlayerId player{};
    std::string label;
};

[[nodiscard]] DebugMapTruth buildDebugMapTruth(
    const MatchState& state, const PlayerMapLayout& layout);
[[nodiscard]] DebugGameplayTruth buildDebugGameplayTruth(
    const MatchState& state,
    std::span<const DebugHunterLabel> hunters = {});

class DebugMapProvider {
public:
    using GameplayTruthSource = std::function<DebugGameplayTruth()>;
    using BehaviorControlSource = std::function<bool(BasiliskBehavior)>;
    using ItemGrantSource = std::function<bool(ItemType)>;
    using KillPlayerSource = std::function<bool(DebugKillTarget)>;

    DebugMapProvider(
        DebugMapTruth mapTruth,
        GameplayTruthSource gameplayTruthSource,
        BehaviorControlSource behaviorControlSource,
        ItemGrantSource itemGrantSource = {},
        KillPlayerSource killPlayerSource = {});

    [[nodiscard]] const DebugMapTruth& mapTruth() const noexcept;
    [[nodiscard]] DebugGameplayTruth gameplayTruth() const;
    [[nodiscard]] bool cycleBasiliskBehavior();
    [[nodiscard]] bool grantItem(ItemType item);
    [[nodiscard]] bool killPlayer(DebugKillTarget target);
    [[nodiscard]] bool killControlAvailable() const noexcept;

private:
    DebugMapTruth mapTruth_;
    GameplayTruthSource gameplayTruthSource_;
    BehaviorControlSource behaviorControlSource_;
    ItemGrantSource itemGrantSource_;
    KillPlayerSource killPlayerSource_;
};

class DebugMapRevealState {
public:
    void toggle() noexcept;
    [[nodiscard]] bool revealed() const noexcept;

private:
    bool revealed_{false};
};

} // namespace basilisk::game::debug
