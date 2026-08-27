#include <cassert>
#include <string>
#include <vector>

#include "LobbyCoordinator.hpp"
#include "LobbyProtocolService.hpp"

using namespace basilisk::game::server;
namespace network = basilisk::game::network;

int main() {
    std::vector<std::string> codes{"CAVE7X", "HUNT34", "PIT789"};
    std::size_t nextCode = 0;
    LobbyCoordinator lobbies{[&] { return codes.at(nextCode++); }};
    const AccountIdentity host{"account-host"};
    const AccountIdentity guest{"account-guest"};
    std::string error;

    LobbyCode code;
    assert(lobbies.host(host, code, error));
    assert(code.value == "CAVE7X");

    LobbyMatchAssignment assignment;
    assert(!lobbies.join(host, code, assignment, error));
    assert(error == "A player cannot join their own lobby.");
    assert(lobbies.join(guest, code, assignment, error));
    assert(assignment.lobby == code);
    assert(assignment.host == host);
    assert(assignment.guest == guest);
    assert(!lobbies.join(guest, code, assignment, error));

    LobbyCode cancelled;
    assert(lobbies.host(host, cancelled, error));
    assert(lobbies.cancel(host, cancelled, error));
    assert(!lobbies.join(guest, cancelled, assignment, error));
    assert(!lobbies.cancel(host, cancelled, error));

    assert(!lobbies.join(guest, LobbyCode{"NOPE00"}, assignment, error));

    LobbyCoordinator protocolLobbies{[] { return std::string{"NET123"}; }};
    LobbyProtocolService protocol{protocolLobbies};
    network::WireBytes request;
    std::vector<LobbyProtocolDelivery> deliveries;
    assert(network::encodeWire(network::LobbyRequest{
        network::kProtocolVersion, network::HostLobbyRequest{}}, request, error));
    assert(protocol.process(host, request, deliveries, error));
    assert(deliveries.size() == 1 && deliveries.front().recipient == host);
    network::LobbyResponse response;
    assert(network::decodeLobbyResponse(deliveries.front().bytes, response, error));
    assert(std::get<network::LobbyHosted>(response.payload).lobbyCode == "NET123");

    assert(network::encodeWire(network::LobbyRequest{
        network::kProtocolVersion, network::JoinLobbyRequest{"NET123"}},
        request, error));
    assert(protocol.process(guest, request, deliveries, error));
    assert(deliveries.size() == 2);
    assert(deliveries[0].recipient == host);
    assert(deliveries[1].recipient == guest);
    assert(network::decodeLobbyResponse(deliveries[0].bytes, response, error));
    assert(std::get<network::LobbyMatchAssigned>(response.payload).role ==
           network::LobbyAssignmentRole::Host);
    assert(network::decodeLobbyResponse(deliveries[1].bytes, response, error));
    assert(std::get<network::LobbyMatchAssigned>(response.payload).role ==
           network::LobbyAssignmentRole::Guest);

    assert(network::encodeWire(network::LobbyRequest{
        network::kProtocolVersion, network::JoinLobbyRequest{"INVALID"}},
        request, error));
    assert(protocol.process(guest, request, deliveries, error));
    assert(deliveries.size() == 1);
    assert(network::decodeLobbyResponse(deliveries[0].bytes, response, error));
    assert(std::holds_alternative<network::LobbyFailure>(response.payload));

    LobbyCoordinator matchmaking{[] { return std::string{"FIFO12"}; }};
    LobbyProtocolService matchmakingProtocol{matchmaking};
    assert(network::encodeWire(network::LobbyRequest{
        network::kProtocolVersion, network::FindMatchRequest{}}, request, error));
    assert(matchmakingProtocol.process(host, request, deliveries, error));
    assert(deliveries.size() == 1);
    assert(network::decodeLobbyResponse(deliveries[0].bytes, response, error));
    assert(std::holds_alternative<network::MatchmakingQueued>(response.payload));
    assert(matchmakingProtocol.process(guest, request, deliveries, error));
    assert(deliveries.size() == 2);
    assert(deliveries[0].recipient == host && deliveries[1].recipient == guest);
    assert(network::decodeLobbyResponse(deliveries[0].bytes, response, error));
    assert(std::get<network::LobbyMatchAssigned>(response.payload).role ==
           network::LobbyAssignmentRole::Host);
    assert(network::decodeLobbyResponse(deliveries[1].bytes, response, error));
    assert(std::get<network::LobbyMatchAssigned>(response.payload).role ==
           network::LobbyAssignmentRole::Guest);

    const AccountIdentity third{"account-third"};
    const AccountIdentity fourth{"account-fourth"};
    const AccountIdentity fifth{"account-fifth"};
    auto sandboxConfig = basilisk::client::defaultSandboxSessionConfig(4);
    sandboxConfig.humanPlayerCount = 3;
    assert(network::encodeWire(network::LobbyRequest{
        network::kProtocolVersion,
        network::HostSandboxLobbyRequest{sandboxConfig}}, request, error));
    assert(protocol.process(host, request, deliveries, error));
    assert(deliveries.size() == 1);
    assert(network::decodeLobbyResponse(deliveries[0].bytes, response, error));
    const auto hostedSandbox =
        std::get<network::SandboxLobbyUpdated>(response.payload);
    assert(hostedSandbox.lobbyCode == "SBX-NET123");
    assert(hostedSandbox.slots.size() == 4);
    assert(hostedSandbox.slots[0].occupied);
    assert(!hostedSandbox.slots[1].occupied);
    assert(hostedSandbox.slots[3].kind ==
        basilisk::client::SandboxLobbySlotKind::Ai);

    // Standard and Sandbox code namespaces are enforced by request type.
    assert(network::encodeWire(network::LobbyRequest{
        network::kProtocolVersion,
        network::JoinLobbyRequest{hostedSandbox.lobbyCode}}, request, error));
    assert(protocol.process(third, request, deliveries, error));
    assert(network::decodeLobbyResponse(deliveries[0].bytes, response, error));
    assert(std::holds_alternative<network::LobbyFailure>(response.payload));
    assert(network::encodeWire(network::LobbyRequest{
        network::kProtocolVersion,
        network::JoinSandboxLobbyRequest{"NET123"}}, request, error));
    assert(protocol.process(third, request, deliveries, error));
    assert(network::decodeLobbyResponse(deliveries[0].bytes, response, error));
    assert(std::holds_alternative<network::LobbyFailure>(response.payload));

    assert(network::encodeWire(network::LobbyRequest{
        network::kProtocolVersion,
        network::JoinSandboxLobbyRequest{hostedSandbox.lobbyCode}}, request, error));
    assert(protocol.process(third, request, deliveries, error));
    assert(deliveries.size() == 2);
    assert(protocol.process(fourth, request, deliveries, error));
    assert(deliveries.size() == 3);
    assert(protocol.process(fifth, request, deliveries, error));
    assert(deliveries.size() == 1);
    assert(network::decodeLobbyResponse(deliveries[0].bytes, response, error));
    assert(std::holds_alternative<network::LobbyFailure>(response.payload));

    std::vector<LobbyProtocolDelivery> disconnectDeliveries;
    protocol.disconnect(third, disconnectDeliveries);
    assert(disconnectDeliveries.size() == 2);
    assert(network::decodeLobbyResponse(
        disconnectDeliveries[0].bytes, response, error));
    assert(!std::get<network::SandboxLobbyUpdated>(response.payload)
        .slots[1].occupied);
    protocol.disconnect(host, disconnectDeliveries);
    assert(!disconnectDeliveries.empty());
    assert(network::decodeLobbyResponse(
        disconnectDeliveries[0].bytes, response, error));
    assert(std::holds_alternative<network::SandboxLobbyClosed>(response.payload));
}
