#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "basilisk/MatchResult.hpp"
#include "basilisk/client/ai/LearnedPolicy.hpp"

namespace basilisk::client::ai {

enum class RuntimeAiPolicyMode { Heuristic, Learned, Shadow };

[[nodiscard]] std::optional<RuntimeAiPolicyMode> parseRuntimeAiPolicyMode(
    std::string_view value) noexcept;
[[nodiscard]] const char* runtimeAiPolicyModeName(
    RuntimeAiPolicyMode mode) noexcept;

struct ShadowTelemetryRecord {
    std::string context;
    RoundNumber round{};
    PlayerId player{};
    AiDifficulty difficulty{AiDifficulty::Medium};
    AiBehavior behavior{AiBehavior::Balanced};
    std::size_t heuristicActionIndex{};
    std::size_t learnedActionIndex{};
    ActionType heuristicActionType{ActionType::Search};
    ActionType learnedActionType{ActionType::Search};
    bool agreement{};
    bool actionTypeAgreement{};
    bool learnedFallback{};
    bool modelError{};
};

struct ShadowTelemetryBucket {
    std::uint64_t decisions{};
    std::uint64_t agreements{};
    std::uint64_t actionTypeAgreements{};
    std::uint64_t fallbacks{};
    std::uint64_t modelErrors{};
};

struct ShadowTelemetryAggregate : ShadowTelemetryBucket {
    std::array<ShadowTelemetryBucket, 3> byDifficulty{};
    std::array<ShadowTelemetryBucket, 7> byBehavior{};
    std::array<ShadowTelemetryBucket, 5> byHeuristicAction{};
    std::array<ShadowTelemetryBucket, 5> byOutcome{};
    std::uint64_t outcomes{};
};

// Thread-safe aggregate with optional streaming JSONL. Records contain only
// policy inputs/outputs and public-safe context identifiers supplied by the
// runtime owner; no snapshots or authoritative state are serialized.
class AiShadowTelemetry final {
public:
    explicit AiShadowTelemetry(std::string outputPath = {});

    void record(const ShadowTelemetryRecord& record);
    void recordOutcome(
        std::string context, MatchOutcome outcome, std::optional<PlayerId> winner);
    [[nodiscard]] ShadowTelemetryAggregate aggregate() const;
    [[nodiscard]] std::optional<ShadowTelemetryRecord> lastRecord() const;
    [[nodiscard]] std::string summary() const;

private:
    struct ContextBucket {
        ShadowTelemetryBucket decisions;
    };

    mutable std::mutex mutex_;
    ShadowTelemetryAggregate aggregate_;
    std::optional<ShadowTelemetryRecord> lastRecord_;
    std::map<std::string, ContextBucket> contexts_;
    std::ofstream output_;
};

struct RuntimeAiPolicyConfig {
    RuntimeAiPolicyMode mode{RuntimeAiPolicyMode::Heuristic};
    std::string modelPath;
    std::string context{"ai-match"};
    std::shared_ptr<AiShadowTelemetry> telemetry;
};

struct RuntimeAiPolicySelection {
    PolicyDecision authoritative;
    AiDecisionEvaluation heuristicEvaluation;
    std::optional<PolicyDecision> learned;
    bool learnedFallback{};
};

class RuntimeAiPolicy final {
public:
    explicit RuntimeAiPolicy(RuntimeAiPolicyConfig config = {});

    [[nodiscard]] RuntimeAiPolicySelection select(
        const PolicyObservation& observation, const AiConfig& config);
    [[nodiscard]] RuntimeAiPolicyMode mode() const noexcept { return config_.mode; }
    [[nodiscard]] bool learnedModelLoaded() const noexcept;
    [[nodiscard]] const std::string& learnedModelError() const noexcept;

private:
    RuntimeAiPolicyConfig config_;
    HeuristicPolicy heuristic_;
    std::unique_ptr<LearnedPolicy> learned_;
};

} // namespace basilisk::client::ai
