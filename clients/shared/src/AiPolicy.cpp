#include "basilisk/client/ai/AiPolicy.hpp"

#include <algorithm>
#include <stdexcept>

namespace basilisk::client::ai {
namespace {

bool sameAction(const AvailableAction& left, const AvailableAction& right) {
    return left.type == right.type && left.targetCave == right.targetCave &&
        left.targetTunnel == right.targetTunnel &&
        left.targetItem == right.targetItem &&
        left.contextualAction == right.contextualAction;
}

PolicyKnowledgeFeatures knowledgeFeatures(
    const PlayerRoundSnapshot& snapshot, const AiKnowledgeState& knowledge) {
    PolicyKnowledgeFeatures result;
    result.previousCave = knowledge.previousCave();
    result.pitWarning = knowledge.pitWarningHere();
    result.basiliskAdjacentWarning = knowledge.basiliskWarningHere();
    result.basiliskDistantWarning = knowledge.basiliskDistantWarningHere();
    result.jackalWarning = knowledge.jackalWarningHere();
    result.rivalWarning = knowledge.rivalWarningHere();
    result.unresolvedPitCandidates = knowledge.unresolvedPitCandidateCount();
    result.repeatedSearches = knowledge.repeatedSearchCount();
    result.materialRevision = knowledge.materialRevision();
    for (const auto& action : snapshot.availableActions) {
        if (action.type == ActionType::Shoot &&
            !knowledge.isDisprovenBasiliskTarget(snapshot.currentCave, action))
            ++result.basiliskCandidateCount;
    }
    return result;
}

PolicyDecision decisionFor(
    const PolicyObservation& observation,
    const AiDecisionEvaluation& evaluation) {
    if (evaluation.actions.empty())
        return {observation.legalActions.size(), "no-action"};
    const AvailableAction& selected = evaluation.actions[evaluation.chosenIndex].action;
    const auto found = std::find_if(observation.legalActions.begin(),
        observation.legalActions.end(), [&](const EncodedAction& legal) {
            return sameAction(legal.action, selected);
        });
    return {found == observation.legalActions.end()
        ? observation.legalActions.size()
        : static_cast<std::size_t>(
            std::distance(observation.legalActions.begin(), found)),
        "heuristic"};
}

} // namespace

std::pair<PolicyDecision, AiDecisionEvaluation> HeuristicPolicy::evaluate(
    const PolicyObservation& observation, const AiConfig& config) const {
    AiDecisionEvaluation evaluation = engine_.evaluate(
        observation.sourceSnapshot, config, observation.knowledgeState);
    return {decisionFor(observation, evaluation), std::move(evaluation)};
}

PolicyDecision HeuristicPolicy::select(
    const PolicyObservation& observation, const AiConfig& config) {
    return evaluate(observation, config).first;
}

PolicyObservation makePolicyObservation(
    const PlayerRoundSnapshot& snapshot,
    const AiKnowledgeState& knowledge,
    const AiConfig& config,
    const std::optional<EncodedAction>& previousAction) {
    PolicyObservation result;
    result.sourceSnapshot = snapshot;
    result.knowledgeState = knowledge;
    result.knowledge = knowledgeFeatures(snapshot, knowledge);
    result.previousAction = previousAction;
    result.legalActions.reserve(snapshot.availableActions.size());
    for (std::size_t index = 0; index < snapshot.availableActions.size(); ++index) {
        const AvailableAction& action = snapshot.availableActions[index];
        if (!passesAiSafetyFilter(snapshot, action, config, knowledge)) continue;
        result.legalActions.push_back({index, action});
    }
    return result;
}

const EncodedAction& resolvePolicyDecision(
    const PolicyObservation& observation,
    const PolicyDecision& decision,
    const AiConfig& config) {
    if (decision.legalActionIndex >= observation.legalActions.size())
        throw std::runtime_error("AI policy selected an illegal action index");
    const EncodedAction& selected = observation.legalActions[decision.legalActionIndex];
    if (selected.legalIndex >= observation.sourceSnapshot.availableActions.size() ||
        !sameAction(selected.action,
            observation.sourceSnapshot.availableActions[selected.legalIndex]))
        throw std::runtime_error(
            "AI legal-action encoding does not match the player-safe action set");
    if (!passesAiSafetyFilter(observation.sourceSnapshot, selected.action,
            config, observation.knowledgeState))
        throw std::runtime_error("AI policy selected an action rejected by safety policy");
    return selected;
}

std::optional<AvailableAction> choosePolicyAction(
    AgentPolicy& policy,
    const PlayerRoundSnapshot& snapshot,
    const AiConfig& config,
    const AiKnowledgeState& knowledge,
    const std::optional<EncodedAction>& previousAction) {
    const PolicyObservation observation = makePolicyObservation(
        snapshot, knowledge, config, previousAction);
    if (observation.legalActions.empty()) return std::nullopt;
    return resolvePolicyDecision(
        observation, policy.select(observation, config), config).action;
}

} // namespace basilisk::client::ai
