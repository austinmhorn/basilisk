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
    struct CallingCardTile {
        client::CallingCardId callingCard;
        PresentationRect bounds;
    };
    std::vector<CallingCardTile> callingCards;
    struct EmblemTile {
        client::EmblemId emblem;
        PresentationRect bounds;
    };
    std::vector<EmblemTile> emblems;
};

[[nodiscard]] std::optional<std::size_t> hitTestMainMenu(
    const MainMenuGeometry& geometry,
    PresentationPoint point) noexcept;

[[nodiscard]] std::optional<client::CallingCardId> hitTestCallingCardGallery(
    const MainMenuGeometry& geometry,
    PresentationPoint point);

[[nodiscard]] std::optional<client::EmblemId> hitTestEmblemGallery(
    const MainMenuGeometry& geometry,
    PresentationPoint point);

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
