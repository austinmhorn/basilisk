#pragma once

#include <span>
#include <optional>
#include <string_view>
#include <vector>

#include "LobbyCoordinator.hpp"
#include "NetworkWireCodec.hpp"

namespace basilisk::game::server {

struct LobbyProtocolDelivery {
    AccountIdentity recipient;
    network::WireBytes bytes;
};

// The transport supplies the identity resolved from its authenticated session;
// no account identifier is accepted from or returned to the client.
class LobbyProtocolService {
public:
    explicit LobbyProtocolService(LobbyCoordinator& coordinator)
        : coordinator_(coordinator) {}

    [[nodiscard]] bool process(
        const AccountIdentity& authenticatedAccount,
        std::string_view authenticatedPublicName,
        std::span<const std::uint8_t> request,
        std::vector<LobbyProtocolDelivery>& deliveries,
        std::string& error);
    [[nodiscard]] bool process(
        const AccountIdentity& authenticatedAccount,
        std::span<const std::uint8_t> request,
        std::vector<LobbyProtocolDelivery>& deliveries,
        std::string& error) {
        return process(authenticatedAccount, {}, request, deliveries, error);
    }
    void disconnect(
        const AccountIdentity& authenticatedAccount,
        std::vector<LobbyProtocolDelivery>& deliveries);
    [[nodiscard]] std::optional<SandboxMatchAssignment> takeSandboxLaunch();

private:
    LobbyCoordinator& coordinator_;
    std::optional<SandboxMatchAssignment> sandboxLaunch_;
};

} // namespace basilisk::game::server
