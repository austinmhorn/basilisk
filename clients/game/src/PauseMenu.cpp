#include "PauseMenu.hpp"

namespace basilisk::game {

void PauseMenuState::open() noexcept {
    active_ = true;
    selectedIndex_ = 0;
}

void PauseMenuState::close() noexcept {
    active_ = false;
    selectedIndex_ = 0;
}

void PauseMenuState::moveSelection(int direction) noexcept {
    if (!active_ || direction == 0) return;
    selectedIndex_ = direction > 0
        ? (selectedIndex_ + 1) % 2
        : (selectedIndex_ + 1) % 2;
}

void PauseMenuState::select(std::size_t index) noexcept {
    if (active_ && index < 2) selectedIndex_ = index;
}

PauseMenuResult PauseMenuState::activateSelected() const noexcept {
    if (!active_) return PauseMenuResult::None;
    return selectedIndex_ == 0
        ? PauseMenuResult::Resume
        : PauseMenuResult::QuitGame;
}

} // namespace basilisk::game
