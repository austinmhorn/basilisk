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
    addStateAction(result, "legal_position", type,
        observation.legalActions.size() > 1 &&
            encodedPosition != observation.legalActions.end()
            ? static_cast<double>(std::distance(
                observation.legalActions.begin(), encodedPosition)) /
                static_cast<double>(observation.legalActions.size() - 1)
            : 0.0);

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

    if (snapshot.recoverableRivalSigilAvailable)
        addStateAction(result, "recoverable_sigil", type);
    if (snapshot.hasHunterSigil) addStateAction(result, "has_sigil", type);
    if (snapshot.extractionCave) addStateAction(result, "known_extraction", type);

    if (action.targetCave) {
        add(result, "target_cave|type=" + type);
        const auto cave = std::find_if(snapshot.map.caves.begin(), snapshot.map.caves.end(),
            [&](const DiscoveredCaveView& candidate) {
                return candidate.cave == *action.targetCave;
            });
        if (cave != snapshot.map.caves.end()) {
            add(result, "target_known|type=" + type);
            if (cave->surveyed) add(result, "target_surveyed|type=" + type);
        }
        if (observation.knowledgeState.isConfirmedPit(*action.targetCave))
            add(result, "target_confirmed_pit|type=" + type);
        if (observation.knowledgeState.isPitCandidate(*action.targetCave))
            add(result, "target_pit_candidate|type=" + type);
        if (observation.knowledge.previousCave == action.targetCave)
            add(result, "target_previous_cave|type=" + type);
    }
    if (action.targetTunnel) add(result, "target_tunnel|type=" + type);
    if (action.targetItem)
        add(result, integerToken("item=", *action.targetItem) + "|type=" + type);
    if (action.contextualAction)
        add(result, integerToken("contextual=", *action.contextualAction) + "|type=" + type);
    if (observation.previousAction) {
        add(result, "previous=" + actionTypeToken(
            observation.previousAction->action.type) + "|type=" + type);
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
    return {best, "learned-linear-v1"};
}

} // namespace basilisk::client::ai
