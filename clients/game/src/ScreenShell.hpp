#pragma once

#include <SDL3/SDL.h>

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
#include "SvgTextureManager.hpp"
#include "TextRenderer.hpp"
#include "basilisk/ClientSnapshot.hpp"

#if defined(BASILISK_GAME_DEBUG)
#include "DebugMapProvider.hpp"
#endif

namespace basilisk::game {

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
#if defined(BASILISK_GAME_DEBUG)
    const debug::DebugMapTruth* debugMapTruth,
    const debug::DebugGameplayTruth* debugGameplayTruth,
    bool revealDebugMap,
    bool revealDebugGameplay,
    bool debugInventoryMenuOpen,
#endif
    int outputWidth,
    int outputHeight,
    std::string& error);

} // namespace basilisk::game
