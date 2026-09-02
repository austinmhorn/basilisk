#include "basilisk/client/ai/LearnedPolicy.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace basilisk::client::ai {
namespace {

constexpr std::string_view kModelMagic = "BASILISK_LINEAR_POLICY";

bool sameAction(const AvailableAction& left, const AvailableAction& right) {
    return left.type == right.type && left.targetCave == right.targetCave &&
        left.targetTunnel == right.targetTunnel &&
        left.targetItem == right.targetItem &&
        left.contextualAction == right.contextualAction;
}

void add(std::array<double, kLearnedPolicyFeatureCount>& values,
    std::string_view name, double value = 1.0) {
    values[learnedPolicyFeatureIndex(name)] += value;
}

std::string actionTypeToken(ActionType type) {
    switch (type) {
        case ActionType::Move: return "move";
        case ActionType::Search: return "search";
        case ActionType::Shoot: return "shoot";
        case ActionType::UseItem: return "use_item";
        case ActionType::Contextual: return "contextual";
    }
    return "unknown";
}

template <typename T>
std::string integerToken(std::string_view prefix, T value) {
    return std::string(prefix) + std::to_string(static_cast<int>(value));
}

double ratio(int value, int maximum) {
    return maximum > 0 ? std::clamp(
        static_cast<double>(value) / static_cast<double>(maximum), 0.0, 1.0) : 0.0;
}

void addStateAction(std::array<double, kLearnedPolicyFeatureCount>& values,
    std::string_view state, std::string_view actionType, double amount = 1.0) {
    add(values, std::string(state) + "|type=" + std::string(actionType), amount);
}

} // namespace

std::size_t learnedPolicyFeatureIndex(std::string_view name) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char value : name) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(hash % kLearnedPolicyFeatureCount);
}

