#include "LobbyCoordinator.hpp"

#include <algorithm>
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

bool LobbyCoordinator::findMatch(
    const AccountIdentity& account,
    std::optional<LobbyMatchAssignment>& assignment,
    std::string& error) {
    if (account.value.empty()) {
        error = "An authenticated account is required.";
        return false;
    }
    std::lock_guard lock(mutex_);
    if (queuedAccounts_.contains(account)) {
        error = "Account is already queued for matchmaking.";
        return false;
    }
    assignment.reset();
    if (matchmakingQueue_.empty()) {
        matchmakingQueue_.push_back(account);
        queuedAccounts_.insert(account);
        error.clear();
        return true;
    }
    const AccountIdentity first = matchmakingQueue_.front();
    matchmakingQueue_.pop_front();
    queuedAccounts_.erase(first);
    LobbyCode matchCode;
    for (int attempt = 0; attempt < 128; ++attempt) {
        LobbyCode candidate{generateCode()};
        if (candidate.value.empty() || waiting_.contains(candidate) ||
            consumed_.contains(candidate)) continue;
        matchCode = std::move(candidate);
        consumed_.insert(matchCode);
        break;
    }
    if (matchCode.value.empty()) {
        matchmakingQueue_.push_front(first);
        queuedAccounts_.insert(first);
        error = "Unable to allocate a matchmaking assignment.";
        return false;
    }
    assignment = LobbyMatchAssignment{std::move(matchCode), first, account};
    error.clear();
    return true;
}

bool LobbyCoordinator::cancelFindMatch(
    const AccountIdentity& account, std::string& error) {
    std::lock_guard lock(mutex_);
    if (!queuedAccounts_.erase(account)) {
        error = "Account is not queued for matchmaking.";
        return false;
    }
    const auto found = std::find(
        matchmakingQueue_.begin(), matchmakingQueue_.end(), account);
    if (found != matchmakingQueue_.end()) matchmakingQueue_.erase(found);
    error.clear();
    return true;
}

std::string LobbyCoordinator::generateCode() { return codeGenerator_(); }

} // namespace basilisk::game::server
