#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace basilisk::game {

enum class MainMenuPage {
    Main,
    StartGame,
    Leaderboards,
    Settings,
};

enum class MainMenuAction {
    StartGame,
    Leaderboards,
    Settings,
    Exit,
    FindGame,
    HostGame,
    JoinGame,
    Back,
    PreviousPage,
    NextPage,
};

enum class MainMenuResult {
    None,
    Exit,
    RequestLeaderboard,
};

class MainMenuState {
public:
    static constexpr std::uint32_t leaderboardPageSize{10};

    [[nodiscard]] MainMenuPage page() const noexcept;
    [[nodiscard]] std::span<const MainMenuAction> actions() const noexcept;
    [[nodiscard]] std::size_t selectedIndex() const noexcept;
    [[nodiscard]] MainMenuAction selectedAction() const noexcept;
    [[nodiscard]] std::uint32_t leaderboardOffset() const noexcept;

    void select(std::size_t index) noexcept;
    void moveSelection(int delta) noexcept;
    [[nodiscard]] MainMenuResult activateSelected() noexcept;
    [[nodiscard]] MainMenuResult activate(MainMenuAction action) noexcept;
    [[nodiscard]] MainMenuResult back() noexcept;

private:
    void setPage(MainMenuPage page) noexcept;

    MainMenuPage page_{MainMenuPage::Main};
    std::size_t selectedIndex_{0};
    std::uint32_t leaderboardOffset_{0};
};

} // namespace basilisk::game
