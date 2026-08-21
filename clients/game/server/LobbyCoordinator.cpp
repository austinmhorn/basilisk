#include "LobbyCoordinator.hpp"

#include <array>
#include <random>
#include <utility>

namespace basilisk::game::server {
namespace {

std::string randomLobbyCode() {
    // Avoid ambiguous characters so codes are easy to read aloud and type.
    static constexpr std::array alphabet{
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'J', 'K', 'M', 'N',
        'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', '3', '4',
        '6', '7', '8', '9'};
    thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick{0, alphabet.size() - 1};
    std::string code;
    code.reserve(6);
    for (int index = 0; index < 6; ++index) code.push_back(alphabet[pick(generator)]);
    return code;
}

} // namespace

LobbyCoordinator::LobbyCoordinator() : codeGenerator_(randomLobbyCode) {}
LobbyCoordinator::LobbyCoordinator(CodeGenerator codeGenerator)
    : codeGenerator_(std::move(codeGenerator)) {}

bool LobbyCoordinator::host(
    const AccountIdentity& account, LobbyCode& code, std::string& error) {
    if (account.value.empty()) {
        error = "An authenticated account is required.";
        return false;
    }
    std::lock_guard lock(mutex_);
    if (waitingHosts_.contains(account)) {
        error = "Account is already hosting a lobby.";
        return false;
    }
    for (int attempt = 0; attempt < 128; ++attempt) {
        LobbyCode candidate{generateCode()};
        if (candidate.value.empty() || waiting_.contains(candidate) ||
            consumed_.contains(candidate)) continue;
        waiting_.emplace(candidate, account);
        waitingHosts_.insert(account);
        code = std::move(candidate);
        error.clear();
        return true;
    }
    error = "Unable to allocate a unique lobby code.";
    return false;
}

bool LobbyCoordinator::join(
    const AccountIdentity& account, const LobbyCode& code,
    LobbyMatchAssignment& assignment, std::string& error) {
    if (account.value.empty()) {
        error = "An authenticated account is required.";
        return false;
    }
    std::lock_guard lock(mutex_);
    const auto found = waiting_.find(code);
    if (found == waiting_.end()) {
        error = "Lobby code is invalid or no longer available.";
        return false;
    }
    if (found->second == account) {
        error = "A player cannot join their own lobby.";
        return false;
    }
    assignment = LobbyMatchAssignment{code, found->second, account};
    waitingHosts_.erase(found->second);
    waiting_.erase(found);
    consumed_.insert(code);
    error.clear();
    return true;
}

bool LobbyCoordinator::cancel(
    const AccountIdentity& account, const LobbyCode& code,
    std::string& error) {
    std::lock_guard lock(mutex_);
    const auto found = waiting_.find(code);
    if (found == waiting_.end() || found->second != account) {
        error = "Lobby cannot be cancelled by this account.";
        return false;
    }
    waitingHosts_.erase(account);
    waiting_.erase(found);
    consumed_.insert(code);
    error.clear();
    return true;
}

void LobbyCoordinator::cancelHostedBy(const AccountIdentity& account) {
    std::lock_guard lock(mutex_);
    for (auto found = waiting_.begin(); found != waiting_.end(); ++found) {
        if (found->second != account) continue;
        consumed_.insert(found->first);
        waiting_.erase(found);
        waitingHosts_.erase(account);
        return;
    }
}

std::string LobbyCoordinator::generateCode() { return codeGenerator_(); }

} // namespace basilisk::game::server
