#include "basilisk/client/ai/RuntimeAiPolicy.hpp"

#include <iomanip>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace basilisk::client::ai {
namespace {

bool sameAction(const AvailableAction& left, const AvailableAction& right) {
    return left.type == right.type && left.targetCave == right.targetCave &&
        left.targetTunnel == right.targetTunnel &&
        left.targetItem == right.targetItem &&
        left.contextualAction == right.contextualAction;
}

std::size_t actionIndex(ActionType type) {
    return static_cast<std::size_t>(type);
}

void add(ShadowTelemetryBucket& bucket, const ShadowTelemetryRecord& record) {
    ++bucket.decisions;
    bucket.agreements += record.agreement;
    bucket.actionTypeAgreements += record.actionTypeAgreement;
    bucket.fallbacks += record.learnedFallback;
    bucket.modelErrors += record.modelError;
}

double rate(std::uint64_t numerator, std::uint64_t denominator) {
    return denominator == 0 ? 0.0 :
        static_cast<double>(numerator) / static_cast<double>(denominator);
}

} // namespace

std::optional<RuntimeAiPolicyMode> parseRuntimeAiPolicyMode(
    std::string_view value) noexcept {
    if (value == "heuristic") return RuntimeAiPolicyMode::Heuristic;
    if (value == "learned") return RuntimeAiPolicyMode::Learned;
    if (value == "shadow") return RuntimeAiPolicyMode::Shadow;
    if (value == "canary") return RuntimeAiPolicyMode::Canary;
    return std::nullopt;
}

const char* runtimeAiPolicyModeName(RuntimeAiPolicyMode mode) noexcept {
    switch (mode) {
        case RuntimeAiPolicyMode::Heuristic: return "heuristic";
        case RuntimeAiPolicyMode::Learned: return "learned";
        case RuntimeAiPolicyMode::Shadow: return "shadow";
        case RuntimeAiPolicyMode::Canary: return "canary";
    }
    return "heuristic";
}

std::optional<std::uint8_t> parseRuntimeAiCanaryDifficulties(
    std::string_view value) noexcept {
    std::uint8_t mask = 0;
    while (!value.empty()) {
        const auto separator = value.find(',');
        const auto token = value.substr(0, separator);
        if (token == "easy") mask |= kCanaryEasy;
        else if (token == "medium") mask |= kCanaryMedium;
        else if (token == "hard") mask |= kCanaryHard;
        else return std::nullopt;
        if (separator == std::string_view::npos) break;
        value.remove_prefix(separator + 1);
    }
    return mask == 0 ? std::nullopt : std::optional<std::uint8_t>{mask};
}

bool runtimeAiCanaryDifficultyEligible(
    std::uint8_t mask, AiDifficulty difficulty) noexcept {
    const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned>(difficulty));
    return (mask & bit) != 0;
}

bool runtimeAiCanaryAssigned(
    std::string_view context, PlayerId player, std::uint8_t percent) noexcept {
    if (percent == 0) return false;
    if (percent >= 100) return true;
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char value : context) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    for (std::size_t shift = 0; shift < sizeof(PlayerId); ++shift) {
        hash ^= static_cast<unsigned char>((player >> (shift * 8)) & 0xffU);
        hash *= 1099511628211ULL;
    }
    return hash % 100 < percent;
}

AiShadowTelemetry::AiShadowTelemetry(std::string outputPath) {
    if (!outputPath.empty()) {
        output_.open(outputPath, std::ios::out | std::ios::trunc);
        if (!output_) throw std::runtime_error(
            "unable to open AI shadow telemetry output: " + outputPath);
    }
}

