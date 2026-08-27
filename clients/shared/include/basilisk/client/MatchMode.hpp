#pragma once

namespace basilisk::client {

enum class MatchMode {
    Online,
    AI,
    Sandbox,
};

[[nodiscard]] constexpr bool trophyEligible(MatchMode mode) noexcept {
    return mode == MatchMode::Online;
}

} // namespace basilisk::client
