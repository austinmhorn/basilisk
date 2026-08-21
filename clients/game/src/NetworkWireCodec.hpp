#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "NetworkProtocol.hpp"

namespace basilisk::game::network {

using WireBytes = std::vector<std::uint8_t>;

enum class WireMessageType : std::uint8_t {
    ServerBootstrap = 1,
    ServerUpdate = 2,
    LeaderboardPageResponse = 3,
    AuthenticationSuccess = 4,
    AuthenticationFailure = 5,
    LogoutSuccess = 6,
    SubmitAction = 16,
    LockAction = 17,
    WatchRemainingHunter = 18,
    Quit = 19,
    LeaderboardPageRequest = 20,
    CreateAccount = 32,
    Login = 33,
    AuthenticateSession = 34,
    LogoutSession = 35,
};

[[nodiscard]] bool inspectWireMessageType(
    std::span<const std::uint8_t> bytes,
    WireMessageType& type,
    std::string& error);

[[nodiscard]] bool encodeWire(
    const ServerBootstrap& message,
    WireBytes& bytes,
    std::string& error);
[[nodiscard]] bool encodeWire(
    const AuthenticationRequest& message,
    WireBytes& bytes,
    std::string& error);
[[nodiscard]] bool encodeWire(
    const AuthenticationResponse& message,
    WireBytes& bytes,
    std::string& error);
[[nodiscard]] bool encodeWire(
    const ServerUpdate& message,
    WireBytes& bytes,
    std::string& error);
[[nodiscard]] bool encodeWire(
    const ClientCommand& message,
    WireBytes& bytes,
    std::string& error);
[[nodiscard]] bool encodeWire(
    const LeaderboardPageResponse& message,
    WireBytes& bytes,
    std::string& error);

[[nodiscard]] bool decodeServerBootstrap(
    std::span<const std::uint8_t> bytes,
    ServerBootstrap& message,
    std::string& error);
[[nodiscard]] bool decodeServerUpdate(
    std::span<const std::uint8_t> bytes,
    ServerUpdate& message,
    std::string& error);
[[nodiscard]] bool decodeClientCommand(
    std::span<const std::uint8_t> bytes,
    ClientCommand& message,
    std::string& error);
[[nodiscard]] bool decodeLeaderboardPageResponse(
    std::span<const std::uint8_t> bytes,
    LeaderboardPageResponse& message,
    std::string& error);
[[nodiscard]] bool decodeAuthenticationRequest(
    std::span<const std::uint8_t> bytes,
    AuthenticationRequest& message,
    std::string& error);
[[nodiscard]] bool decodeAuthenticationResponse(
    std::span<const std::uint8_t> bytes,
    AuthenticationResponse& message,
    std::string& error);

} // namespace basilisk::game::network
