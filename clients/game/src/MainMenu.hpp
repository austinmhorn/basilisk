#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "NetworkProtocol.hpp"
#include "basilisk/client/PlayerProfile.hpp"
#include "basilisk/client/AccountCosmetics.hpp"
#include "basilisk/client/SandboxConfiguration.hpp"
#include "basilisk/client/ai/AiDecisionEngine.hpp"

namespace basilisk::game {

enum class MainMenuPage {
    Main,
    StartGame,
    PlayOnline,
    OnlineStandard,
    PlayAi,
    AiStandard,
    OnlineSandbox,
    Sandbox,
    SandboxLobby,
    JoinSandboxLobby,
    Leaderboards,
    Settings,
    Cosmetics,
    HostLobby,
    JoinLobby,
    MatchReady,
    FindMatch,
};

enum class MainMenuAction {
    StartGame,
    PlayOnline,
    PlayAi,
    StandardOnline,
    SandboxOnline,
    StandardAi,
    SandboxAi,
    HostSandboxGame,
    JoinSandboxGame,
    CycleAiDifficulty,
    CycleAiBehavior,
    StartAiGame,
    CycleSandboxHunters,
    CycleSandboxHumanPlayers,
    CycleSandboxCaves,
    CycleSandboxJackals,
    CycleSandboxArrowFrequency,
    CycleSandboxStartingArrows,
    CycleSandboxMaxArrows,
    CycleSandboxDifficulty,
    CycleSandboxBehavior,
    CreateSandboxLobby,
    LaunchSandbox,
    Leaderboards,
    Settings,
    EditProfile,
    Exit,
    FindGame,
    HostGame,
    JoinGame,
    Back,
    PreviousPage,
    NextPage,
    SubmitLobbyCode,
    CancelLobby,
    CancelFindMatch,
    Logout,
};

enum class SandboxEntryMode { Online, Ai };

enum class MainMenuResult {
    None,
    Exit,
    RequestLeaderboard,
    RequestHostLobby,
    RequestJoinLobby,
    RequestCancelLobby,
    RequestFindMatch,
    RequestCancelFindMatch,
    RequestHostSandboxLobby,
    RequestJoinSandboxLobby,
    RequestLeaveSandboxLobby,
    Logout,
    RequestPlayOnline,
    StartAiGame,
    StartSandbox,
};

class MainMenuState {
public:
    static constexpr std::uint32_t leaderboardPageSize{10};

    [[nodiscard]] MainMenuPage page() const noexcept;
    [[nodiscard]] std::span<const MainMenuAction> actions() const noexcept;
    [[nodiscard]] std::size_t selectedIndex() const noexcept;
    [[nodiscard]] MainMenuAction selectedAction() const noexcept;
    [[nodiscard]] std::uint32_t leaderboardOffset() const noexcept;
    [[nodiscard]] const std::string& lobbyCode() const noexcept;
    [[nodiscard]] const std::string& lobbyError() const noexcept;
    [[nodiscard]] bool lobbyWaiting() const noexcept;
    [[nodiscard]] const client::CallingCardId& selectedCallingCard() const noexcept;
    [[nodiscard]] const client::EmblemId& selectedEmblem() const noexcept;
    [[nodiscard]] client::ai::AiDifficulty aiDifficulty() const noexcept;
    [[nodiscard]] client::ai::AiBehavior aiBehavior() const noexcept;
    [[nodiscard]] std::size_t sandboxHunterCount() const noexcept;
    [[nodiscard]] const client::SandboxSessionConfig& sandboxConfig() const noexcept;
    [[nodiscard]] const std::string& sandboxValidationError() const noexcept;
    [[nodiscard]] client::ai::AiDifficulty sandboxDifficulty() const noexcept;
    [[nodiscard]] client::ai::AiBehavior sandboxBehavior() const noexcept;
    [[nodiscard]] SandboxEntryMode sandboxEntryMode() const noexcept;
    [[nodiscard]] const std::vector<network::SandboxLobbySlotView>&
        sandboxLobbyRoster() const noexcept;
    void openOnline() noexcept;

    void select(std::size_t index) noexcept;
    void moveSelection(int delta) noexcept;
    void adjustSelected(int delta) noexcept;
    [[nodiscard]] MainMenuResult activateSelected() noexcept;
    [[nodiscard]] MainMenuResult activate(MainMenuAction action) noexcept;
    [[nodiscard]] MainMenuResult back() noexcept;
    void appendLobbyCode(std::string_view text);
    void eraseLobbyCode();
    void lobbyHosted(std::string code);
    void lobbyAssigned(std::string code);
    void lobbyCancelled();
    void lobbyFailed(std::string error);
    void connectionLost(std::string error);
    void matchmakingCancelled();
    void selectCallingCard(client::CallingCardId callingCard);
    void selectEmblem(client::EmblemId emblem);
    void applyConfirmedCosmeticLoadout(
        const client::AccountCosmeticLoadout& loadout);
    void setSandboxConfig(client::SandboxSessionConfig config);
    void sandboxLobbyUpdated(network::SandboxLobbyUpdated update);
    void sandboxLobbyClosed(std::string message);

private:
    void setPage(MainMenuPage page) noexcept;
    void adjust(MainMenuAction action, int delta) noexcept;

    MainMenuPage page_{MainMenuPage::Main};
    std::size_t selectedIndex_{0};
    std::uint32_t leaderboardOffset_{0};
    std::string lobbyCode_;
    std::string lobbyError_;
    bool lobbyWaiting_{false};
    client::CallingCardId selectedCallingCard_{"arrow-right-black"};
    client::EmblemId selectedEmblem_{"circle-black"};
    client::ai::AiDifficulty aiDifficulty_{client::ai::AiDifficulty::Medium};
    client::ai::AiBehavior aiBehavior_{client::ai::AiBehavior::Balanced};
    client::SandboxSessionConfig sandboxConfig_ = client::defaultSandboxSessionConfig();
    SandboxEntryMode sandboxEntryMode_{SandboxEntryMode::Ai};
    std::string sandboxValidationError_;
    std::vector<network::SandboxLobbySlotView> sandboxLobbyRoster_;
};

} // namespace basilisk::game
