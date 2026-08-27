#pragma once

#ifndef BASILISK_GAME_DEBUG_BUILD
#error "DebugKillMenu is available only to BasiliskGameDebug"
#endif

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <vector>

#include "TextRenderer.hpp"
#include "DebugMapProvider.hpp"

namespace basilisk::game::debug {

class DebugKillMenuState {
public:
    void toggle() noexcept;
    void toggle(std::vector<DebugParticipant> participants);
    void close() noexcept;
    void moveSelection(int direction) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::size_t selectedIndex() const noexcept;
    [[nodiscard]] DebugKillTarget selectedTarget() const noexcept;
    [[nodiscard]] PlayerId selectedPlayer() const noexcept;
    [[nodiscard]] const std::vector<DebugParticipant>& participants() const noexcept;

private:
    bool active_{false};
    std::size_t selected_{0};
    std::vector<DebugParticipant> participants_;
};

[[nodiscard]] bool renderDebugKillMenu(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    const DebugKillMenuState& menu,
    int outputWidth,
    int outputHeight,
    std::string& error);

} // namespace basilisk::game::debug
