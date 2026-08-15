#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ActionSelection.hpp"
#include "ClientLifecycle.hpp"
#include "MapActionMenu.hpp"
#include "MapLayout.hpp"
#include "MapPresentation.hpp"
#include "SvgTextureManager.hpp"
#include "TextRenderer.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/PublicMatchMetadata.hpp"
#include "basilisk/client/ClientViewContext.hpp"
#include "basilisk/client/PlayerProfile.hpp"

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

// Public/demo-safe screen inputs that are intentionally outside gameplay
// snapshot state: match facts, profiles/cosmetics, and presentation-only rows.
struct ScreenShellData {
    PublicMatchMetadata matchMetadata;
    std::array<client::PublicPlayerProfile, 2> profiles;
    client::ClientViewContext viewContext;
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
    const PlayerRoundSnapshot& snapshot,
    PlayerMapLayout& mapLayout,
    MapPresentationState& mapPresentation,
    MapPresentationGeometry& mapGeometry,
    const ActionSelectionState& actionSelection,
    ActionPanelGeometry& actionGeometry,
    const MapActionMenuState& mapActionMenu,
    MapActionMenuGeometry& mapActionMenuGeometry,
    LifecycleModalGeometry& lifecycleModalGeometry,
    const ScreenShellData& data,
    int outputWidth,
    int outputHeight,
    std::string& error);

} // namespace basilisk::game
