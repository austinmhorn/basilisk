#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>

namespace basilisk::game {

struct PublicProfileHandle {
    std::string value;

    auto operator<=>(const PublicProfileHandle&) const = default;
};

// Public read-model data contains no durable private account identity.
struct PublicTrophyLeaderboardEntry {
    std::size_t rank{};
    PublicProfileHandle handle;
    std::string displayName;
    std::int64_t trophyTotal{};

    bool operator==(const PublicTrophyLeaderboardEntry&) const = default;
};

} // namespace basilisk::game
