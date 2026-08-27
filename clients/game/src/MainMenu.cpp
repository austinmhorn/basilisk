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
    MainMenuAction::Back,
};
constexpr std::array onlineActions{
    MainMenuAction::StandardOnline,
    MainMenuAction::SandboxOnline,
    MainMenuAction::Back,
};
constexpr std::array onlineStandardActions{
    MainMenuAction::FindGame,
    MainMenuAction::HostGame,
    MainMenuAction::JoinGame,
    MainMenuAction::Back,
};
constexpr std::array aiActions{
    MainMenuAction::StandardAi,
    MainMenuAction::SandboxAi,
    MainMenuAction::Back,
};
constexpr std::array onlineSandboxActions{
    MainMenuAction::HostSandboxGame,
    MainMenuAction::JoinSandboxGame,
    MainMenuAction::Back,
};
constexpr std::array joinSandboxLobbyActions{
    MainMenuAction::SubmitLobbyCode,
    MainMenuAction::Back,
};
constexpr std::array aiStandardActions{
    MainMenuAction::CycleAiDifficulty,
    MainMenuAction::CycleAiBehavior,
    MainMenuAction::StartAiGame,
    MainMenuAction::Back,
};
constexpr std::array sandboxActions{
    MainMenuAction::CycleSandboxHunters,
    MainMenuAction::CycleSandboxHumanPlayers,
    MainMenuAction::CycleSandboxCaves,
    MainMenuAction::CycleSandboxJackals,
    MainMenuAction::CycleSandboxArrowFrequency,
    MainMenuAction::CycleSandboxStartingArrows,
    MainMenuAction::CycleSandboxMaxArrows,
    MainMenuAction::CycleSandboxDifficulty,
    MainMenuAction::CycleSandboxBehavior,
    MainMenuAction::CreateSandboxLobby,
    MainMenuAction::Back,
};
constexpr std::array sandboxAiActions{
    MainMenuAction::CycleSandboxHunters,
    MainMenuAction::CycleSandboxCaves,
    MainMenuAction::CycleSandboxJackals,
    MainMenuAction::CycleSandboxArrowFrequency,
    MainMenuAction::CycleSandboxStartingArrows,
    MainMenuAction::CycleSandboxMaxArrows,
    MainMenuAction::CycleSandboxDifficulty,
    MainMenuAction::CycleSandboxBehavior,
    MainMenuAction::LaunchSandbox,
    MainMenuAction::Back,
};
constexpr std::array sandboxLobbyActions{
    MainMenuAction::LaunchSandbox,
    MainMenuAction::Back,
};
constexpr std::array onlineSandboxLobbyActions{MainMenuAction::Back};
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
        case MainMenuPage::OnlineStandard: return onlineStandardActions;
        case MainMenuPage::PlayAi: return aiActions;
        case MainMenuPage::AiStandard: return aiStandardActions;
        case MainMenuPage::OnlineSandbox: return onlineSandboxActions;
        case MainMenuPage::Sandbox: return sandboxEntryMode_ == SandboxEntryMode::Online
            ? std::span<const MainMenuAction>{sandboxActions}
            : std::span<const MainMenuAction>{sandboxAiActions};
        case MainMenuPage::SandboxLobby: return sandboxEntryMode_ ==
                SandboxEntryMode::Online
            ? std::span<const MainMenuAction>{onlineSandboxLobbyActions}
            : std::span<const MainMenuAction>{sandboxLobbyActions};
        case MainMenuPage::JoinSandboxLobby: return joinSandboxLobbyActions;
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
std::size_t MainMenuState::sandboxHunterCount() const noexcept {
    return sandboxConfig_.hunterCount;
}
const client::SandboxSessionConfig& MainMenuState::sandboxConfig() const noexcept {
    return sandboxConfig_;
}
const std::string& MainMenuState::sandboxValidationError() const noexcept {
    return sandboxValidationError_;
}
client::ai::AiDifficulty MainMenuState::sandboxDifficulty() const noexcept {
    return sandboxConfig_.aiDifficulty;
}
client::ai::AiBehavior MainMenuState::sandboxBehavior() const noexcept {
    return sandboxConfig_.aiBehavior;
}
SandboxEntryMode MainMenuState::sandboxEntryMode() const noexcept {
    return sandboxEntryMode_;
}
const std::vector<network::SandboxLobbySlotView>&
MainMenuState::sandboxLobbyRoster() const noexcept { return sandboxLobbyRoster_; }
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

