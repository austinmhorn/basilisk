#include "NetworkEndpointConfig.hpp"

#include <charconv>
#include <system_error>

namespace basilisk::game {

bool applyNetworkEndpointOption(
    std::string_view option,
    std::string_view value,
    NetworkEndpointConfig& config,
    std::string& error) {
    if (value.empty()) {
        error = std::string{option} + " requires a non-empty value.";
        return false;
    }
    if (option == "--bind") {
        config.bindAddress = value;
    } else if (option == "--port") {
        std::uint64_t port{};
        const auto result = std::from_chars(
            value.data(), value.data() + value.size(), port);
        if (result.ec != std::errc{} ||
            result.ptr != value.data() + value.size() ||
            port == 0 || port > 65535) {
            error = "--port requires an integer from 1 through 65535.";
            return false;
        }
        config.serverPort = static_cast<std::uint16_t>(port);
    } else if (option == "--connect") {
        config.connectUrl = value;
    } else {
        error = "Unknown network endpoint option.";
        return false;
    }
    error.clear();
    return true;
}

} // namespace basilisk::game
