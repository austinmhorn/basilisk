#pragma once

#include <SDL3/SDL.h>

#include <string>

#include "MapLayout.hpp"
#include "MapPresentation.hpp"
#include "TextRenderer.hpp"
#include "basilisk/ClientSnapshot.hpp"

namespace basilisk::game {

[[nodiscard]] bool renderPlayerKnownMap(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    const PlayerRoundSnapshot& snapshot,
    const PlayerMapLayout& layout,
    const MapPresentationGeometry& geometry,
    const MapPresentationState& presentation,
    std::string& error);

} // namespace basilisk::game
