#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "NetworkProtocol.hpp"
#include "NetworkWireCodec.hpp"
#include "PublicAccountProfiles.hpp"
#include "TrophyScoring.hpp"
#include "basilisk/client/MatchMode.hpp"
#include "basilisk/Types.hpp"
#include "basilisk/client/PlayerProfile.hpp"
#include "basilisk/client/SandboxConfiguration.hpp"
#include "basilisk/client/ai/RuntimeAiPolicy.hpp"

namespace basilisk::game::server {

class AuthoritativeInMemoryMatchState;

// An authenticated, byte-oriented endpoint bound permanently to one player.
// It is also the typed transport consumed by NetworkGameSessionAdapter.
class InMemoryMatchEndpoint final : public network::ClientTransport {
public:
    [[nodiscard]] bool send(const network::ClientCommand& command) override;
    [[nodiscard]] bool sendBytes(
        std::span<const std::uint8_t> bytes,
        std::string& error);

    [[nodiscard]] PlayerId authenticatedPlayer() const noexcept;
    [[nodiscard]] std::optional<network::WireBytes> takeNextServerFrame();

    // Trusted transport lifecycle hook. This is intentionally separate from
    // the player's explicit QuitCommand/forfeit.
    void disconnect();

private:
    friend class AuthoritativeInMemoryMatch;
    friend class AuthoritativeInMemoryMatchState;

    InMemoryMatchEndpoint(
        std::shared_ptr<AuthoritativeInMemoryMatchState> state,
        PlayerId authenticatedPlayer);
    void enqueue(network::WireBytes frame);

    std::shared_ptr<AuthoritativeInMemoryMatchState> state_;
    PlayerId authenticatedPlayer_{};
    std::vector<network::WireBytes> serverFrames_;
};

// Trusted host-side session. MatchState and authoritative events are confined
// to its private implementation and never cross an endpoint.
class AuthoritativeInMemoryMatch {
public:
    [[nodiscard]] static std::unique_ptr<AuthoritativeInMemoryMatch> create(
        MapSeed mapSeed,
        MatchSeed matchSeed,
        std::vector<client::PublicPlayerProfile> profiles,
        std::string& error,
        std::optional<TrophyScoringContext> trophyScoring = std::nullopt,
        std::shared_ptr<PublicTrophyReadModel> leaderboard = nullptr,
        client::MatchMode mode = client::MatchMode::Online);
    [[nodiscard]] static std::unique_ptr<AuthoritativeInMemoryMatch> createSandbox(
        const client::SandboxSessionConfig& config,
        std::vector<client::PublicPlayerProfile> profiles,
        std::vector<client::ai::AiConfig> aiPlayers,
        std::string& error,
        client::ai::RuntimeAiPolicyConfig policy = {});

    [[nodiscard]] std::shared_ptr<InMemoryMatchEndpoint> connect(
        PlayerId authenticatedPlayer,
        std::string& error);
    [[nodiscard]] std::shared_ptr<InMemoryMatchEndpoint> reconnect(
        PlayerId authenticatedPlayer,
        std::string& error);

    // Trusted diagnostics/time input for tests and a future server loop.
    [[nodiscard]] RoundNumber authoritativeRound() const noexcept;
    [[nodiscard]] std::size_t resolvedRoundCount() const noexcept;
    [[nodiscard]] std::optional<std::string> trophyScoringError() const;
    [[nodiscard]] std::uint64_t disconnectGraceMs() const noexcept;
    void advanceTime(std::uint64_t elapsedMs);

private:
    explicit AuthoritativeInMemoryMatch(
        std::shared_ptr<AuthoritativeInMemoryMatchState> state);

    std::shared_ptr<AuthoritativeInMemoryMatchState> state_;
};

} // namespace basilisk::game::server
