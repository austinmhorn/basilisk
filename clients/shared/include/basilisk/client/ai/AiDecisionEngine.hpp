#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/client/ai/AiKnowledgeState.hpp"

namespace basilisk::client::ai {

using AiSeed = std::uint64_t;

enum class AiDifficulty { Easy, Medium, Hard };
enum class AiBehavior {
    Balanced,
    Explorer,
    Aggressive,
    ObjectiveFocused,
    Survivalist,
    Opportunist,
    Random,
};

struct AiConfig {
    AiDifficulty difficulty{AiDifficulty::Medium};
    AiBehavior behavior{AiBehavior::Balanced};
    PlayerId player{};
    AiSeed seed{};
};

struct AiActionUtility {
    AvailableAction action;
    double utility{0.0};
};

struct AiDecisionEvaluation {
    std::vector<AiActionUtility> actions;
    std::size_t chosenIndex{0};
    std::size_t basiliskCandidates{0};
    bool basiliskAdjacentEvidence{false};
    bool basiliskDistantEvidence{false};
    bool sigilRecoverable{false};
};

[[nodiscard]] AiBehavior resolveBehavior(AiBehavior requested, AiSeed seed);

class AiDecisionEngine {
public:
    [[nodiscard]] std::optional<AvailableAction> choose(
        const PlayerRoundSnapshot& snapshot,
        const AiConfig& config) const;
    [[nodiscard]] std::optional<AvailableAction> choose(
        const PlayerRoundSnapshot& snapshot,
        const AiConfig& config,
        const AiKnowledgeState& knowledge) const;
    [[nodiscard]] AiDecisionEvaluation evaluate(
        const PlayerRoundSnapshot& snapshot,
        const AiConfig& config,
        const AiKnowledgeState& knowledge) const;
};

[[nodiscard]] const char* difficultyName(AiDifficulty difficulty) noexcept;
[[nodiscard]] const char* behaviorName(AiBehavior behavior) noexcept;

} // namespace basilisk::client::ai