void AiShadowTelemetry::record(const ShadowTelemetryRecord& record) {
    std::lock_guard lock{mutex_};
    add(aggregate_, record);
    add(aggregate_.byDifficulty[static_cast<std::size_t>(record.difficulty)], record);
    add(aggregate_.byBehavior[static_cast<std::size_t>(record.behavior)], record);
    add(aggregate_.byHeuristicAction[actionIndex(record.heuristicActionType)], record);
    add(contexts_[record.context].decisions, record);
    lastRecord_ = record;
    if (output_) {
        output_ << "{\"schemaVersion\":1,\"kind\":\"decision\",\"context\":\""
            << record.context << "\",\"round\":" << record.round
            << ",\"player\":" << record.player
            << ",\"difficulty\":" << static_cast<int>(record.difficulty)
            << ",\"behavior\":" << static_cast<int>(record.behavior)
            << ",\"heuristicActionIndex\":" << record.heuristicActionIndex
            << ",\"learnedActionIndex\":" << record.learnedActionIndex
            << ",\"heuristicActionType\":" << static_cast<int>(record.heuristicActionType)
            << ",\"learnedActionType\":" << static_cast<int>(record.learnedActionType)
            << ",\"agreement\":" << (record.agreement ? "true" : "false")
            << ",\"actionTypeAgreement\":"
            << (record.actionTypeAgreement ? "true" : "false")
            << ",\"fallback\":" << (record.learnedFallback ? "true" : "false")
            << ",\"modelError\":" << (record.modelError ? "true" : "false")
            << "}\n";
        output_.flush();
    }
}

void AiShadowTelemetry::recordOutcome(
    std::string context, MatchOutcome outcome, std::optional<PlayerId> winner) {
    std::lock_guard lock{mutex_};
    ++aggregate_.outcomes;
    const auto found = contexts_.find(context);
    if (found != contexts_.end()) {
        auto& bucket = aggregate_.byOutcome[static_cast<std::size_t>(outcome)];
        bucket.decisions += found->second.decisions.decisions;
        bucket.agreements += found->second.decisions.agreements;
        bucket.actionTypeAgreements += found->second.decisions.actionTypeAgreements;
        bucket.fallbacks += found->second.decisions.fallbacks;
        bucket.modelErrors += found->second.decisions.modelErrors;
        contexts_.erase(found);
    }
    if (output_) {
        output_ << "{\"schemaVersion\":1,\"kind\":\"outcome\",\"context\":\""
            << context << "\",\"outcome\":" << static_cast<int>(outcome)
            << ",\"winner\":";
        if (winner) output_ << *winner; else output_ << "null";
        output_ << "}\n";
        output_.flush();
    }
}

void AiShadowTelemetry::recordCanaryDecision(const ShadowTelemetryRecord& record,
    bool assigned, bool authoritativeLearned, bool safetyViolation) {
    std::lock_guard lock{mutex_};
    if (!output_) return;
    output_ << "{\"schemaVersion\":1,\"kind\":\"canary-decision\",\"context\":\""
        << record.context << "\",\"round\":" << record.round
        << ",\"player\":" << record.player
        << ",\"difficulty\":" << static_cast<int>(record.difficulty)
        << ",\"behavior\":" << static_cast<int>(record.behavior)
        << ",\"assigned\":" << (assigned ? "true" : "false")
        << ",\"authoritativePolicy\":\""
        << (authoritativeLearned ? "learned" : "heuristic") << "\""
        << ",\"actionType\":" << static_cast<int>(
            authoritativeLearned ? record.learnedActionType : record.heuristicActionType)
        << ",\"fallback\":" << (record.learnedFallback ? "true" : "false")
        << ",\"modelError\":" << (record.modelError ? "true" : "false")
        << ",\"safetyViolation\":" << (safetyViolation ? "true" : "false")
        << "}\n";
    output_.flush();
}

void AiShadowTelemetry::recordCanaryOutcome(std::string context,
    MatchOutcome outcome, std::optional<PlayerId> winner, bool completed, bool stalled) {
    std::lock_guard lock{mutex_};
    if (!output_) return;
    output_ << "{\"schemaVersion\":1,\"kind\":\"canary-outcome\",\"context\":\""
        << context << "\",\"outcome\":" << static_cast<int>(outcome)
        << ",\"winner\":";
    if (winner) output_ << *winner; else output_ << "null";
    output_ << ",\"completed\":" << (completed ? "true" : "false")
        << ",\"stalled\":" << (stalled ? "true" : "false") << "}\n";
    output_.flush();
}

ShadowTelemetryAggregate AiShadowTelemetry::aggregate() const {
    std::lock_guard lock{mutex_};
    return aggregate_;
}

std::optional<ShadowTelemetryRecord> AiShadowTelemetry::lastRecord() const {
    std::lock_guard lock{mutex_};
    return lastRecord_;
}

std::string AiShadowTelemetry::summary() const {
    const auto values = aggregate();
    std::ostringstream output;
    output << std::fixed << std::setprecision(4)
        << "decisions=" << values.decisions
        << " agreement=" << rate(values.agreements, values.decisions)
        << " action_type_agreement="
        << rate(values.actionTypeAgreements, values.decisions)
        << " fallback=" << rate(values.fallbacks, values.decisions)
        << " model_error=" << rate(values.modelErrors, values.decisions)
        << " outcomes=" << values.outcomes;
    return output.str();
}

