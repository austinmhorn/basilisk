#pragma once

#include <optional>
#include <string>
#include <vector>

#include "basilisk/ClientSnapshot.hpp"

namespace basilisk::game {

enum class SecondaryObjectiveKind {
    Recoverable,
    Secured
};

struct SecondaryObjectivePresentation {
    SecondaryObjectiveKind kind{SecondaryObjectiveKind::Recoverable};
    std::string title;
    std::string status;
    std::vector<std::string> bodyLines;
    std::optional<std::string> detail;
};

[[nodiscard]] std::optional<SecondaryObjectivePresentation>
secondaryObjectivePresentation(const PlayerRoundSnapshot& snapshot);

[[nodiscard]] std::vector<std::string> roundReportText(
    const PlayerRoundSnapshot& snapshot);

} // namespace basilisk::game
