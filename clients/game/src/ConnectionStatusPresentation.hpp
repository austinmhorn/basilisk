#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <string_view>

#include "TextRenderer.hpp"
#include "WebSocketNetworkSession.hpp"

namespace basilisk::game {

[[nodiscard]] std::string_view connectionStatusText(
    NetworkConnectionState state) noexcept;

// Draws a centered connection state before bootstrap and a compact status pill
// once player-safe session data is available.
[[nodiscard]] bool renderConnectionStatus(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    NetworkConnectionState state,
    std::string_view detail,
    bool sessionReady,
    int outputWidth,
    int outputHeight,
    std::string& error);

} // namespace basilisk::game
