#include "basilisk/client/ai/AiPolicy.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "basilisk/client/RoutePlanner.hpp"

namespace basilisk::client::ai {
namespace {

bool sameAction(const AvailableAction& left, const AvailableAction& right) {
    return left.type == right.type && left.targetCave == right.targetCave &&
        left.targetTunnel == right.targetTunnel &&
        left.targetItem == right.targetItem &&
        left.contextualAction == right.contextualAction;
}

const DiscoveredCaveView* caveView(const PlayerMapView& map, CaveId cave) {
    const auto found = std::find_if(map.caves.begin(), map.caves.end(),
        [cave](const DiscoveredCaveView& candidate) { return candidate.cave == cave; });
    return found == map.caves.end() ? nullptr : &*found;
}

const TunnelView* actionTunnel(
    const PlayerRoundSnapshot& snapshot, const AvailableAction& action) {
    const DiscoveredCaveView* current = caveView(snapshot.map, snapshot.currentCave);
    if (current == nullptr) return nullptr;
    const auto found = std::find_if(current->exits.begin(), current->exits.end(),
        [&](const TunnelView& tunnel) {
            return (action.targetTunnel && tunnel.id == *action.targetTunnel) ||
                (action.targetCave && tunnel.destination == action.targetCave);
        });
    return found == current->exits.end() ? nullptr : &*found;
}

bool safeExplorationMove(const PlayerRoundSnapshot& snapshot,
    const AvailableAction& action, const AiKnowledgeState& knowledge) {
    if (action.type != ActionType::Move) return false;
    if (action.targetCave &&
        (knowledge.isConfirmedPit(*action.targetCave) ||
         knowledge.isPitCandidate(*action.targetCave) ||
         knowledge.temporarilyAvoids(
             snapshot.currentCave, *action.targetCave, snapshot.round))) return false;
    const TunnelView* tunnel = actionTunnel(snapshot, action);
    if (tunnel == nullptr || tunnel->strongColdDraft) return false;
    if (action.targetTunnel && !tunnel->destination) {
        const AiExitKey exit{snapshot.currentCave, *action.targetTunnel};
        return !knowledge.isConfirmedPitExit(exit) &&
            !knowledge.isPitCandidateExit(exit);
    }
    return true;
}

PlayerMapView safeExplorationMap(
    const PlayerRoundSnapshot& snapshot, const AiKnowledgeState& knowledge) {
    PlayerMapView safe = snapshot.map;
    safe.caves.erase(std::remove_if(safe.caves.begin(), safe.caves.end(),
        [&](const DiscoveredCaveView& cave) {
            return cave.cave != snapshot.currentCave &&
                (knowledge.isConfirmedPit(cave.cave) || knowledge.isPitCandidate(cave.cave));
        }), safe.caves.end());
    for (DiscoveredCaveView& cave : safe.caves) {
        cave.exits.erase(std::remove_if(cave.exits.begin(), cave.exits.end(),
            [&](const TunnelView& tunnel) {
                return tunnel.strongColdDraft ||
                    knowledge.isConfirmedPitExit({cave.cave, tunnel.id}) ||
                    (tunnel.destination &&
                     (knowledge.isConfirmedPit(*tunnel.destination) ||
                      knowledge.isPitCandidate(*tunnel.destination) ||
                      knowledge.temporarilyAvoids(
                          cave.cave, *tunnel.destination, snapshot.round)));
            }), cave.exits.end());
    }
    return safe;
}

bool higherPriorityOverride(
    const PolicyObservation& observation, const AiConfig& config) {
    const auto& snapshot = observation.sourceSnapshot;
    const auto& knowledge = observation.knowledgeState;
    if (snapshot.hasHunterSigil && std::any_of(observation.legalActions.begin(),
        observation.legalActions.end(), [](const EncodedAction& action) {
            return action.action.type == ActionType::UseItem &&
                action.action.targetItem == ItemType::SurveyFragment;
        })) return true;
    if (snapshot.recoverableRivalSigilAvailable &&
        !knowledge.hasCheckedForRecoverableSigil(snapshot.currentCave)) return true;
    if (std::any_of(observation.legalActions.begin(), observation.legalActions.end(),
        [](const EncodedAction& action) {
            return action.action.type == ActionType::Contextual &&
                action.action.contextualAction == ContextualActionType::Escape;
        })) return true;
    if (snapshot.health * 4 <= snapshot.maxHealth &&
        std::any_of(observation.legalActions.begin(), observation.legalActions.end(),
            [](const EncodedAction& action) {
                return action.action.type == ActionType::UseItem &&
                    action.action.targetItem == ItemType::HealingDraught;
            })) return true;
    if (!knowledge.basiliskWarningHere() || snapshot.arrows <= 0) return false;
    std::size_t candidates = 0;
    for (const EncodedAction& encoded : observation.legalActions) {
        const TunnelView* tunnel = actionTunnel(snapshot, encoded.action);
        if (encoded.action.type == ActionType::Shoot &&
            (!encoded.action.targetCave ||
             !knowledge.isConfirmedPit(*encoded.action.targetCave)) &&
            (tunnel == nullptr || !tunnel->strongColdDraft) &&
            !knowledge.isDisprovenBasiliskTarget(snapshot.currentCave, encoded.action))
            ++candidates;
    }
    return config.difficulty == AiDifficulty::Hard ? candidates > 0 : candidates == 1;
}

std::vector<EncodedAction> terminalObjectiveActions(
    const PolicyObservation& observation, const AiConfig& config) {
    const auto& snapshot = observation.sourceSnapshot;
    if (config.difficulty == AiDifficulty::Easy ||
        snapshot.matchStatus != MatchStatus::Active) return {};
    const auto& knowledge = observation.knowledgeState;
    std::vector<EncodedAction> escape;
    for (const EncodedAction& encoded : observation.legalActions) {
        if (encoded.action.type == ActionType::Contextual &&
            encoded.action.contextualAction == ContextualActionType::Escape)
            escape.push_back(encoded);
    }
    if (!escape.empty()) return escape;
    if (!snapshot.hasHunterSigil || !snapshot.extractionCave) return {};

    const PlayerMapView safeMap = safeExplorationMap(snapshot, knowledge);
    const auto route = client_navigation::planKnownRoute(
        safeMap, *snapshot.extractionCave);
    if (route.status != client_navigation::KnownRouteStatus::Reachable ||
        route.caves.size() < 2) return {};
    std::vector<EncodedAction> nextHop;
    for (const EncodedAction& encoded : observation.legalActions) {
        if (encoded.action.targetCave == route.caves[1] &&
            safeExplorationMove(snapshot, encoded.action, knowledge))
            nextHop.push_back(encoded);
    }
    return nextHop;
}

std::vector<EncodedAction> explorationActions(
    const PolicyObservation& observation, const AiConfig& config) {
    const auto& snapshot = observation.sourceSnapshot;
    const auto& knowledge = observation.knowledgeState;
    if (config.difficulty == AiDifficulty::Easy ||
        snapshot.matchStatus != MatchStatus::Active ||
        observation.legalActions.size() <= 1 || higherPriorityOverride(observation, config))
        return {};

    std::vector<EncodedAction> direct;
    for (const EncodedAction& encoded : observation.legalActions) {
        if (!safeExplorationMove(snapshot, encoded.action, knowledge)) continue;
        const TunnelView* tunnel = actionTunnel(snapshot, encoded.action);
        const DiscoveredCaveView* destination = encoded.action.targetCave
            ? caveView(snapshot.map, *encoded.action.targetCave) : nullptr;
        if ((tunnel != nullptr && !tunnel->destination) ||
            (destination != nullptr &&
             (destination->surveyed || !knowledge.isKnownSafe(destination->cave))))
            direct.push_back(encoded);
    }
    if (!direct.empty()) return direct;

    // Reaching an uncertain frontier is not progress by itself. Once stalled,
    // investigate its unresolved exits instead of filtering Search out and
    // repeatedly routing away/back. An inconclusive investigation may retry;
    // confirmed Pit exits and immediately traversable frontiers stay distinct.
    if (knowledge.turnsSinceExplorationProgress() >= 4 &&
        knowledge.pitWarningHere() && knowledge.unresolvedPitCandidateCount() > 0) {
        std::vector<EncodedAction> investigation;
        for (const EncodedAction& encoded : observation.legalActions)
            if (encoded.action.type == ActionType::Search)
                investigation.push_back(encoded);
        if (!investigation.empty()) return investigation;
    }

    struct RouteOption {
        std::size_t length{};
        CaveId next{};
        bool immediateBacktrack{};
        std::size_t traversals{};
        std::size_t visits{};
        std::size_t cyclePenalty{};
    };
    const PlayerMapView safeMap = safeExplorationMap(snapshot, knowledge);
    std::vector<RouteOption> routes;
    for (const DiscoveredCaveView& cave : safeMap.caves) {
        const bool frontier = cave.surveyed || !knowledge.isKnownSafe(cave.cave) ||
            std::any_of(cave.exits.begin(), cave.exits.end(),
                [](const TunnelView& tunnel) { return !tunnel.destination; });
        if (!frontier || cave.cave == snapshot.currentCave) continue;
        const auto route = client_navigation::planKnownRoute(safeMap, cave.cave);
        if (route.status != client_navigation::KnownRouteStatus::Reachable ||
            route.caves.size() < 2) continue;
        const bool legalFirstHop = std::any_of(observation.legalActions.begin(),
            observation.legalActions.end(), [&](const EncodedAction& action) {
                return action.action.targetCave == route.caves[1] &&
                    safeExplorationMove(snapshot, action.action, knowledge);
            });
        if (!legalFirstHop) continue;
        routes.push_back({route.caves.size(), route.caves[1],
            knowledge.previousCave() == route.caves[1],
            knowledge.explorationTraversalCount(snapshot.currentCave, route.caves[1]),
            knowledge.explorationVisitCount(route.caves[1]),
            knowledge.explorationCyclePenalty(route.caves[1])});
    }
    if (routes.empty()) return {};

    const auto routeRank = [](const RouteOption& route) {
        return std::tuple{route.immediateBacktrack, route.traversals,
            route.visits, route.cyclePenalty};
    };
    const auto bestRank = routeRank(*std::min_element(
        routes.begin(), routes.end(), [&](const RouteOption& left, const RouteOption& right) {
            return routeRank(left) < routeRank(right);
        }));
    std::size_t shortest = std::numeric_limits<std::size_t>::max();
    for (const RouteOption& route : routes) {
        if (routeRank(route) != bestRank) continue;
        shortest = std::min(shortest, route.length);
    }
    std::vector<CaveId> nextCaves;
    for (const RouteOption& route : routes) {
        if (routeRank(route) != bestRank || route.length != shortest) continue;
        if (std::find(nextCaves.begin(), nextCaves.end(), route.next) == nextCaves.end())
            nextCaves.push_back(route.next);
    }

    std::vector<EncodedAction> progressing;
    for (const EncodedAction& encoded : observation.legalActions) {
        if (!safeExplorationMove(snapshot, encoded.action, knowledge) ||
            !encoded.action.targetCave) continue;
        if (std::find(nextCaves.begin(), nextCaves.end(),
                *encoded.action.targetCave) != nextCaves.end())
            progressing.push_back(encoded);
    }
    return progressing;
}

std::vector<EncodedAction> nonCyclingFallbackActions(
    const PolicyObservation& observation, const AiConfig& config) {
    const auto& snapshot = observation.sourceSnapshot;
    const auto& knowledge = observation.knowledgeState;
    if (config.difficulty == AiDifficulty::Easy ||
        snapshot.matchStatus != MatchStatus::Active ||
        observation.legalActions.size() <= 1 || higherPriorityOverride(observation, config))
        return {};
    if (knowledge.turnsSinceExplorationProgress() < 4) return {};

    std::vector<EncodedAction> safeMoves;
    for (const EncodedAction& encoded : observation.legalActions) {
        if (safeExplorationMove(snapshot, encoded.action, knowledge) &&
            encoded.action.targetCave)
            safeMoves.push_back(encoded);
    }
    if (safeMoves.empty()) return {};

    const bool repeatedSearchState =
        knowledge.searchedWithoutExplorationProgress(snapshot.currentCave) &&
        knowledge.unresolvedPitCandidateCount() == 0;
    const auto moveRank = [&](const EncodedAction& encoded) {
        const CaveId destination = *encoded.action.targetCave;
        return std::tuple{
            knowledge.previousCave() == destination,
            knowledge.explorationTraversalCount(snapshot.currentCave, destination),
            knowledge.explorationVisitCount(destination),
            knowledge.explorationCyclePenalty(destination)};
    };
    const auto bestRank = moveRank(*std::min_element(safeMoves.begin(), safeMoves.end(),
        [&](const EncodedAction& left, const EncodedAction& right) {
            return moveRank(left) < moveRank(right);
        }));

    std::vector<EncodedAction> alternatives;
    for (const EncodedAction& encoded : safeMoves) {
        if (moveRank(encoded) == bestRank)
            alternatives.push_back(encoded);
    }
    // Once a cave has already been searched without any new map knowledge,
    // force a safe change of vantage rather than allowing the policy to repeat
    // the materially identical Search. The least-visited rule still permits a
    // necessary backtrack when it is the only traversable choice.
    if (repeatedSearchState || alternatives.size() < observation.legalActions.size())
        return alternatives;
    return {};
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
    PlayerRoundSnapshot snapshot = observation.sourceSnapshot;
    if (observation.explorationPriorityApplied) {
        snapshot.availableActions.clear();
        for (const EncodedAction& action : observation.legalActions)
            snapshot.availableActions.push_back(action.action);
    }
    AiDecisionEvaluation evaluation = engine_.evaluate(
        snapshot, config, observation.knowledgeState);
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
    const auto terminal = terminalObjectiveActions(result, config);
    if (!terminal.empty()) {
        result.legalActions = terminal;
        result.explorationPriorityApplied = true;
    } else {
        const auto priority = explorationActions(result, config);
        if (!priority.empty()) {
            result.legalActions = priority;
            result.explorationPriorityApplied = true;
        } else if (const auto fallback = nonCyclingFallbackActions(result, config);
            !fallback.empty()) {
            result.legalActions = fallback;
            result.explorationPriorityApplied = true;
        }
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
