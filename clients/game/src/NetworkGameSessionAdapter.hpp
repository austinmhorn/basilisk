#pragma once

#include <memory>
#include <string>

#include "ClientSessionController.hpp"
#include "NetworkProtocol.hpp"

namespace basilisk::game {

// Owns the ordinary client controller for a remote, player-safe session.
// Authoritative state and transport serialization remain outside this layer.
class NetworkGameSessionAdapter {
public:
    [[nodiscard]] static std::unique_ptr<NetworkGameSessionAdapter> create(
        network::ServerBootstrap bootstrap,
        std::shared_ptr<network::ClientTransport> transport,
        std::string& error);

    [[nodiscard]] ClientSessionController& controller() noexcept;
    [[nodiscard]] const ClientSessionController& controller() const noexcept;

    [[nodiscard]] bool ingest(
        network::ServerUpdate update,
        std::string& error);

private:
    explicit NetworkGameSessionAdapter(
        std::unique_ptr<ClientSessionController> controller);

    std::unique_ptr<ClientSessionController> controller_;
};

} // namespace basilisk::game
