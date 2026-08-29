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

namespace {
SandboxLobbySnapshot sandboxSnapshot(
    const LobbyCode& code, const LobbyCoordinator::SandboxLobbyState& state) {
    SandboxLobbySnapshot snapshot{code, state.config, {}};
    snapshot.slots.reserve(state.config.hunterCount);
    for (std::size_t index = 0; index < state.config.hunterCount; ++index) {
        const std::size_t slot = index + 1;
        const auto kind = index == 0 ? client::SandboxLobbySlotKind::Host :
            (index < state.config.humanPlayerCount
                ? client::SandboxLobbySlotKind::EmptyHuman
                : client::SandboxLobbySlotKind::Ai);
        snapshot.slots.push_back({static_cast<std::uint8_t>(slot),
            static_cast<PlayerId>(slot), kind,
            kind == client::SandboxLobbySlotKind::Ai ||
                state.humans.contains(slot),
            kind == client::SandboxLobbySlotKind::Host ||
                kind == client::SandboxLobbySlotKind::Ai ||
                (state.humans.contains(slot) &&
                 state.ready.contains(state.humans.at(slot))),
            state.humans.contains(slot)
                ? state.publicNames.at(state.humans.at(slot)) : std::string{}});
    }
    return snapshot;
}

std::map<AccountIdentity, PlayerId> sandboxMemberPlayers(
    const LobbyCoordinator::SandboxLobbyState& state) {
    std::map<AccountIdentity, PlayerId> players;
    for (const auto& [slot, account] : state.humans)
        players.emplace(account, PlayerId{static_cast<std::uint32_t>(slot)});
    return players;
}

std::vector<AccountIdentity> sandboxRecipients(
    const LobbyCoordinator::SandboxLobbyState& state) {
    std::vector<AccountIdentity> recipients;
    recipients.reserve(state.humans.size());
    for (const auto& [slot, account] : state.humans) {
        (void)slot;
        recipients.push_back(account);
    }
    return recipients;
}
} // namespace

bool LobbyCoordinator::hostSandbox(
    const AccountIdentity& account,
    std::string publicName,
    const client::SandboxSessionConfig& config,
    SandboxLobbyChange& change,
    std::string& error) {
    if (account.value.empty() || publicName.empty()) {
        error = "An authenticated account is required.";
        return false;
    }
    if (const auto invalid = client::validateOnlineSandboxSessionConfig(config)) {
        error = std::string{*invalid};
        return false;
    }
    std::lock_guard lock(mutex_);
    if (sandboxMembership_.contains(account)) {
        error = "Account is already in a Sandbox lobby.";
        return false;
    }
    for (int attempt = 0; attempt < 128; ++attempt) {
        LobbyCode code{"SBX-" + generateCode()};
        if (code.value.size() <= 4 || sandboxWaiting_.contains(code) ||
            waiting_.contains(code) || consumed_.contains(code)) continue;
        SandboxLobbyState state{
            account, config, {{1, account}}, {{account, publicName}}};
        auto [found, inserted] = sandboxWaiting_.emplace(code, std::move(state));
        if (!inserted) continue;
        sandboxMembership_.emplace(account, code);
        change = {sandboxSnapshot(code, found->second), {account}, {}, false,
            sandboxMemberPlayers(found->second)};
        error.clear();
        return true;
    }
    error = "Unable to allocate a unique Sandbox lobby code.";
    return false;
}

bool LobbyCoordinator::joinSandbox(
    const AccountIdentity& account, std::string publicName,
    const LobbyCode& code,
    SandboxLobbyChange& change, std::string& error) {
    if (account.value.empty() || publicName.empty()) {
        error = "An authenticated account is required.";
        return false;
    }
    std::lock_guard lock(mutex_);
    if (sandboxMembership_.contains(account)) {
        error = "Account is already in a Sandbox lobby.";
        return false;
    }
    const auto found = sandboxWaiting_.find(code);
    if (found == sandboxWaiting_.end()) {
        error = "Sandbox lobby code is invalid or no longer available.";
        return false;
    }
    auto& state = found->second;
    std::size_t openSlot = 0;
    for (std::size_t slot = 2; slot <= state.config.humanPlayerCount; ++slot) {
        if (!state.humans.contains(slot)) {
            openSlot = slot;
            break;
        }
    }
    if (openSlot == 0) {
        error = "Sandbox lobby is full.";
        return false;
    }
    state.humans.emplace(openSlot, account);
    state.publicNames.emplace(account, std::move(publicName));
    sandboxMembership_.emplace(account, code);
    change = {sandboxSnapshot(code, state), sandboxRecipients(state), {}, false,
        sandboxMemberPlayers(state)};
    error.clear();
    return true;
}

