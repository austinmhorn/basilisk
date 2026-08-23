#pragma once

#include <cstddef>

namespace basilisk::game {

enum class PauseMenuResult {
    None,
    Resume,
    QuitGame,
};

class PauseMenuState {
public:
    void open() noexcept;
    void close() noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool blocksGameplayInput() const noexcept { return active_; }

    void moveSelection(int direction) noexcept;
    void select(std::size_t index) noexcept;
    [[nodiscard]] std::size_t selectedIndex() const noexcept {
        return selectedIndex_;
    }
    [[nodiscard]] PauseMenuResult activateSelected() const noexcept;

private:
    bool active_{false};
    std::size_t selectedIndex_{0};
};

} // namespace basilisk::game
