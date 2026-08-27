#include "LobbyProtocolService.hpp"

#include <algorithm>
#include <type_traits>

namespace basilisk::game::server {

bool LobbyProtocolService::process(
    const AccountIdentity& account,
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
        const network::LobbyResponse response{network::kProtocolVersion,
            change.closed
                ? network::LobbyResponsePayload{network::SandboxLobbyClosed{
                    change.snapshot.lobby.value}}
                : network::LobbyResponsePayload{network::SandboxLobbyUpdated{
                    change.snapshot.lobby.value, change.snapshot.config,
                    change.snapshot.slots}}};
        for (const auto& recipient : change.recipients)
            if (!deliver(recipient, response)) return false;
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
            if (!coordinator_.hostSandbox(account, payload.config, change, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliverSandboxChange(change);
        } else if constexpr (std::is_same_v<T, network::JoinSandboxLobbyRequest>) {
            SandboxLobbyChange change;
            if (!coordinator_.joinSandbox(
                    account, LobbyCode{payload.lobbyCode}, change, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliverSandboxChange(change);
        } else {
            SandboxLobbyChange change;
            if (!coordinator_.leaveSandbox(
                    account, LobbyCode{payload.lobbyCode}, change, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliverSandboxChange(change);
        }
    }, request.payload);
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
            const network::LobbyResponse response{network::kProtocolVersion,
                change.closed
                    ? network::LobbyResponsePayload{network::SandboxLobbyClosed{
                        change.snapshot.lobby.value}}
                    : network::LobbyResponsePayload{network::SandboxLobbyUpdated{
                        change.snapshot.lobby.value, change.snapshot.config,
                        change.snapshot.slots}}};
            if (network::encodeWire(response, bytes, error))
                deliveries.push_back({recipient, std::move(bytes)});
        }
    }
}

} // namespace basilisk::game::server
