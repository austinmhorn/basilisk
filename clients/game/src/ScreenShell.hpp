#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "MapLayout.hpp"
#include "MapPresentation.hpp"
#include "SvgTextureManager.hpp"
#include "TextRenderer.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/PublicMatchMetadata.hpp"
#include "basilisk/client/PlayerProfile.hpp"

namespace basilisk::game {

struct ActionRowView {
    std::string key;
    std::string label;
    std::string detail;
};

// Public/demo-safe screen inputs that are intentionally outside gameplay
// snapshot state: match facts, profiles/cosmetics, and presentation-only rows.
struct ScreenShellData {
    PublicMatchMetadata matchMetadata;
    std::array<client::PublicPlayerProfile, 2> profiles;
    PlayerId localPlayer{};
    std::vector<ActionRowView> actionRows;
};

[[nodiscard]] bool renderScreenShell(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    SvgTextureManager& svgTextures,
    const PlayerRoundSnapshot& snapshot,
    PlayerMapLayout& mapLayout,
    MapPresentationState& mapPresentation,
    MapPresentationGeometry& mapGeometry,
    const ScreenShellData& data,
    int outputWidth,
    int outputHeight,
    std::string& error);

} // namespace basilisk::game
