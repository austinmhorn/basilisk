#include "LobbyProtocolService.hpp"

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
        } else {
            if (!coordinator_.cancel(
                    account, LobbyCode{payload.lobbyCode}, error))
                return deliver(account, {network::kProtocolVersion,
                    network::LobbyFailure{error}});
            return deliver(account, {network::kProtocolVersion,
                network::LobbyCancelled{payload.lobbyCode}});
        }
    }, request.payload);
}

} // namespace basilisk::game::server
