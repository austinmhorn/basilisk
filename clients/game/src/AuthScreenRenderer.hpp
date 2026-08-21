#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "AuthScreen.hpp"
#include "MapPresentation.hpp"
#include "TextRenderer.hpp"

namespace basilisk::game {

struct AuthFieldGeometry { AuthField field{}; PresentationRect bounds; };
struct AuthScreenGeometry {
    std::vector<AuthFieldGeometry> fields;
    PresentationRect submit;
    PresentationRect switchMode;
};

[[nodiscard]] bool renderAuthScreen(
    SDL_Renderer* renderer, TextRenderer& text, const AuthScreenState& state,
    AuthScreenGeometry& geometry, int outputWidth, int outputHeight,
    std::string& error);
[[nodiscard]] bool hitTest(PresentationRect bounds, PresentationPoint point) noexcept;

} // namespace basilisk::game
