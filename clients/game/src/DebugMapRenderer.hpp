#pragma once

#ifndef BASILISK_GAME_DEBUG_BUILD
#error "DebugMapRenderer is available only to BasiliskGameDebug"
#endif

#include <SDL3/SDL.h>

#include <string>

#include "DebugMapProvider.hpp"
#include "MapPresentation.hpp"
#include "TextRenderer.hpp"
#include "basilisk/ClientSnapshot.hpp"

namespace basilisk::game::debug {

[[nodiscard]] bool renderRevealedPhysicalMap(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    const DebugMapTruth& truth,
    const PlayerRoundSnapshot& snapshot,
    const MapPresentationGeometry& geometry,
    std::string& error);

} // namespace basilisk::game::debug
