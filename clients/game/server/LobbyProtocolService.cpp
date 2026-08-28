#include "LobbyProtocolService.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace basilisk::game::server {

bool LobbyProtocolService::process(
    const AccountIdentity& account,
    std::string_view publicName,
    std::span<const std::uint8_t> requestBytes,
    std::vector<LobbyProtocolDelivery>& deliveries,
    std::string& error) {
    network::LobbyRequest request;
    if (!network::decodeLobbyRequest(requestBytes, request, error)) return false;
    deliveries.clear();
    const auto deliver = [&](const AccountIdentity& recipient,
                             network::LobbyResponse response) {
        network::WireBytes bytes;
        if (!network::encodeWire(response, bytes, error)) return false;
        deliveries.push_back({recipient, std::move(bytes)});
        return true;
    };
    const auto deliverSandboxChange = [&](const SandboxLobbyChange& change) {
        for (const auto& recipient : change.recipients) {
            const auto member = change.memberPlayers.find(recipient);
            const network::LobbyResponse response{network::kProtocolVersion,
                change.closed
                    ? network::LobbyResponsePayload{network::SandboxLobbyClosed{
                        change.snapshot.lobby.value}}
                    : network::LobbyResponsePayload{network::SandboxLobbyUpdated{
                        change.snapshot.lobby.value, change.snapshot.config,
                        change.snapshot.slots,
                        member == change.memberPlayers.end()
                            ? PlayerId{} : member->second}}};
            if (!deliver(recipient, response)) return false;
        }
        for (const auto& removed : change.removed) {
            if (std::find(change.recipients.begin(), change.recipients.end(), removed) !=
                change.recipients.end()) continue;
            if (!deliver(removed, {network::kProtocolVersion,
                    network::SandboxLobbyClosed{change.snapshot.lobby.value}}))
                return false;
        }
        return true;
    };
    return std::visit([&](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, network::HostLobbyRequest>) {
            LobbyCode code;
            if (!coordinator_.host(account, code, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliver(account, {network::kProtocolVersion,
                network::LobbyHosted{std::move(code.value)}});
        } else if constexpr (std::is_same_v<T, network::JoinLobbyRequest>) {
            LobbyMatchAssignment assignment;
            if (!coordinator_.join(
                    account, LobbyCode{payload.lobbyCode}, assignment, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliver(assignment.host, {network::kProtocolVersion,
                       network::LobbyMatchAssigned{
                           assignment.lobby.value,
                           network::LobbyAssignmentRole::Host}}) &&
                   deliver(assignment.guest, {network::kProtocolVersion,
                       network::LobbyMatchAssigned{
                           assignment.lobby.value,
                           network::LobbyAssignmentRole::Guest}});
        } else if constexpr (
            std::is_same_v<T, network::CancelHostedLobbyRequest>) {
            if (!coordinator_.cancel(
                    account, LobbyCode{payload.lobbyCode}, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliver(account, {network::kProtocolVersion,
                network::LobbyCancelled{payload.lobbyCode}});
        } else if constexpr (std::is_same_v<T, network::FindMatchRequest>) {
            std::optional<LobbyMatchAssignment> assignment;
            if (!coordinator_.findMatch(account, assignment, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            if (!assignment.has_value())
                return deliver(account, {network::kProtocolVersion,
                    network::MatchmakingQueued{}});
            return deliver(assignment->host, {network::kProtocolVersion,
                       network::LobbyMatchAssigned{
                           assignment->lobby.value,
                           network::LobbyAssignmentRole::Host}}) &&
                   deliver(assignment->guest, {network::kProtocolVersion,
                       network::LobbyMatchAssigned{
                           assignment->lobby.value,
                           network::LobbyAssignmentRole::Guest}});
        } else if constexpr (std::is_same_v<T, network::CancelFindMatchRequest>) {
            if (!coordinator_.cancelFindMatch(account, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliver(account, {network::kProtocolVersion,
                network::MatchmakingCancelled{}});
        } else if constexpr (std::is_same_v<T, network::HostSandboxLobbyRequest>) {
            SandboxLobbyChange change;
            if (!coordinator_.hostSandbox(
                    account, std::string{publicName}, payload.config, change, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliverSandboxChange(change);
        } else if constexpr (std::is_same_v<T, network::JoinSandboxLobbyRequest>) {
            SandboxLobbyChange change;
            if (!coordinator_.joinSandbox(account, std::string{publicName},
                    LobbyCode{payload.lobbyCode}, change, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliverSandboxChange(change);
        } else if constexpr (std::is_same_v<T, network::LeaveSandboxLobbyRequest>) {
            SandboxLobbyChange change;
            if (!coordinator_.leaveSandbox(
                    account, LobbyCode{payload.lobbyCode}, change, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliverSandboxChange(change);
        } else if constexpr (std::is_same_v<T, network::SetSandboxReadyRequest>) {
            SandboxLobbyChange change;
            if (!coordinator_.setSandboxReady(account,
                    LobbyCode{payload.lobbyCode}, payload.ready, change, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliverSandboxChange(change);
        } else {
            SandboxMatchAssignment assignment;
            if (!coordinator_.startSandbox(account,
                    LobbyCode{payload.lobbyCode}, assignment, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            sandboxLaunch_ = std::move(assignment);
            deliveries.clear();
            return true;
        }
    }, request.payload);
}

std::optional<SandboxMatchAssignment> LobbyProtocolService::takeSandboxLaunch() {
    return std::exchange(sandboxLaunch_, std::nullopt);
}

void LobbyProtocolService::disconnect(
    const AccountIdentity& account,
    std::vector<LobbyProtocolDelivery>& deliveries) {
    std::vector<SandboxLobbyChange> changes;
    coordinator_.disconnectSandbox(account, changes);
    deliveries.clear();
    std::string error;
    for (const auto& change : changes) {
        for (const auto& recipient : change.recipients) {
            network::WireBytes bytes;
            const auto member = change.memberPlayers.find(recipient);
            const network::LobbyResponse response{network::kProtocolVersion,
                change.closed
                    ? network::LobbyResponsePayload{network::SandboxLobbyClosed{
                        change.snapshot.lobby.value}}
                    : network::LobbyResponsePayload{network::SandboxLobbyUpdated{
                        change.snapshot.lobby.value, change.snapshot.config,
                        change.snapshot.slots,
                        member == change.memberPlayers.end()
                            ? PlayerId{} : member->second}}};
            if (network::encodeWire(response, bytes, error))
                deliveries.push_back({recipient, std::move(bytes)});
        }
    }
}

} // namespace basilisk::game::server
