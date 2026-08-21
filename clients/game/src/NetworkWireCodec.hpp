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
    SubmitAction = 16,
    LockAction = 17,
    WatchRemainingHunter = 18,
    Quit = 19,
};

[[nodiscard]] bool encodeWire(
    const ServerBootstrap& message,
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

} // namespace basilisk::game::network