bool LobbyCoordinator::leaveSandbox(
    const AccountIdentity& account, const LobbyCode& code,
    SandboxLobbyChange& change, std::string& error) {
    std::lock_guard lock(mutex_);
    const auto member = sandboxMembership_.find(account);
    const auto found = sandboxWaiting_.find(code);
    if (member == sandboxMembership_.end() || member->second != code ||
        found == sandboxWaiting_.end()) {
        error = "Account is not a member of this Sandbox lobby.";
        return false;
    }
    auto& state = found->second;
    if (state.host == account) {
        change.snapshot = sandboxSnapshot(code, state);
        change.recipients = sandboxRecipients(state);
        change.removed = change.recipients;
        change.closed = true;
        change.memberPlayers = sandboxMemberPlayers(state);
        for (const auto& recipient : change.recipients)
            sandboxMembership_.erase(recipient);
        sandboxWaiting_.erase(found);
    } else {
        state.ready.erase(account);
        state.publicNames.erase(account);
        for (auto slot = state.humans.begin(); slot != state.humans.end(); ++slot) {
            if (slot->second != account) continue;
            state.humans.erase(slot);
            break;
        }
        sandboxMembership_.erase(member);
        change = {sandboxSnapshot(code, state), sandboxRecipients(state),
            {account}, false, sandboxMemberPlayers(state)};
    }
    error.clear();
    return true;
}

bool LobbyCoordinator::setSandboxReady(
    const AccountIdentity& account, const LobbyCode& code, bool ready,
    SandboxLobbyChange& change, std::string& error) {
    std::lock_guard lock(mutex_);
    const auto member = sandboxMembership_.find(account);
    const auto found = sandboxWaiting_.find(code);
    if (member == sandboxMembership_.end() || member->second != code ||
        found == sandboxWaiting_.end() || found->second.launching) {
        error = "Sandbox lobby membership is stale.";
        return false;
    }
    auto& state = found->second;
    if (state.host == account) {
        error = "The host does not use ready state.";
        return false;
    }
    if (ready) state.ready.insert(account);
    else state.ready.erase(account);
    change = {sandboxSnapshot(code, state), sandboxRecipients(state), {}, false,
        sandboxMemberPlayers(state)};
    error.clear();
    return true;
}

bool LobbyCoordinator::startSandbox(
    const AccountIdentity& account, const LobbyCode& code,
    SandboxMatchAssignment& assignment, std::string& error) {
    std::lock_guard lock(mutex_);
    const auto found = sandboxWaiting_.find(code);
    if (found == sandboxWaiting_.end() || found->second.host != account) {
        error = "Only the Sandbox host may start this lobby.";
        return false;
    }
    auto& state = found->second;
    if (state.launching) {
        error = "Sandbox lobby is already launching.";
        return false;
    }
    if (client::validateOnlineSandboxSessionConfig(state.config).has_value()) {
        error = "Sandbox lobby configuration is no longer valid.";
        return false;
    }
    if (state.humans.size() != state.config.humanPlayerCount) {
        error = "All reserved human slots must be occupied.";
        return false;
    }
    for (const auto& [slot, member] : state.humans) {
        if (slot != 1 && !state.ready.contains(member)) {
            error = "All joined hunters must be ready.";
            return false;
        }
    }
    state.launching = true;
    assignment = {code, state.config, {}, {}};
    for (const auto& [slot, member] : state.humans)
        assignment.humans.emplace(static_cast<PlayerId>(slot), member);
    for (std::size_t slot = state.config.humanPlayerCount + 1;
         slot <= state.config.hunterCount; ++slot)
        assignment.aiPlayers.push_back(static_cast<PlayerId>(slot));
    for (const auto& [slot, member] : state.humans) {
        (void)slot;
        sandboxMembership_.erase(member);
    }
    consumed_.insert(code);
    sandboxWaiting_.erase(found);
    error.clear();
    return true;
}

void LobbyCoordinator::disconnectSandbox(
    const AccountIdentity& account, std::vector<SandboxLobbyChange>& changes) {
    changes.clear();
    LobbyCode code;
    {
        std::lock_guard lock(mutex_);
        const auto member = sandboxMembership_.find(account);
        if (member == sandboxMembership_.end()) return;
        code = member->second;
    }
    SandboxLobbyChange change;
    std::string ignored;
    if (leaveSandbox(account, code, change, ignored)) changes.push_back(std::move(change));
}

} // namespace basilisk::game::server
