#include <cassert>

#include "MainMenu.hpp"
#include "AuthScreen.hpp"

using namespace basilisk::game;

int main() {
    AuthScreenState auth;
    assert(auth.mode() == AuthMode::SignIn);
    auth.append("hunter@example.test");
    auth.nextField();
    auth.append("secret");
    network::AuthenticationRequest authRequest;
    assert(auth.request(authRequest));
    assert(std::holds_alternative<network::LoginRequest>(authRequest.payload));
    auth.setWaiting(false);
    auth.switchMode();
    assert(auth.mode() == AuthMode::CreateAccount);

    MainMenuState menu;
    assert(menu.page() == MainMenuPage::Main);
    assert(menu.selectedAction() == MainMenuAction::StartGame);

    assert(menu.activateSelected() == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::StartGame);
    assert(menu.actions().size() == 4);
    assert(menu.activate(MainMenuAction::FindGame) ==
           MainMenuResult::RequestFindMatch);
    assert(menu.page() == MainMenuPage::FindMatch);
    assert(menu.activate(MainMenuAction::CancelFindMatch) ==
           MainMenuResult::RequestCancelFindMatch);
    menu.matchmakingCancelled();
    assert(menu.page() == MainMenuPage::StartGame);
    assert(menu.activate(MainMenuAction::HostGame) ==
           MainMenuResult::RequestHostLobby);
    assert(menu.page() == MainMenuPage::HostLobby);
    menu.lobbyHosted("CAVE7X");
    assert(menu.lobbyCode() == "CAVE7X" && menu.lobbyWaiting());
    assert(menu.activate(MainMenuAction::CancelLobby) ==
           MainMenuResult::RequestCancelLobby);
    menu.lobbyCancelled();
    assert(menu.page() == MainMenuPage::StartGame);
    assert(menu.activate(MainMenuAction::JoinGame) == MainMenuResult::None);
    menu.appendLobbyCode("hunt34");
    assert(menu.lobbyCode() == "HUNT34");
    assert(menu.activate(MainMenuAction::SubmitLobbyCode) ==
           MainMenuResult::RequestJoinLobby);
    menu.lobbyAssigned("HUNT34");
    assert(menu.page() == MainMenuPage::MatchReady);
    assert(menu.back() == MainMenuResult::None);
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
    assert(menu.activate(MainMenuAction::Settings) == MainMenuResult::None);
    assert(menu.selectedAction() == MainMenuAction::Logout);
    assert(menu.activateSelected() == MainMenuResult::Logout);

    (void)menu.back();
    assert(menu.activate(MainMenuAction::EditProfile) == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::Cosmetics);
    assert(menu.selectedAction() == MainMenuAction::Back);
    assert(menu.activateSelected() == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::Main);

    menu.moveSelection(-1);
    assert(menu.selectedAction() == MainMenuAction::EditProfile);
    menu.moveSelection(-1);
    assert(menu.selectedAction() == MainMenuAction::Exit);
    assert(menu.activateSelected() == MainMenuResult::Exit);
}