void MainMenuState::adjustSelected(int delta) noexcept {
    adjust(selectedAction(), delta);
}

void MainMenuState::adjust(MainMenuAction action, int delta) noexcept {
    if (delta == 0) return;
    if (page_ == MainMenuPage::AiStandard) {
        const int direction = delta < 0 ? -1 : 1;
        if (action == MainMenuAction::CycleAiDifficulty) {
            const int count = 3;
            aiDifficulty_ = static_cast<client::ai::AiDifficulty>(
                (static_cast<int>(aiDifficulty_) + direction + count) % count);
        } else if (action == MainMenuAction::CycleAiBehavior) {
            const int count = 7;
            aiBehavior_ = static_cast<client::ai::AiBehavior>(
                (static_cast<int>(aiBehavior_) + direction + count) % count);
        }
        return;
    }
    if (page_ != MainMenuPage::Sandbox) return;
    const auto wrap = [delta](std::size_t value, std::size_t minimum,
                          std::size_t maximum) {
        if (delta < 0) return value == minimum ? maximum : value - 1;
        return value == maximum ? minimum : value + 1;
    };
    switch (action) {
        case MainMenuAction::CycleSandboxHunters: {
            const auto oldCaves = sandboxConfig_.caveCount;
            const bool defaultCaves = oldCaves ==
                client::defaultSandboxCaves(sandboxConfig_.hunterCount);
            const bool defaultJackals = sandboxConfig_.jackalCount ==
                client::defaultSandboxJackals(oldCaves);
            sandboxConfig_.hunterCount = wrap(sandboxConfig_.hunterCount, 2, 6);
            sandboxConfig_.humanPlayerCount = std::min(
                sandboxConfig_.humanPlayerCount, sandboxConfig_.hunterCount);
            sandboxConfig_.caveCount = defaultCaves
                ? client::defaultSandboxCaves(sandboxConfig_.hunterCount)
                : std::max(sandboxConfig_.caveCount,
                    client::minimumSandboxCaves(sandboxConfig_.hunterCount));
            sandboxConfig_.jackalCount = defaultJackals
                ? client::defaultSandboxJackals(sandboxConfig_.caveCount)
                : std::min(sandboxConfig_.jackalCount,
                    client::maximumSandboxJackals(sandboxConfig_.caveCount));
            break;
        }
        case MainMenuAction::CycleSandboxHumanPlayers:
            sandboxConfig_.humanPlayerCount = wrap(
                sandboxConfig_.humanPlayerCount,
                sandboxEntryMode_ == SandboxEntryMode::Online ? 2 : 1,
                sandboxConfig_.hunterCount);
            break;
        case MainMenuAction::CycleSandboxCaves: {
            const bool defaultJackals = sandboxConfig_.jackalCount ==
                client::defaultSandboxJackals(sandboxConfig_.caveCount);
            std::vector<std::size_t> valid;
            for (const auto caves : client::sandboxCaveCounts)
                if (caves >= client::minimumSandboxCaves(sandboxConfig_.hunterCount))
                    valid.push_back(caves);
            auto current = std::find(valid.begin(), valid.end(), sandboxConfig_.caveCount);
            std::size_t index = current == valid.end() ? 0 :
                static_cast<std::size_t>(current - valid.begin());
            index = delta < 0 ? (index == 0 ? valid.size() - 1 : index - 1)
                              : (index + 1) % valid.size();
            sandboxConfig_.caveCount = valid[index];
            sandboxConfig_.jackalCount = defaultJackals
                ? client::defaultSandboxJackals(sandboxConfig_.caveCount)
                : std::min(sandboxConfig_.jackalCount,
                    client::maximumSandboxJackals(sandboxConfig_.caveCount));
            break;
        }
        case MainMenuAction::CycleSandboxJackals:
            sandboxConfig_.jackalCount = wrap(sandboxConfig_.jackalCount, 0,
                client::maximumSandboxJackals(sandboxConfig_.caveCount));
            break;
        case MainMenuAction::CycleSandboxStartingArrows:
            sandboxConfig_.startingArrows = static_cast<int>(wrap(
                static_cast<std::size_t>(sandboxConfig_.startingArrows), 0,
                static_cast<std::size_t>(sandboxConfig_.maxArrows)));
            break;
        case MainMenuAction::CycleSandboxMaxArrows:
            sandboxConfig_.maxArrows = static_cast<int>(wrap(
                static_cast<std::size_t>(sandboxConfig_.maxArrows),
                static_cast<std::size_t>(sandboxConfig_.startingArrows),
                static_cast<std::size_t>(client::sandboxMaximumArrowCapacity)));
            break;
        default: return;
    }
    sandboxValidationError_.clear();
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
        case MainMenuAction::StandardOnline:
            setPage(MainMenuPage::OnlineStandard);
            break;
        case MainMenuAction::SandboxOnline:
            sandboxEntryMode_ = SandboxEntryMode::Online;
            sandboxConfig_.humanPlayerCount = std::max<std::size_t>(
                2, sandboxConfig_.humanPlayerCount);
            setPage(MainMenuPage::OnlineSandbox);
            break;
        case MainMenuAction::HostSandboxGame:
            sandboxEntryMode_ = SandboxEntryMode::Online;
            sandboxConfig_.humanPlayerCount = std::max<std::size_t>(
                2, sandboxConfig_.humanPlayerCount);
            setPage(MainMenuPage::Sandbox);
            break;
        case MainMenuAction::JoinSandboxGame:
            lobbyCode_.clear();
            lobbyError_.clear();
            lobbyWaiting_ = false;
            setPage(MainMenuPage::JoinSandboxLobby);
            break;
        case MainMenuAction::StandardAi:
            setPage(MainMenuPage::AiStandard);
            break;
        case MainMenuAction::SandboxAi:
            sandboxEntryMode_ = SandboxEntryMode::Ai;
            sandboxConfig_.humanPlayerCount = 1;
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
        case MainMenuAction::CycleSandboxHumanPlayers:
        case MainMenuAction::CycleSandboxCaves:
        case MainMenuAction::CycleSandboxJackals:
        case MainMenuAction::CycleSandboxStartingArrows:
        case MainMenuAction::CycleSandboxMaxArrows:
            adjust(action, 1);
            break;
        case MainMenuAction::CycleSandboxArrowFrequency:
        {
            const auto current = std::find(client::sandboxArrowSpawnIntervals.begin(),
                client::sandboxArrowSpawnIntervals.end(),
                sandboxConfig_.arrowSpawnIntervalRounds);
            const auto index = current == client::sandboxArrowSpawnIntervals.end()
                ? 0U : static_cast<std::size_t>(
                    current - client::sandboxArrowSpawnIntervals.begin() + 1) %
                    client::sandboxArrowSpawnIntervals.size();
            sandboxConfig_.arrowSpawnIntervalRounds =
                client::sandboxArrowSpawnIntervals[index];
            sandboxValidationError_.clear();
            break;
        }
        case MainMenuAction::CycleSandboxDifficulty:
            sandboxConfig_.aiDifficulty = static_cast<client::ai::AiDifficulty>(
                (static_cast<int>(sandboxConfig_.aiDifficulty) + 1) % 3);
            break;
        case MainMenuAction::CycleSandboxBehavior:
            sandboxConfig_.aiBehavior = static_cast<client::ai::AiBehavior>(
                (static_cast<int>(sandboxConfig_.aiBehavior) + 1) % 7);
            break;
        case MainMenuAction::CreateSandboxLobby:
            if (const auto error = client::validateOnlineSandboxSessionConfig(
                    sandboxConfig_)) {
                sandboxValidationError_ = std::string{*error};
                break;
            }
            sandboxValidationError_.clear();
            lobbyWaiting_ = true;
            lobbyError_.clear();
            return MainMenuResult::RequestHostSandboxLobby;
        case MainMenuAction::LaunchSandbox:
            if (sandboxEntryMode_ == SandboxEntryMode::Online) {
                sandboxValidationError_ = "Waiting for human players.";
                break;
            }
            if (const auto error = client::validateSandboxSessionConfig(sandboxConfig_)) {
                sandboxValidationError_ = std::string{*error};
                break;
            }
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
            if (page_ == MainMenuPage::SandboxLobby) {
                return MainMenuResult::RequestLeaveSandboxLobby;
            }
            if (page_ == MainMenuPage::Sandbox) {
                setPage(sandboxEntryMode_ == SandboxEntryMode::Online
                    ? MainMenuPage::OnlineSandbox : MainMenuPage::PlayAi);
                break;
            }
            if (page_ == MainMenuPage::OnlineSandbox ||
                page_ == MainMenuPage::JoinSandboxLobby) {
                setPage(page_ == MainMenuPage::JoinSandboxLobby
                    ? MainMenuPage::OnlineSandbox : MainMenuPage::PlayOnline);
                break;
            }
            if (page_ == MainMenuPage::OnlineStandard) {
                setPage(MainMenuPage::PlayOnline);
                break;
            }
            if (page_ == MainMenuPage::AiStandard) {
                setPage(MainMenuPage::PlayAi);
                break;
            }
            setPage(page_ == MainMenuPage::JoinLobby ||
                    page_ == MainMenuPage::MatchReady ||
                    page_ == MainMenuPage::FindMatch
                ? MainMenuPage::OnlineStandard :
                (page_ == MainMenuPage::PlayOnline || page_ == MainMenuPage::PlayAi
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
            return page_ == MainMenuPage::JoinSandboxLobby
                ? MainMenuResult::RequestJoinSandboxLobby
                : MainMenuResult::RequestJoinLobby;
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
    if (page_ == MainMenuPage::SandboxLobby) {
        return MainMenuResult::RequestLeaveSandboxLobby;
    } else if (page_ == MainMenuPage::Sandbox) {
        setPage(sandboxEntryMode_ == SandboxEntryMode::Online
            ? MainMenuPage::OnlineSandbox : MainMenuPage::PlayAi);
    } else if (page_ == MainMenuPage::OnlineSandbox ||
               page_ == MainMenuPage::JoinSandboxLobby) {
        setPage(page_ == MainMenuPage::JoinSandboxLobby
            ? MainMenuPage::OnlineSandbox : MainMenuPage::PlayOnline);
    } else if (page_ == MainMenuPage::OnlineStandard) {
        setPage(MainMenuPage::PlayOnline);
    } else if (page_ == MainMenuPage::AiStandard) {
        setPage(MainMenuPage::PlayAi);
    } else if (page_ != MainMenuPage::Main) {
        setPage(page_ == MainMenuPage::JoinLobby ||
                page_ == MainMenuPage::MatchReady ||
                page_ == MainMenuPage::FindMatch
            ? MainMenuPage::OnlineStandard :
            (page_ == MainMenuPage::PlayOnline || page_ == MainMenuPage::PlayAi
                ? MainMenuPage::StartGame : MainMenuPage::Main));
    }
    return MainMenuResult::None;
}

void MainMenuState::appendLobbyCode(std::string_view text) {
    if ((page_ != MainMenuPage::JoinLobby &&
         page_ != MainMenuPage::JoinSandboxLobby) || lobbyWaiting_) return;
    for (unsigned char value : text) {
        if (lobbyCode_.size() >= 12) break;
        if (std::isalnum(value) != 0 ||
            (page_ == MainMenuPage::JoinSandboxLobby && value == '-'))
            lobbyCode_.push_back(static_cast<char>(std::toupper(value)));
    }
}
void MainMenuState::eraseLobbyCode() {
    if ((page_ == MainMenuPage::JoinLobby ||
         page_ == MainMenuPage::JoinSandboxLobby) &&
        !lobbyWaiting_ && !lobbyCode_.empty())
        lobbyCode_.pop_back();
}

void MainMenuState::sandboxLobbyUpdated(network::SandboxLobbyUpdated update) {
    lobbyCode_ = std::move(update.lobbyCode);
    sandboxConfig_ = update.config;
    sandboxLobbyRoster_ = std::move(update.slots);
    lobbyWaiting_ = false;
    lobbyError_.clear();
    setPage(MainMenuPage::SandboxLobby);
}

void MainMenuState::sandboxLobbyClosed(std::string message) {
    sandboxLobbyRoster_.clear();
    lobbyCode_.clear();
    lobbyWaiting_ = false;
    lobbyError_ = std::move(message);
    setPage(MainMenuPage::OnlineSandbox);
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
    setPage(MainMenuPage::OnlineStandard);
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
    setPage(MainMenuPage::OnlineStandard);
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

void MainMenuState::setSandboxConfig(client::SandboxSessionConfig config) {
    sandboxConfig_ = std::move(config);
    sandboxValidationError_.clear();
}

void MainMenuState::setPage(MainMenuPage page) noexcept {
    page_ = page;
    selectedIndex_ = 0;
}

} // namespace basilisk::game
