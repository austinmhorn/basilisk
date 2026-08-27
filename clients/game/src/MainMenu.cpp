#include "MainMenu.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace basilisk::game {
namespace {

constexpr std::array mainActions{
    MainMenuAction::StartGame,
    MainMenuAction::Leaderboards,
    MainMenuAction::Settings,
    MainMenuAction::Exit,
    MainMenuAction::EditProfile,
};
constexpr std::array startActions{
    MainMenuAction::PlayOnline,
    MainMenuAction::PlayAi,
    MainMenuAction::Sandbox,
    MainMenuAction::Back,
};
constexpr std::array onlineActions{
    MainMenuAction::FindGame,
    MainMenuAction::HostGame,
    MainMenuAction::JoinGame,
    MainMenuAction::Back,
};
constexpr std::array aiActions{
    MainMenuAction::CycleAiDifficulty,
    MainMenuAction::CycleAiBehavior,
    MainMenuAction::StartAiGame,
    MainMenuAction::Back,
};
constexpr std::array sandboxActions{
    MainMenuAction::CycleSandboxHunters,
    MainMenuAction::CycleSandboxDifficulty,
    MainMenuAction::CycleSandboxBehavior,
    MainMenuAction::StartSandbox,
    MainMenuAction::Back,
};
constexpr std::array leaderboardActions{
    MainMenuAction::PreviousPage,
    MainMenuAction::NextPage,
    MainMenuAction::Back,
};
constexpr std::array settingsActions{MainMenuAction::Logout, MainMenuAction::Back};
constexpr std::array cosmeticsActions{MainMenuAction::Back};
constexpr std::array hostLobbyActions{
    MainMenuAction::CancelLobby, MainMenuAction::Back};
constexpr std::array joinLobbyActions{
    MainMenuAction::SubmitLobbyCode, MainMenuAction::Back};
constexpr std::array matchReadyActions{MainMenuAction::Back};
constexpr std::array findMatchActions{
    MainMenuAction::CancelFindMatch, MainMenuAction::Back};

} // namespace

MainMenuPage MainMenuState::page() const noexcept { return page_; }

std::span<const MainMenuAction> MainMenuState::actions() const noexcept {
    switch (page_) {
        case MainMenuPage::Main: return mainActions;
        case MainMenuPage::StartGame: return startActions;
        case MainMenuPage::PlayOnline: return onlineActions;
        case MainMenuPage::PlayAi: return aiActions;
        case MainMenuPage::Sandbox: return sandboxActions;
        case MainMenuPage::Leaderboards: return leaderboardActions;
        case MainMenuPage::Settings: return settingsActions;
        case MainMenuPage::Cosmetics: return cosmeticsActions;
        case MainMenuPage::HostLobby: return hostLobbyActions;
        case MainMenuPage::JoinLobby: return joinLobbyActions;
        case MainMenuPage::MatchReady: return matchReadyActions;
        case MainMenuPage::FindMatch: return findMatchActions;
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
const std::string& MainMenuState::lobbyCode() const noexcept { return lobbyCode_; }
const std::string& MainMenuState::lobbyError() const noexcept { return lobbyError_; }
bool MainMenuState::lobbyWaiting() const noexcept { return lobbyWaiting_; }
const client::CallingCardId& MainMenuState::selectedCallingCard() const noexcept {
    return selectedCallingCard_;
}
const client::EmblemId& MainMenuState::selectedEmblem() const noexcept {
    return selectedEmblem_;
}
client::ai::AiDifficulty MainMenuState::aiDifficulty() const noexcept { return aiDifficulty_; }
client::ai::AiBehavior MainMenuState::aiBehavior() const noexcept { return aiBehavior_; }
std::size_t MainMenuState::sandboxHunterCount() const noexcept { return sandboxHunterCount_; }
client::ai::AiDifficulty MainMenuState::sandboxDifficulty() const noexcept {
    return sandboxDifficulty_;
}
client::ai::AiBehavior MainMenuState::sandboxBehavior() const noexcept {
    return sandboxBehavior_;
}
void MainMenuState::openOnline() noexcept { setPage(MainMenuPage::PlayOnline); }

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
        case MainMenuAction::PlayOnline:
            return MainMenuResult::RequestPlayOnline;
        case MainMenuAction::PlayAi:
            setPage(MainMenuPage::PlayAi);
            break;
        case MainMenuAction::Sandbox:
            setPage(MainMenuPage::Sandbox);
            break;
        case MainMenuAction::CycleAiDifficulty:
            aiDifficulty_ = static_cast<client::ai::AiDifficulty>(
                (static_cast<int>(aiDifficulty_) + 1) % 3);
            break;
        case MainMenuAction::CycleAiBehavior:
            aiBehavior_ = static_cast<client::ai::AiBehavior>(
                (static_cast<int>(aiBehavior_) + 1) % 7);
            break;
        case MainMenuAction::StartAiGame:
            return MainMenuResult::StartAiGame;
        case MainMenuAction::CycleSandboxHunters:
            sandboxHunterCount_ = sandboxHunterCount_ == 6 ? 2 : sandboxHunterCount_ + 1;
            break;
        case MainMenuAction::CycleSandboxDifficulty:
            sandboxDifficulty_ = static_cast<client::ai::AiDifficulty>(
                (static_cast<int>(sandboxDifficulty_) + 1) % 3);
            break;
        case MainMenuAction::CycleSandboxBehavior:
            sandboxBehavior_ = static_cast<client::ai::AiBehavior>(
                (static_cast<int>(sandboxBehavior_) + 1) % 7);
            break;
        case MainMenuAction::StartSandbox:
            return MainMenuResult::StartSandbox;
        case MainMenuAction::Leaderboards:
            leaderboardOffset_ = 0;
            setPage(MainMenuPage::Leaderboards);
            return MainMenuResult::RequestLeaderboard;
        case MainMenuAction::Settings:
            setPage(MainMenuPage::Settings);
            break;
        case MainMenuAction::EditProfile:
            setPage(MainMenuPage::Cosmetics);
            break;
        case MainMenuAction::Exit:
            return MainMenuResult::Exit;
        case MainMenuAction::Back:
            if (page_ == MainMenuPage::HostLobby && lobbyWaiting_)
                return MainMenuResult::RequestCancelLobby;
            if (page_ == MainMenuPage::FindMatch && lobbyWaiting_)
                return MainMenuResult::RequestCancelFindMatch;
            setPage(page_ == MainMenuPage::JoinLobby ||
                    page_ == MainMenuPage::MatchReady ||
                    page_ == MainMenuPage::FindMatch
                ? MainMenuPage::PlayOnline :
                (page_ == MainMenuPage::PlayOnline || page_ == MainMenuPage::PlayAi ||
                 page_ == MainMenuPage::Sandbox
                    ? MainMenuPage::StartGame : MainMenuPage::Main));
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
            lobbyCode_.clear(); lobbyError_.clear(); lobbyWaiting_ = true;
            setPage(MainMenuPage::FindMatch);
            return MainMenuResult::RequestFindMatch;
        case MainMenuAction::HostGame:
            lobbyCode_.clear();
            lobbyError_.clear();
            lobbyWaiting_ = true;
            setPage(MainMenuPage::HostLobby);
            return MainMenuResult::RequestHostLobby;
        case MainMenuAction::JoinGame:
            lobbyCode_.clear();
            lobbyError_.clear();
            lobbyWaiting_ = false;
            setPage(MainMenuPage::JoinLobby);
            break;
        case MainMenuAction::SubmitLobbyCode:
            if (lobbyCode_.empty()) {
                lobbyError_ = "Enter a lobby code.";
                break;
            }
            lobbyWaiting_ = true;
            lobbyError_.clear();
            return MainMenuResult::RequestJoinLobby;
        case MainMenuAction::CancelLobby:
            return MainMenuResult::RequestCancelLobby;
        case MainMenuAction::CancelFindMatch:
            return MainMenuResult::RequestCancelFindMatch;
    }
    return MainMenuResult::None;
}