std::array<double, kLearnedPolicyFeatureCount> encodeLearnedPolicyFeatures(
    const PolicyObservation& observation,
    const EncodedAction& encoded,
    const AiConfig& config) {
    std::array<double, kLearnedPolicyFeatureCount> result{};
    const AvailableAction& action = encoded.action;
    const std::string type = actionTypeToken(action.type);
    add(result, "bias");
    add(result, "type=" + type);
    add(result, integerToken("difficulty=", config.difficulty) + "|type=" + type);
    add(result, integerToken("behavior=", config.behavior) + "|type=" + type);
    add(result, integerToken("difficulty=", config.difficulty) + "|" +
        integerToken("behavior=", config.behavior) + "|type=" + type);

    const auto& snapshot = observation.sourceSnapshot;
    addStateAction(result, "health_ratio", type,
        ratio(snapshot.health, snapshot.maxHealth));
    addStateAction(result, "arrows_ratio", type,
        ratio(snapshot.arrows, snapshot.maxArrows));
    addStateAction(result, "round", type,
        std::min(1.0, static_cast<double>(snapshot.round) / 100.0));
    const auto encodedPosition = std::find_if(observation.legalActions.begin(),
        observation.legalActions.end(), [&](const EncodedAction& candidate) {
            return candidate.legalIndex == encoded.legalIndex;
        });
    const std::size_t encodedPositionIndex = encodedPosition != observation.legalActions.end()
        ? static_cast<std::size_t>(std::distance(
            observation.legalActions.begin(), encodedPosition))
        : observation.legalActions.size();
    addStateAction(result, "legal_position", type,
        observation.legalActions.size() > 1 &&
            encodedPosition != observation.legalActions.end()
            ? static_cast<double>(encodedPositionIndex) /
                static_cast<double>(observation.legalActions.size() - 1)
            : 0.0);
    add(result, integerToken("legal_count=",
        std::min<std::size_t>(observation.legalActions.size(), 8)) + "|type=" + type);
    add(result, integerToken("legal_position_band=",
        std::min<std::size_t>(encodedPositionIndex, 11)) + "|type=" + type);
    add(result, integerToken("round_band=",
        std::min<RoundNumber>(snapshot.round / 20, 5)) + "|type=" + type);
    const int arrowBand = snapshot.arrows <= 2 ? snapshot.arrows : 3;
    add(result, integerToken("arrow_band=", arrowBand) + "|type=" + type);
    const int healthBand = snapshot.maxHealth > 0
        ? std::clamp(snapshot.health * 4 / snapshot.maxHealth, 0, 3) : 0;
    add(result, integerToken("health_band=", healthBand) + "|type=" + type);

    const auto& knowledge = observation.knowledge;
    if (knowledge.pitWarning) addStateAction(result, "pit_warning", type);
    if (knowledge.basiliskAdjacentWarning)
        addStateAction(result, "basilisk_adjacent", type);
    if (knowledge.basiliskDistantWarning)
        addStateAction(result, "basilisk_distant", type);
    if (knowledge.jackalWarning) addStateAction(result, "jackal_warning", type);
    if (knowledge.rivalWarning) addStateAction(result, "rival_warning", type);
    addStateAction(result, "basilisk_candidates", type,
        std::min(1.0, static_cast<double>(knowledge.basiliskCandidateCount) / 6.0));
    addStateAction(result, "pit_candidates", type,
        std::min(1.0, static_cast<double>(knowledge.unresolvedPitCandidates) / 6.0));
    addStateAction(result, "repeated_searches", type,
        std::min(1.0, static_cast<double>(knowledge.repeatedSearches) / 5.0));
    add(result, integerToken("basilisk_candidate_band=",
        std::min<std::size_t>(knowledge.basiliskCandidateCount, 7)) + "|type=" + type);
    add(result, integerToken("repeated_search_band=",
        std::min<std::size_t>(knowledge.repeatedSearches, 5)) + "|type=" + type);
    add(result, integerToken("basilisk_adjacent_candidates=",
        knowledge.basiliskAdjacentWarning
            ? std::min<std::size_t>(knowledge.basiliskCandidateCount, 7) : 8) +
        "|type=" + type);

    if (snapshot.recoverableRivalSigilAvailable)
        addStateAction(result, "recoverable_sigil", type);
    if (snapshot.hasHunterSigil) addStateAction(result, "has_sigil", type);
    if (snapshot.extractionCave) addStateAction(result, "known_extraction", type);
    if (snapshot.hasHunterSigil && snapshot.extractionCave)
        addStateAction(result, "extracting", type);
    if (snapshot.recoverableRivalSigilAvailable && type == "search")
        add(result, "recoverable_sigil|search");

    std::size_t sameTypeRank = 0;
    for (std::size_t index = 0;
         index < encodedPositionIndex && index < observation.legalActions.size(); ++index) {
        const auto& candidate = observation.legalActions[index];
        if (candidate.action.type == action.type) ++sameTypeRank;
    }
    add(result, integerToken("type_rank=", std::min<std::size_t>(sameTypeRank, 7)) +
        "|type=" + type);
    for (const auto& candidate : observation.legalActions)
        add(result, "available=" + actionTypeToken(candidate.action.type) + "|type=" + type);

    if (action.targetCave) {
        add(result, "target_cave|type=" + type);
        const auto cave = std::find_if(snapshot.map.caves.begin(), snapshot.map.caves.end(),
            [&](const DiscoveredCaveView& candidate) {
                return candidate.cave == *action.targetCave;
            });
        if (cave != snapshot.map.caves.end()) {
            add(result, "target_known|type=" + type);
            if (cave->surveyed) add(result, "target_surveyed|type=" + type);
            addStateAction(result, "target_degree", type,
                std::min(1.0, static_cast<double>(cave->exits.size()) / 6.0));
        }
        if (observation.knowledgeState.isConfirmedPit(*action.targetCave))
            add(result, "target_confirmed_pit|type=" + type);
        if (observation.knowledgeState.isPitCandidate(*action.targetCave))
            add(result, "target_pit_candidate|type=" + type);
        if (observation.knowledge.previousCave == action.targetCave)
            add(result, "target_previous_cave|type=" + type);
        if (snapshot.extractionCave == action.targetCave)
            add(result, "target_extraction|type=" + type);
    }
    if (action.targetTunnel) add(result, "target_tunnel|type=" + type);
    if (action.targetItem)
        add(result, integerToken("item=", *action.targetItem) + "|type=" + type);
    if (action.contextualAction)
        add(result, integerToken("contextual=", *action.contextualAction) + "|type=" + type);
    if (observation.previousAction) {
        add(result, "previous=" + actionTypeToken(
            observation.previousAction->action.type) + "|type=" + type);
        if (sameAction(observation.previousAction->action, action))
            add(result, "repeat_exact_action|type=" + type);
    }
    return result;
}

