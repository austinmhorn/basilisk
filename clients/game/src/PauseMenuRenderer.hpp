#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>

#include "MapPresentation.hpp"
#include "PauseMenu.hpp"
#include "TextRenderer.hpp"

namespace basilisk::game {

struct PauseMenuGeometry {
    PresentationRect resumeButton;
    PresentationRect quitButton;
};

[[nodiscard]] std::optional<std::size_t> hitTestPauseMenu(
    const PauseMenuGeometry& geometry,
    PresentationPoint point) noexcept;

[[nodiscard]] bool renderPauseMenu(
    SDL_Renderer* renderer,
    TextRenderer& text,
    const PauseMenuState& pause,
    PauseMenuGeometry& geometry,
    int outputWidth,
    int outputHeight,
    std::string& error);

} // namespace basilisk::game
