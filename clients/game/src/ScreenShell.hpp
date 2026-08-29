#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ActionSelection.hpp"
#include "ClientLifecycle.hpp"
#include "ClientSessionController.hpp"
#include "MapActionMenu.hpp"
#include "MapLayout.hpp"
#include "MapPresentation.hpp"
#include "SandboxPresentation.hpp"
#include "SvgTextureManager.hpp"
#include "TextRenderer.hpp"
#include "basilisk/ClientSnapshot.hpp"

#if defined(BASILISK_GAME_DEBUG)
#include "DebugMapProvider.hpp"
#endif

namespace basilisk::game {

struct HudArrowSectionLayout {
    int slotCount{};
    float slotWidth{10.0F};
    float slotSpacing{3.0F};
    float contentWidth{};
    float sectionWidth{};
};

[[nodiscard]] inline HudArrowSectionLayout hudArrowSectionLayout(
    int maxArrows) noexcept {
    HudArrowSectionLayout result;
    result.slotCount = std::max(0, maxArrows);
    if (result.slotCount > 0) {
        result.contentWidth = static_cast<float>(result.slotCount) *
            result.slotWidth + static_cast<float>(result.slotCount - 1) *
            result.slotSpacing;
    }
    // Five slots occupy 62 logical pixels in the established HUD. Keeping that
    // as the label-safe minimum preserves the normal layout while larger
    // quivers expand PACK by their actual slot footprint.
    constexpr float minimumContentWidth = 62.0F;
    constexpr float sectionPadding = 16.0F;
    result.sectionWidth = std::max(minimumContentWidth, result.contentWidth) +
        sectionPadding;
    return result;
}

struct ActionRowGeometry {
    std::size_t actionIndex{};
    PresentationRect bounds;
};

struct ActionPanelGeometry {
    PresentationRect panel;
    PresentationRect viewport;
    PresentationRect lockButton;
    std::vector<ActionRowGeometry> rows;
    std::size_t visibleCapacity{};
};

struct InventoryItemGeometry {
    ItemType item{ItemType::HealingDraught};
    PresentationRect bounds;
};

struct InventoryPanelGeometry {
    PresentationRect panel;
    std::vector<InventoryItemGeometry> items;
};

struct MapActionMenuGeometry {
    struct Row {
        MapActionMenuChoice choice;
        PresentationRect bounds;
    };

    PresentationRect panel;
    std::vector<Row> rows;
};

struct LifecycleModalGeometry {
    PresentationRect panel;
    std::optional<PresentationRect> watchButton;
    PresentationRect quitButton;
    bool blocking{false};
};


[[nodiscard]] std::optional<std::size_t> hitTestActionRow(
    const ActionPanelGeometry& geometry,
    PresentationPoint point) noexcept;
[[nodiscard]] bool hitTestActionLockButton(
    const ActionPanelGeometry& geometry,
    PresentationPoint point) noexcept;
[[nodiscard]] bool hitTestActionPanel(
    const ActionPanelGeometry& geometry,
    PresentationPoint point) noexcept;
[[nodiscard]] std::optional<ItemType> hitTestInventoryItem(
    const InventoryPanelGeometry& geometry,
    PresentationPoint point) noexcept;
[[nodiscard]] std::optional<MapActionMenuChoice> hitTestMapActionRow(
    const MapActionMenuGeometry& geometry,
    PresentationPoint point) noexcept;
[[nodiscard]] bool hitTestMapActionMenu(
    const MapActionMenuGeometry& geometry,
    PresentationPoint point) noexcept;
[[nodiscard]] bool hitTestLifecycleWatch(
    const LifecycleModalGeometry& geometry,
    PresentationPoint point) noexcept;
[[nodiscard]] bool hitTestLifecycleQuit(
    const LifecycleModalGeometry& geometry,
    PresentationPoint point) noexcept;

[[nodiscard]] bool renderScreenShell(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    SvgTextureManager& svgTextures,
    const ClientSessionController& session,
    PlayerMapLayout& mapLayout,
    MapPresentationState& mapPresentation,
    MapPresentationGeometry& mapGeometry,
    const ActionSelectionState& actionSelection,
    std::optional<std::size_t> hoveredActionIndex,
    ActionPanelGeometry& actionGeometry,
    InventoryPanelGeometry& inventoryGeometry,
    const MapActionMenuState& mapActionMenu,
    MapActionMenuGeometry& mapActionMenuGeometry,
    LifecycleModalGeometry& lifecycleModalGeometry,
    std::optional<TrophyAwardPresentation> trophyAward,
#if defined(BASILISK_GAME_DEBUG)
    const debug::DebugMapTruth* debugMapTruth,
    const debug::DebugGameplayTruth* debugGameplayTruth,
    bool revealDebugMap,
    bool revealDebugGameplay,
    bool debugInventoryMenuOpen,
    bool debugKillMenuOpen,
    bool debugKillAvailable,
#endif
    int outputWidth,
    int outputHeight,
    std::string& error);

} // namespace basilisk::game
