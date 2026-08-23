#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>

namespace basilisk::game {

struct Username {
    std::string value;

    auto operator<=>(const Username&) const = default;
};

struct PublicAccountProfile {
    Username username;

    bool operator==(const PublicAccountProfile&) const = default;
};

// Public read-model data contains no durable private account identity.
struct PublicTrophyLeaderboardEntry {
    std::size_t rank{};
    Username username;
    std::int64_t trophyTotal{};

    bool operator==(const PublicTrophyLeaderboardEntry&) const = default;
};

} // namespace basilisk::game
