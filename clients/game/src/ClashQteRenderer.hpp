#pragma once
#include <SDL3/SDL.h>
#include <string>
#include "NetworkProtocol.hpp"
#include "TextRenderer.hpp"
namespace basilisk::game {
[[nodiscard]] bool renderClashQte(SDL_Renderer*, TextRenderer&,
    const network::ClashStarted&, std::string_view input, int width, int height,
    std::string& error);
}
