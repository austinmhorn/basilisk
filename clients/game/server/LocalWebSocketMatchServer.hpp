#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "TrophyScoring.hpp"
#include "PublicAccountProfiles.hpp"
#include "AccountAuth.hpp"
#include "basilisk/Types.hpp"
#include "basilisk/client/PlayerProfile.hpp"

namespace basilisk::game::server {

struct LocalServerTrophyConfig {
    TrophyMatchId match;
    AccountIdentity p1Account;
    AccountIdentity p2Account;

    // Empty selects the existing in-memory persistence for tests/development.
    std::string sqliteDatabasePath;
    std::optional<PublicAccountProfile> p1PublicProfile;
    std::optional<PublicAccountProfile> p2PublicProfile;
};

struct LocalWebSocketServerConfig {
    std::uint16_t port{8765};
    std::string bindAddress{"127.0.0.1"};
    std::string p1Token;
    std::string p2Token;
    MapSeed mapSeed{20260816};
    MatchSeed matchSeed{424242};
    std::vector<client::PublicPlayerProfile> profiles;
    // Server-wide persistence used by dynamically assigned authenticated
    // matches and the public trophy read model.
    std::string trophyDatabasePath;
    std::optional<LocalServerTrophyConfig> trophies;
    // When present, authentication is performed with binary protocol messages.
    // The fixed URL-token fields remain the development fallback.
    std::shared_ptr<SQLiteAccountAuth> authentication;
    std::optional<AccountIdentity> p1AuthenticatedAccount;
    std::optional<AccountIdentity> p2AuthenticatedAccount;
};

// Native-only localhost WebSocket shell for fixed development and assigned
// authenticated authoritative matches.
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
    [[nodiscard]] bool trophyTotal(
        const AccountIdentity& account,
        std::int64_t& total,
        std::string& error) const;
    [[nodiscard]] bool leaderboard(
        std::vector<TrophyLeaderboardEntry>& entries,
        std::string& error) const;
    [[nodiscard]] std::optional<std::string> trophyScoringError() const;
    void advanceTime(std::uint64_t elapsedMs);
    void stop();

private:
    class Impl;
    explicit LocalWebSocketMatchServer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace basilisk::game::server
