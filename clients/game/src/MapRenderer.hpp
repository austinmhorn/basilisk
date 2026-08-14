#pragma once

#include <SDL3/SDL.h>

#include "MapLayout.hpp"

namespace basilisk::game {

struct MapViewport {
    SDL_FRect bounds{};
    LogicalPoint logicalCenter{};
    float scale{1.0F};
};

void renderPlayerKnownMap(
    SDL_Renderer* renderer,
    const PlayerMapView& map,
    const PlayerMapLayout& layout,
    CaveId currentCave,
    const MapViewport& viewport);

} // namespace basilisk::game