LearnedPolicyModel LearnedPolicyModel::load(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("unable to open learned AI model: " + path);
    std::string magic;
    int modelVersion = 0;
    int observationVersion = 0;
    int actionVersion = 0;
    int featureVersion = 0;
    std::size_t featureCount = 0;
    input >> magic >> modelVersion >> observationVersion >> actionVersion >>
        featureVersion >> featureCount;
    if (!input || magic != kModelMagic || modelVersion != kLearnedPolicyModelVersion ||
        observationVersion != kAiObservationSchemaVersion ||
        actionVersion != kAiActionSchemaVersion ||
        featureVersion != kLearnedPolicyFeatureSchemaVersion ||
        featureCount != kLearnedPolicyFeatureCount)
        throw std::runtime_error("learned AI model schema is incompatible");
    LearnedPolicyModel model;
    for (double& weight : model.weights) {
        input >> weight;
        if (!input || !std::isfinite(weight))
            throw std::runtime_error("learned AI model weights are corrupt");
    }
    std::string trailing;
    if (input >> trailing)
        throw std::runtime_error("learned AI model contains unexpected trailing data");
    return model;
}

LearnedPolicy::LearnedPolicy(
    std::string modelPath, std::unique_ptr<AgentPolicy> fallback)
    : fallback_(std::move(fallback)) {
    if (!fallback_) fallback_ = std::make_unique<HeuristicPolicy>();
    try {
        model_ = LearnedPolicyModel::load(modelPath);
        modelLoaded_ = true;
    } catch (const std::exception& error) {
        loadError_ = error.what();
    }
}

PolicyDecision LearnedPolicy::select(
    const PolicyObservation& observation, const AiConfig& config) {
    if (!modelLoaded_) {
        PolicyDecision decision = fallback_->select(observation, config);
        decision.policyMetadata = "learned-fallback:" + decision.policyMetadata;
        return decision;
    }
    if (observation.legalActions.empty()) return {0, "learned:no-action"};
    HeuristicPolicy planner;
    auto [heuristicDecision, heuristicEvaluation] = planner.evaluate(observation, config);
    if (config.difficulty == AiDifficulty::Hard &&
        heuristicDecision.legalActionIndex < heuristicEvaluation.actions.size() &&
        heuristicEvaluation.actions[heuristicDecision.legalActionIndex].utility >= 7000.0)
        return {heuristicDecision.legalActionIndex,
            "learned-linear-v3:terminal-objective"};

    std::size_t best = 0;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < observation.legalActions.size(); ++index) {
        const auto features = encodeLearnedPolicyFeatures(
            observation, observation.legalActions[index], config);
        double score = 0.0;
        for (std::size_t feature = 0; feature < features.size(); ++feature)
            score += features[feature] * model_.weights[feature];
        if (score > bestScore) {
            bestScore = score;
            best = index;
        }
    }
    const ActionType selectedType = observation.legalActions[best].action.type;
    std::size_t planned = best;
    double plannedUtility = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0;
         index < observation.legalActions.size() && index < heuristicEvaluation.actions.size();
         ++index) {
        if (observation.legalActions[index].action.type == selectedType &&
            heuristicEvaluation.actions[index].utility > plannedUtility) {
            planned = index;
            plannedUtility = heuristicEvaluation.actions[index].utility;
        }
    }
    return {planned, "learned-linear-v3:planned-target"};
}

} // namespace basilisk::client::ai