MainMenuResult MainMenuState::back() noexcept {
    if (page_ == MainMenuPage::HostLobby && lobbyWaiting_)
        return MainMenuResult::RequestCancelLobby;
    if (page_ == MainMenuPage::FindMatch && lobbyWaiting_)
        return MainMenuResult::RequestCancelFindMatch;
    if (page_ != MainMenuPage::Main)
        setPage(page_ == MainMenuPage::JoinLobby ||
                page_ == MainMenuPage::MatchReady ||
                page_ == MainMenuPage::FindMatch
            ? MainMenuPage::PlayOnline :
            (page_ == MainMenuPage::PlayOnline || page_ == MainMenuPage::PlayAi ||
             page_ == MainMenuPage::Sandbox
                ? MainMenuPage::StartGame : MainMenuPage::Main));
    return MainMenuResult::None;
}

void MainMenuState::appendLobbyCode(std::string_view text) {
    if (page_ != MainMenuPage::JoinLobby || lobbyWaiting_) return;
    for (unsigned char value : text) {
        if (lobbyCode_.size() >= 12) break;
        if (std::isalnum(value) != 0)
            lobbyCode_.push_back(static_cast<char>(std::toupper(value)));
    }
}
void MainMenuState::eraseLobbyCode() {
    if (page_ == MainMenuPage::JoinLobby && !lobbyWaiting_ && !lobbyCode_.empty())
        lobbyCode_.pop_back();
}
void MainMenuState::lobbyHosted(std::string code) {
    lobbyCode_ = std::move(code);
    lobbyWaiting_ = true;
    lobbyError_.clear();
}
void MainMenuState::lobbyAssigned(std::string code) {
    lobbyCode_ = std::move(code);
    lobbyWaiting_ = false;
    lobbyError_.clear();
    setPage(MainMenuPage::MatchReady);
}
void MainMenuState::lobbyCancelled() {
    lobbyCode_.clear(); lobbyError_.clear(); lobbyWaiting_ = false;
    setPage(MainMenuPage::PlayOnline);
}
void MainMenuState::lobbyFailed(std::string error) {
    lobbyWaiting_ = false;
    lobbyError_ = std::move(error);
}
void MainMenuState::connectionLost(std::string error) {
    if (lobbyWaiting_) lobbyFailed(std::move(error));
}
void MainMenuState::matchmakingCancelled() {
    lobbyCode_.clear(); lobbyError_.clear(); lobbyWaiting_ = false;
    setPage(MainMenuPage::PlayOnline);
}

void MainMenuState::selectCallingCard(client::CallingCardId callingCard) {
    selectedCallingCard_ = std::move(callingCard);
}
void MainMenuState::selectEmblem(client::EmblemId emblem) {
    selectedEmblem_ = std::move(emblem);
}

void MainMenuState::applyConfirmedCosmeticLoadout(
    const client::AccountCosmeticLoadout& loadout) {
    selectedCallingCard_ = loadout.callingCardId;
    selectedEmblem_ = loadout.emblemId;
}

void MainMenuState::setPage(MainMenuPage page) noexcept {
    page_ = page;
    selectedIndex_ = 0;
}

} // namespace basilisk::game
