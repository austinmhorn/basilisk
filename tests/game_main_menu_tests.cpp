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
    const auto* login = std::get_if<network::LoginRequest>(&authRequest.payload);
    assert(login != nullptr && login->email == "hunter@example.test");
    auth.setWaiting(false);
    auth.switchMode();
    assert(auth.mode() == AuthMode::CreateAccount);
    auth.nextField();
    auth.nextField();
    auth.append("cave-hunter");
    assert(auth.request(authRequest));
    const auto* create =
        std::get_if<network::CreateAccountRequest>(&authRequest.payload);
    assert(create != nullptr && create->email == "hunter@example.test");
    assert(create->username == "cave-hunter");

    MainMenuState menu;
    assert(menu.page() == MainMenuPage::Main);
    assert(menu.selectedAction() == MainMenuAction::StartGame);

    assert(menu.activateSelected() == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::StartGame);
    assert(menu.actions().size() == 3);
    assert(menu.activate(MainMenuAction::PlayOnline) ==
           MainMenuResult::RequestPlayOnline);
    menu.openOnline();
    assert(menu.page() == MainMenuPage::PlayOnline);
    assert(menu.actions().size() == 4);
    assert(menu.activate(MainMenuAction::FindGame) ==
           MainMenuResult::RequestFindMatch);
    assert(menu.page() == MainMenuPage::FindMatch);
    menu.connectionLost("Connection to the server was lost.");
    assert(!menu.lobbyWaiting());
    assert(menu.lobbyError() == "Connection to the server was lost.");
    assert(menu.activate(MainMenuAction::FindGame) ==
           MainMenuResult::RequestFindMatch);
    assert(menu.activate(MainMenuAction::CancelFindMatch) ==
           MainMenuResult::RequestCancelFindMatch);
    menu.matchmakingCancelled();
    assert(menu.page() == MainMenuPage::PlayOnline);
    assert(menu.activate(MainMenuAction::HostGame) ==
           MainMenuResult::RequestHostLobby);
    assert(menu.page() == MainMenuPage::HostLobby);
    menu.connectionLost("Connection to the server was lost.");
    assert(!menu.lobbyWaiting());
    assert(menu.lobbyError() == "Connection to the server was lost.");
    assert(menu.activate(MainMenuAction::HostGame) ==
           MainMenuResult::RequestHostLobby);
    menu.lobbyHosted("CAVE7X");
    assert(menu.lobbyCode() == "CAVE7X" && menu.lobbyWaiting());
    assert(menu.activate(MainMenuAction::CancelLobby) ==
           MainMenuResult::RequestCancelLobby);
    menu.lobbyCancelled();
    assert(menu.page() == MainMenuPage::PlayOnline);
    assert(menu.activate(MainMenuAction::JoinGame) == MainMenuResult::None);
    menu.appendLobbyCode("hunt34");
    assert(menu.lobbyCode() == "HUNT34");
    assert(menu.activate(MainMenuAction::SubmitLobbyCode) ==
           MainMenuResult::RequestJoinLobby);
    menu.lobbyAssigned("HUNT34");
    assert(menu.page() == MainMenuPage::MatchReady);
    assert(menu.back() == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::PlayOnline);
    assert(menu.back() == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::StartGame);
    assert(menu.activate(MainMenuAction::PlayAi) == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::PlayAi);
    assert(menu.aiDifficulty() == basilisk::client::ai::AiDifficulty::Medium);
    assert(menu.aiBehavior() == basilisk::client::ai::AiBehavior::Balanced);
    assert(menu.activate(MainMenuAction::CycleAiDifficulty) == MainMenuResult::None);
    assert(menu.aiDifficulty() == basilisk::client::ai::AiDifficulty::Hard);
    assert(menu.activate(MainMenuAction::CycleAiBehavior) == MainMenuResult::None);
    assert(menu.aiBehavior() == basilisk::client::ai::AiBehavior::Explorer);
    assert(menu.activate(MainMenuAction::StartAiGame) == MainMenuResult::StartAiGame);
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
    assert(menu.selectedCallingCard() ==
           basilisk::client::CallingCardId{"arrow-right-black"});
    assert(menu.selectedEmblem() ==
           basilisk::client::EmblemId{"circle-black"});
    menu.selectCallingCard(
        basilisk::client::CallingCardId{"slanted-rectangles-white"});
    assert(menu.selectedCallingCard() ==
           basilisk::client::CallingCardId{"slanted-rectangles-white"});
    menu.selectEmblem(basilisk::client::EmblemId{"rounded-square-green"});
    assert(menu.selectedEmblem() ==
           basilisk::client::EmblemId{"rounded-square-green"});
    menu.applyConfirmedCosmeticLoadout({
        basilisk::client::CallingCardId{"diamonds-flag-white"},
        basilisk::client::EmblemId{"circle-green"}});
    assert(menu.selectedCallingCard() ==
           basilisk::client::CallingCardId{"diamonds-flag-white"});
    assert(menu.selectedEmblem() ==
           basilisk::client::EmblemId{"circle-green"});
    assert(menu.selectedAction() == MainMenuAction::Back);
    assert(menu.activateSelected() == MainMenuResult::None);
    assert(menu.page() == MainMenuPage::Main);
    assert(menu.selectedCallingCard() ==
           basilisk::client::CallingCardId{"diamonds-flag-white"});
    assert(menu.selectedEmblem() ==
           basilisk::client::EmblemId{"circle-green"});

    menu.moveSelection(-1);
    assert(menu.selectedAction() == MainMenuAction::EditProfile);
    menu.moveSelection(-1);
    assert(menu.selectedAction() == MainMenuAction::Exit);
    assert(menu.activateSelected() == MainMenuResult::Exit);
}
