#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "basilisk/Types.hpp"
#include "basilisk/client/PlayerProfile.hpp"

namespace basilisk::game::server {

struct LocalWebSocketServerConfig {
    std::uint16_t port{8765};
    std::string p1Token;
    std::string p2Token;
    MapSeed mapSeed{20260816};
    MatchSeed matchSeed{424242};
    std::vector<client::PublicPlayerProfile> profiles;
};

// Native-only localhost WebSocket shell around one authoritative match.
class LocalWebSocketMatchServer {
public:
    [[nodiscard]] static std::unique_ptr<LocalWebSocketMatchServer> start(
        LocalWebSocketServerConfig config,
        std::string& error);
    ~LocalWebSocketMatchServer();
    LocalWebSocketMatchServer(const LocalWebSocketMatchServer&) = delete;
    LocalWebSocketMatchServer& operator=(const LocalWebSocketMatchServer&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::size_t connectedClientCount() const noexcept;
    [[nodiscard]] std::size_t processedDisconnectCount() const noexcept;
    [[nodiscard]] RoundNumber authoritativeRound() const noexcept;
    [[nodiscard]] std::size_t resolvedRoundCount() const noexcept;
    void advanceTime(std::uint64_t elapsedMs);
    void stop();

private:
    class Impl;
    explicit LocalWebSocketMatchServer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace basilisk::game::server
