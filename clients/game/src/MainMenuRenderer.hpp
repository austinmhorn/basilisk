#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
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

[[nodiscard]] PresentationRect mainMenuFooterButtonBounds(
    PresentationRect shell,
    double scale,
    double width,
    bool rightAligned) noexcept;

[[nodiscard]] std::string sandboxLobbySlotTitle(
    std::size_t slot,
    client::SandboxLobbySlotKind kind,
    bool occupied,
    std::string_view publicName = {});

[[nodiscard]] std::optional<MainMenuAction> hitTestMainMenu(
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
