#include "MainMenu.hpp"

#include <algorithm>
#include <array>

namespace basilisk::game {
namespace {

constexpr std::array mainActions{
    MainMenuAction::StartGame,
    MainMenuAction::Leaderboards,
    MainMenuAction::Settings,
    MainMenuAction::Exit,
};
constexpr std::array startActions{
    MainMenuAction::FindGame,
    MainMenuAction::HostGame,
    MainMenuAction::JoinGame,
    MainMenuAction::Back,
};
constexpr std::array leaderboardActions{
    MainMenuAction::PreviousPage,
    MainMenuAction::NextPage,
    MainMenuAction::Back,
};
constexpr std::array settingsActions{MainMenuAction::Logout, MainMenuAction::Back};

} // namespace

MainMenuPage MainMenuState::page() const noexcept { return page_; }

std::span<const MainMenuAction> MainMenuState::actions() const noexcept {
    switch (page_) {
        case MainMenuPage::Main: return mainActions;
        case MainMenuPage::StartGame: return startActions;
        case MainMenuPage::Leaderboards: return leaderboardActions;
        case MainMenuPage::Settings: return settingsActions;
    }
    return mainActions;
}

std::size_t MainMenuState::selectedIndex() const noexcept {
    return selectedIndex_;
}

MainMenuAction MainMenuState::selectedAction() const noexcept {
    return actions()[selectedIndex_];
}

std::uint32_t MainMenuState::leaderboardOffset() const noexcept {
    return leaderboardOffset_;
}

void MainMenuState::select(std::size_t index) noexcept {
    if (index < actions().size()) selectedIndex_ = index;
}

void MainMenuState::moveSelection(int delta) noexcept {
    const std::size_t count = actions().size();
    if (count == 0) return;
    const int current = static_cast<int>(selectedIndex_);
    const int wrapped = (current + delta % static_cast<int>(count) +
        static_cast<int>(count)) % static_cast<int>(count);
    selectedIndex_ = static_cast<std::size_t>(wrapped);
}

MainMenuResult MainMenuState::activateSelected() noexcept {
    return activate(selectedAction());
}

MainMenuResult MainMenuState::activate(MainMenuAction action) noexcept {
    switch (action) {
        case MainMenuAction::StartGame:
            setPage(MainMenuPage::StartGame);
            break;
        case MainMenuAction::Leaderboards:
            leaderboardOffset_ = 0;
            setPage(MainMenuPage::Leaderboards);
            return MainMenuResult::RequestLeaderboard;
        case MainMenuAction::Settings:
            setPage(MainMenuPage::Settings);
            break;
        case MainMenuAction::Exit:
            return MainMenuResult::Exit;
        case MainMenuAction::Back:
            setPage(MainMenuPage::Main);
            break;
        case MainMenuAction::PreviousPage:
            if (leaderboardOffset_ >= leaderboardPageSize) {
                leaderboardOffset_ -= leaderboardPageSize;
                return MainMenuResult::RequestLeaderboard;
            }
            break;
        case MainMenuAction::NextPage:
            leaderboardOffset_ += leaderboardPageSize;
            return MainMenuResult::RequestLeaderboard;
        case MainMenuAction::Logout:
            return MainMenuResult::Logout;
        case MainMenuAction::FindGame:
        case MainMenuAction::HostGame:
        case MainMenuAction::JoinGame:
            break;
    }
    return MainMenuResult::None;
}

MainMenuResult MainMenuState::back() noexcept {
    if (page_ != MainMenuPage::Main) setPage(MainMenuPage::Main);
    return MainMenuResult::None;
}

void MainMenuState::setPage(MainMenuPage page) noexcept {
    page_ = page;
    selectedIndex_ = 0;
}

} // namespace basilisk::game
