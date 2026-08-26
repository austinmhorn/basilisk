#pragma once

#ifndef BASILISK_GAME_DEBUG_BUILD
#error "DebugInventoryMenu is available only to BasiliskGameDebug"
#endif

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>

#include "TextRenderer.hpp"
#include "basilisk/items/Item.hpp"

namespace basilisk::game::debug {

class DebugInventoryMenuState {
public:
    void toggle() noexcept;
    void close() noexcept;
    void moveSelection(int direction) noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::size_t selectedIndex() const noexcept;
    [[nodiscard]] ItemType selectedItem() const noexcept;

private:
    bool active_{false};
    std::size_t selected_{0};
};

[[nodiscard]] bool renderDebugInventoryMenu(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    const DebugInventoryMenuState& menu,
    int outputWidth,
    int outputHeight,
    std::string& error);

} // namespace basilisk::game::debug
