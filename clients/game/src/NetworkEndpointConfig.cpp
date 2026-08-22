#include "NetworkEndpointConfig.hpp"

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
