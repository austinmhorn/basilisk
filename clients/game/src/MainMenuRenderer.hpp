#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "MainMenu.hpp"
#include "MapPresentation.hpp"
#include "NetworkProtocol.hpp"
#include "SvgTextureManager.hpp"
#include "TextRenderer.hpp"

namespace basilisk::game {

struct MainMenuButtonGeometry {
    MainMenuAction action{};
    PresentationRect bounds;
};

struct MainMenuGeometry {
    std::vector<MainMenuButtonGeometry> buttons;
};

[[nodiscard]] std::optional<std::size_t> hitTestMainMenu(
    const MainMenuGeometry& geometry,
    PresentationPoint point) noexcept;

[[nodiscard]] bool renderMainMenu(
    SDL_Renderer* renderer,
    TextRenderer& text,
    SvgTextureManager& svgTextures,
    const MainMenuState& menu,
    std::optional<std::int64_t> trophyTotal,
    const std::optional<PublicAccountProfile>& authenticatedProfile,
    const std::optional<network::LeaderboardPageResponse>& leaderboard,
    MainMenuGeometry& geometry,
    int outputWidth,
    int outputHeight,
    std::string& error);

} // namespace basilisk::game
