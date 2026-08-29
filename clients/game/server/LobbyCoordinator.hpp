#pragma once

#include <functional>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "NetworkProtocol.hpp"
#include "TrophyScoring.hpp"
#include "basilisk/client/SandboxConfiguration.hpp"

namespace basilisk::game::server {

struct LobbyCode {
    std::string value;
    auto operator<=>(const LobbyCode&) const = default;
};

// Server-private assignment. Account identities never cross the client wire.
struct LobbyMatchAssignment {
    LobbyCode lobby;
    AccountIdentity host;
    AccountIdentity guest;
};

struct SandboxLobbySnapshot {
    LobbyCode lobby;
    client::SandboxSessionConfig config;
    std::vector<network::SandboxLobbySlotView> slots;
};

struct SandboxLobbyChange {
    SandboxLobbySnapshot snapshot;
    std::vector<AccountIdentity> recipients;
    std::vector<AccountIdentity> removed;
    bool closed{};
    std::map<AccountIdentity, PlayerId> memberPlayers;
};
struct SandboxMatchAssignment {
    LobbyCode lobby;
    client::SandboxSessionConfig config;
    std::map<PlayerId, AccountIdentity> humans;
    std::vector<PlayerId> aiPlayers;
};

class LobbyCoordinator {
public:
    using CodeGenerator = std::function<std::string()>;
    struct SandboxLobbyState {
        AccountIdentity host;
        client::SandboxSessionConfig config;
        std::map<std::size_t, AccountIdentity> humans;
        std::map<AccountIdentity, std::string> publicNames;
        std::set<AccountIdentity> ready;
        bool launching{};
    };

    LobbyCoordinator();
    explicit LobbyCoordinator(CodeGenerator codeGenerator);

    [[nodiscard]] bool host(
        const AccountIdentity& account, LobbyCode& code, std::string& error);
    [[nodiscard]] bool join(
        const AccountIdentity& account, const LobbyCode& code,
        LobbyMatchAssignment& assignment, std::string& error);
    [[nodiscard]] bool cancel(
        const AccountIdentity& account, const LobbyCode& code,
        std::string& error);
    // Connection cleanup: cancel any lobby still waiting for this host.
    void cancelHostedBy(const AccountIdentity& account);
    [[nodiscard]] bool findMatch(
        const AccountIdentity& account,
        std::optional<LobbyMatchAssignment>& assignment,
        std::string& error);
    [[nodiscard]] bool cancelFindMatch(
        const AccountIdentity& account, std::string& error);
    [[nodiscard]] bool hostSandbox(
        const AccountIdentity& account,
        std::string publicName,
        const client::SandboxSessionConfig& config,
        SandboxLobbyChange& change, std::string& error);
    [[nodiscard]] bool joinSandbox(
        const AccountIdentity& account, std::string publicName,
        const LobbyCode& code,
        SandboxLobbyChange& change, std::string& error);
    [[nodiscard]] bool leaveSandbox(
        const AccountIdentity& account, const LobbyCode& code,
        SandboxLobbyChange& change, std::string& error);
    [[nodiscard]] bool setSandboxReady(
        const AccountIdentity& account, const LobbyCode& code, bool ready,
        SandboxLobbyChange& change, std::string& error);
    [[nodiscard]] bool startSandbox(
        const AccountIdentity& account, const LobbyCode& code,
        SandboxMatchAssignment& assignment, std::string& error);
    void disconnectSandbox(
        const AccountIdentity& account, std::vector<SandboxLobbyChange>& changes);

private:
    [[nodiscard]] std::string generateCode();

    CodeGenerator codeGenerator_;
    std::map<LobbyCode, AccountIdentity> waiting_;
    std::set<LobbyCode> consumed_;
    std::set<AccountIdentity> waitingHosts_;
    std::deque<AccountIdentity> matchmakingQueue_;
    std::set<AccountIdentity> queuedAccounts_;
    std::map<LobbyCode, SandboxLobbyState> sandboxWaiting_;
    std::map<AccountIdentity, LobbyCode> sandboxMembership_;
    std::mutex mutex_;
};

} // namespace basilisk::game::server
