#pragma once

#include <memory>
#include <optional>
#include <string>

#include "ClientSessionController.hpp"
#include "NetworkProtocol.hpp"

namespace basilisk::game {

enum class NetworkConnectionState {
    Connecting,
    Connected,
    Disconnected,
    Error,
};

// Owns a platform WebSocket and adapts its binary v1 frames to the ordinary
// player-safe NetworkGameSessionAdapter.
class WebSocketNetworkSession {
public:
    [[nodiscard]] static std::unique_ptr<WebSocketNetworkSession> connect(
        std::string url,
        std::string token,
        std::string& error);
    [[nodiscard]] static std::unique_ptr<WebSocketNetworkSession>
    connectForAuthentication(std::string url, std::string& error);

    ~WebSocketNetworkSession();
    WebSocketNetworkSession(WebSocketNetworkSession&&) noexcept;
    WebSocketNetworkSession& operator=(WebSocketNetworkSession&&) noexcept;
    WebSocketNetworkSession(const WebSocketNetworkSession&) = delete;
    WebSocketNetworkSession& operator=(const WebSocketNetworkSession&) = delete;

    // Process frames on the application's main thread.
    void pump();
    // Idempotently close the transport and ignore any later callbacks.
    void close();
    [[nodiscard]] NetworkConnectionState state() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] ClientSessionController* controller() noexcept;
    [[nodiscard]] const ClientSessionController* controller() const noexcept;
    [[nodiscard]] bool requestLeaderboard(
        std::uint32_t offset,
        std::uint32_t limit);
    [[nodiscard]] const std::optional<network::LeaderboardPageResponse>&
    leaderboardPage() const noexcept;
    [[nodiscard]] bool authenticate(const network::AuthenticationRequest& request);
    [[nodiscard]] bool logout(const std::string& sessionToken);
    [[nodiscard]] bool requestLobby(const network::LobbyRequest& request);
    [[nodiscard]] const std::optional<network::AuthenticationResponse>&
    authenticationResponse() const noexcept;
    [[nodiscard]] const std::optional<network::LobbyResponse>&
    lobbyResponse() const noexcept;

private:
    class Impl;
    explicit WebSocketNetworkSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace basilisk::game
