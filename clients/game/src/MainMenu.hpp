#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace basilisk::game {

enum class MainMenuPage {
    Main,
    StartGame,
    Leaderboards,
    Settings,
    HostLobby,
    JoinLobby,
    MatchReady,
    FindMatch,
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
    SubmitLobbyCode,
    CancelLobby,
    CancelFindMatch,
    Logout,
};

enum class MainMenuResult {
    None,
    Exit,
    RequestLeaderboard,
    RequestHostLobby,
    RequestJoinLobby,
    RequestCancelLobby,
    RequestFindMatch,
    RequestCancelFindMatch,
    Logout,
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

    void select(std::size_t index) noexcept;
    void moveSelection(int delta) noexcept;
    [[nodiscard]] MainMenuResult activateSelected() noexcept;
    [[nodiscard]] MainMenuResult activate(MainMenuAction action) noexcept;
    [[nodiscard]] MainMenuResult back() noexcept;
    void appendLobbyCode(std::string_view text);
    void eraseLobbyCode();
    void lobbyHosted(std::string code);
    void lobbyAssigned(std::string code);
    void lobbyCancelled();
    void lobbyFailed(std::string error);
    void matchmakingCancelled();

private:
    void setPage(MainMenuPage page) noexcept;

    MainMenuPage page_{MainMenuPage::Main};
    std::size_t selectedIndex_{0};
    std::uint32_t leaderboardOffset_{0};
    std::string lobbyCode_;
    std::string lobbyError_;
    bool lobbyWaiting_{false};
};

} // namespace basilisk::game
