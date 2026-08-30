#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/client/ai/AiDecisionEngine.hpp"
#include "basilisk/client/ai/AiKnowledgeState.hpp"

namespace basilisk::client::ai {

inline constexpr int kAiObservationSchemaVersion = 1;
inline constexpr int kAiActionSchemaVersion = 1;

struct PolicyKnowledgeFeatures {
    std::optional<CaveId> previousCave;
    bool pitWarning{};
    bool basiliskAdjacentWarning{};
    bool basiliskDistantWarning{};
    bool jackalWarning{};
    bool rivalWarning{};
    std::size_t basiliskCandidateCount{};
    std::size_t unresolvedPitCandidates{};
    std::size_t repeatedSearches{};
    std::uint64_t materialRevision{};
};

struct EncodedAction {
    // Index in PlayerRoundSnapshot::availableActions. PolicyDecision indexes
    // the filtered legalActions vector instead.
    std::size_t legalIndex{};
    AvailableAction action;
};

// Every field is derived from one player's safe snapshot and private memory.
struct PolicyObservation {
    int schemaVersion{kAiObservationSchemaVersion};
    PlayerRoundSnapshot sourceSnapshot;
    AiKnowledgeState knowledgeState;
    PolicyKnowledgeFeatures knowledge;
    std::vector<EncodedAction> legalActions;
    std::optional<EncodedAction> previousAction;
};

struct PolicyDecision {
    std::size_t legalActionIndex{};
    std::string policyMetadata;
};

class AgentPolicy {
public:
    virtual ~AgentPolicy() = default;
    [[nodiscard]] virtual PolicyDecision select(
        const PolicyObservation& observation, const AiConfig& config) = 0;
};

class HeuristicPolicy final : public AgentPolicy {
public:
    [[nodiscard]] PolicyDecision select(
        const PolicyObservation& observation, const AiConfig& config) override;
    [[nodiscard]] std::pair<PolicyDecision, AiDecisionEvaluation> evaluate(
        const PolicyObservation& observation, const AiConfig& config) const;
private:
    AiDecisionEngine engine_;
};

[[nodiscard]] PolicyObservation makePolicyObservation(
    const PlayerRoundSnapshot& snapshot,
    const AiKnowledgeState& knowledge,
    const AiConfig& config,
    const std::optional<EncodedAction>& previousAction = std::nullopt);

// The sole policy-output authority boundary. It rejects bad indices, altered
// action encodings, and actions forbidden by the shared difficulty safety gate.
[[nodiscard]] const EncodedAction& resolvePolicyDecision(
    const PolicyObservation& observation,
    const PolicyDecision& decision,
    const AiConfig& config);

[[nodiscard]] std::optional<AvailableAction> choosePolicyAction(
    AgentPolicy& policy,
    const PlayerRoundSnapshot& snapshot,
    const AiConfig& config,
    const AiKnowledgeState& knowledge,
    const std::optional<EncodedAction>& previousAction = std::nullopt);

} // namespace basilisk::client::ai
