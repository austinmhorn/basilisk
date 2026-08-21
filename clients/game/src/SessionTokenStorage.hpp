#pragma once

#include <optional>
#include <string>

namespace basilisk::game {

class SessionTokenStorage {
public:
    [[nodiscard]] static std::optional<std::string> load();
    [[nodiscard]] static bool save(const std::string& token, std::string& error);
    [[nodiscard]] static bool clear(std::string& error);
};

} // namespace basilisk::game
