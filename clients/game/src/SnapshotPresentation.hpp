#pragma once

#include <cstddef>
#include <optional>
#include <span>
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

struct RoundReportLayout {
    std::vector<float> rowHeights;
    float panelHeight{0.0F};
};

[[nodiscard]] std::optional<SecondaryObjectivePresentation>
secondaryObjectivePresentation(const PlayerRoundSnapshot& snapshot);

[[nodiscard]] std::vector<std::string> roundReportText(
    const PlayerRoundSnapshot& snapshot);

[[nodiscard]] RoundReportLayout roundReportLayout(
    std::span<const std::size_t> wrappedLineCounts,
    float scale);

} // namespace basilisk::game
