#pragma once

#include <optional>

#include "basilisk/Types.hpp"

namespace basilisk {

enum class MatchStatus {
    Active,
    Completed
};

enum class MatchOutcome {
    None,
    BasiliskKilled,
    EscapedWithSigil,
    Draw
};

struct MatchResult {
    MatchStatus status{MatchStatus::Active};
    MatchOutcome outcome{MatchOutcome::None};
    std::optional<PlayerId> winner;
};

} // namespace basilisk
