#include <cassert>

#include "MainMenu.hpp"

using namespace basilisk::game;

int main() {
    MainMenuState menu;
    assert(menu.page() == MainMenuPage::Main);
    assert(menu.selectedAction() == MainMenuAction::StartGame);

    assert(menu.activateSelected() == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::StartGame);
    assert(menu.actions().size() == 4);
    assert(menu.activate(MainMenuAction::FindGame) == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::StartGame);
    assert(menu.back() == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::Main);

    assert(menu.activate(MainMenuAction::Leaderboards) ==
           MainMenuResult::RequestLeaderboard);
    assert(menu.page() == MainMenuPage::Leaderboards);
    assert(menu.leaderboardOffset() == 0);
    assert(menu.activate(MainMenuAction::NextPage) ==
           MainMenuResult::RequestLeaderboard);
    assert(menu.leaderboardOffset() == MainMenuState::leaderboardPageSize);
    assert(menu.activate(MainMenuAction::PreviousPage) ==
           MainMenuResult::RequestLeaderboard);
    assert(menu.leaderboardOffset() == 0);
    assert(menu.activate(MainMenuAction::PreviousPage) == MainMenuResult::None);

    (void)menu.back();
    menu.moveSelection(-1);
    assert(menu.selectedAction() == MainMenuAction::Exit);
    assert(menu.activateSelected() == MainMenuResult::Exit);
}
