#pragma once

#include <span>
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
        std::span<const std::uint8_t> request,
        std::vector<LobbyProtocolDelivery>& deliveries,
        std::string& error);

private:
    LobbyCoordinator& coordinator_;
};

} // namespace basilisk::game::server
