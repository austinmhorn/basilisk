#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>

#include "TrophyScoring.hpp"

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

class LobbyCoordinator {
public:
    using CodeGenerator = std::function<std::string()>;

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

private:
    [[nodiscard]] std::string generateCode();

    CodeGenerator codeGenerator_;
    std::map<LobbyCode, AccountIdentity> waiting_;
    std::set<LobbyCode> consumed_;
    std::set<AccountIdentity> waitingHosts_;
    std::mutex mutex_;
};

} // namespace basilisk::game::server