RuntimeAiPolicy::RuntimeAiPolicy(RuntimeAiPolicyConfig config)
    : config_(std::move(config)) {
    if (config_.mode != RuntimeAiPolicyMode::Heuristic &&
        (config_.mode != RuntimeAiPolicyMode::Canary || config_.canaryPercent > 0))
        learned_ = std::make_unique<LearnedPolicy>(config_.modelPath);
}

RuntimeAiPolicySelection RuntimeAiPolicy::select(
    const PolicyObservation& observation, const AiConfig& config) {
    auto [heuristicDecision, evaluation] = heuristic_.evaluate(observation, config);
    RuntimeAiPolicySelection result{heuristicDecision, std::move(evaluation)};
    if (learned_ == nullptr) return result;

    result.canaryAssigned = config_.mode == RuntimeAiPolicyMode::Canary &&
        runtimeAiCanaryDifficultyEligible(config_.canaryDifficulties,
            config.difficulty) &&
        runtimeAiCanaryAssigned(config_.context, config.player,
            config_.canaryPercent);
    if (config_.mode == RuntimeAiPolicyMode::Canary && !result.canaryAssigned) {
        if (config_.telemetry != nullptr &&
            heuristicDecision.legalActionIndex < observation.legalActions.size()) {
            const auto& action = observation.legalActions[heuristicDecision.legalActionIndex];
            config_.telemetry->recordCanaryDecision({config_.context,
                observation.sourceSnapshot.round, observation.sourceSnapshot.player,
                config.difficulty, config.behavior, action.legalIndex, action.legalIndex,
                action.action.type, action.action.type, true, true, false, false},
                false, false);
        }
        return result;
    }

    result.learned = learned_->select(observation, config);
    result.learnedFallback = !learned_->modelLoaded();
    if (config_.mode == RuntimeAiPolicyMode::Learned || result.canaryAssigned)
        result.authoritative = *result.learned;

    if (config_.mode == RuntimeAiPolicyMode::Canary && config_.telemetry != nullptr &&
        heuristicDecision.legalActionIndex < observation.legalActions.size() &&
        result.learned->legalActionIndex < observation.legalActions.size()) {
        const auto& heuristicAction = observation.legalActions[heuristicDecision.legalActionIndex];
        const auto& learnedAction = observation.legalActions[result.learned->legalActionIndex];
        config_.telemetry->recordCanaryDecision({config_.context,
            observation.sourceSnapshot.round, observation.sourceSnapshot.player,
            config.difficulty, config.behavior, heuristicAction.legalIndex,
            learnedAction.legalIndex, heuristicAction.action.type,
            learnedAction.action.type, sameAction(heuristicAction.action, learnedAction.action),
            heuristicAction.action.type == learnedAction.action.type,
            result.learnedFallback, !learned_->modelLoaded()}, true,
            learned_->modelLoaded());
    }

    if (config_.mode == RuntimeAiPolicyMode::Shadow && config_.telemetry != nullptr &&
        heuristicDecision.legalActionIndex < observation.legalActions.size() &&
        result.learned->legalActionIndex < observation.legalActions.size()) {
        const auto& heuristicAction =
            observation.legalActions[heuristicDecision.legalActionIndex];
        const auto& learnedAction = observation.legalActions[result.learned->legalActionIndex];
        config_.telemetry->record({config_.context, observation.sourceSnapshot.round,
            observation.sourceSnapshot.player, config.difficulty, config.behavior,
            heuristicAction.legalIndex, learnedAction.legalIndex,
            heuristicAction.action.type, learnedAction.action.type,
            sameAction(heuristicAction.action, learnedAction.action),
            heuristicAction.action.type == learnedAction.action.type,
            result.learnedFallback, !learned_->modelLoaded()});
    }
    return result;
}

bool RuntimeAiPolicy::learnedModelLoaded() const noexcept {
    return learned_ != nullptr && learned_->modelLoaded();
}

const std::string& RuntimeAiPolicy::learnedModelError() const noexcept {
    static const std::string empty;
    return learned_ == nullptr ? empty : learned_->loadError();
}

} // namespace basilisk::client::ai
