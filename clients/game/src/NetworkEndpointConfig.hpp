#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace basilisk::game {

inline constexpr std::string_view kLocalWebSocketEndpoint{
    "ws://127.0.0.1:8765"};
inline constexpr std::string_view kProductionWebSocketEndpoint{
    "wss://game.xiivestudio.com"};

enum class ClientEndpointDefault {
    LocalDevelopment,
    Production,
};

struct NetworkEndpointConfig {
    std::string bindAddress{"127.0.0.1"};
    std::uint16_t serverPort{8765};
    std::string connectUrl{kLocalWebSocketEndpoint};
};

[[nodiscard]] NetworkEndpointConfig clientNetworkEndpointConfig(
    ClientEndpointDefault endpointDefault);

[[nodiscard]] bool applyNetworkEndpointOption(
    std::string_view option,
    std::string_view value,
    NetworkEndpointConfig& config,
    std::string& error);

} // namespace basilisk::game
