#pragma once

#include <string>
#include <string_view>

namespace basilisk::game {

struct NetworkEndpointConfig {
    std::string bindAddress{"127.0.0.1"};
    std::string connectUrl{"ws://127.0.0.1:8765"};
};

[[nodiscard]] bool applyNetworkEndpointOption(
    std::string_view option,
    std::string_view value,
    NetworkEndpointConfig& config,
    std::string& error);

} // namespace basilisk::game
