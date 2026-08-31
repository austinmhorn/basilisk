#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "basilisk/client/ai/AiPolicy.hpp"

namespace basilisk::client::ai {

inline constexpr int kLearnedPolicyModelVersion = 2;
inline constexpr int kLearnedPolicyFeatureSchemaVersion = 2;
inline constexpr std::size_t kLearnedPolicyFeatureCount = 128;

struct LearnedPolicyModel {
    std::array<double, kLearnedPolicyFeatureCount> weights{};

    [[nodiscard]] static LearnedPolicyModel load(const std::string& path);
};

// A deterministic linear scorer over each already-filtered legal action.
// Construction never throws for a missing/corrupt model: select() delegates
// to the supplied production-safe fallback instead.
class LearnedPolicy final : public AgentPolicy {
public:
    explicit LearnedPolicy(std::string modelPath,
        std::unique_ptr<AgentPolicy> fallback = std::make_unique<HeuristicPolicy>());

    [[nodiscard]] PolicyDecision select(
        const PolicyObservation& observation, const AiConfig& config) override;
    [[nodiscard]] bool modelLoaded() const noexcept { return modelLoaded_; }
    [[nodiscard]] const std::string& loadError() const noexcept { return loadError_; }

private:
    LearnedPolicyModel model_;
    std::unique_ptr<AgentPolicy> fallback_;
    bool modelLoaded_{};
    std::string loadError_;
};

// Public for compatibility tests and offline trainers. Features are derived
// exclusively from PolicyObservation and one safety-filtered encoded action.
[[nodiscard]] std::array<double, kLearnedPolicyFeatureCount>
encodeLearnedPolicyFeatures(
    const PolicyObservation& observation,
    const EncodedAction& action,
    const AiConfig& config);

[[nodiscard]] std::size_t learnedPolicyFeatureIndex(std::string_view name) noexcept;

} // namespace basilisk::client::ai
