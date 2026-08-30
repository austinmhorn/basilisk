#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>

#include "basilisk/client/ai/LearnedPolicy.hpp"
#include "basilisk/client/ai/RuntimeAiPolicy.hpp"

using namespace basilisk;
using namespace basilisk::client::ai;

namespace {

bool sameAction(const AvailableAction& left, const AvailableAction& right) {
    return left.type == right.type && left.targetCave == right.targetCave &&
        left.targetTunnel == right.targetTunnel &&
        left.targetItem == right.targetItem &&
        left.contextualAction == right.contextualAction;
}

PlayerRoundSnapshot snapshot() {
    PlayerRoundSnapshot result;
    result.player = 7;
    result.round = 12;
    result.health = 70;
    result.maxHealth = 100;
    result.arrows = 4;
    result.maxArrows = 5;
    result.alive = true;
    result.currentCave = 1;
    result.map.currentCave = 1;
    result.map.caves.push_back(DiscoveredCaveView{1,
        {TunnelView{1, CaveId{2}, true}, TunnelView{2, CaveId{3}, false}}, false});
    AvailableAction pit;
    pit.type = ActionType::Move;
    pit.targetCave = 2;
    AvailableAction safe;
    safe.type = ActionType::Move;
    safe.targetCave = 3;
    AvailableAction search;
    search.type = ActionType::Search;
    result.availableActions = {pit, safe, search};
    result.observations.push_back({ObservationType::PitNearby, result.player});
    return result;
}

std::filesystem::path temporaryModel(std::string_view contents) {
    static unsigned counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
        ("basilisk-learned-policy-test-" + std::to_string(++counter) + ".model");
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    output << contents;
    return path;
}

void modelValidationAndDeterministicInference() {
    LearnedPolicy policy{BASILISK_TEST_LEARNED_MODEL};
    assert(policy.modelLoaded());
    assert(policy.loadError().empty());

    auto safe = snapshot();
    AiKnowledgeState knowledge;
    knowledge.observe(safe);
    const AiConfig config{AiDifficulty::Hard, AiBehavior::Balanced, 7, 991};
    const auto observation = makePolicyObservation(safe, knowledge, config);
    assert(observation.legalActions.size() == 2);
    assert(std::none_of(observation.legalActions.begin(),
        observation.legalActions.end(), [](const EncodedAction& action) {
            return action.action.targetCave == CaveId{2};
        }));
    const PolicyDecision first = policy.select(observation, config);
    const PolicyDecision second = policy.select(observation, config);
    assert(first.legalActionIndex == second.legalActionIndex);
    assert(first.policyMetadata == "learned-linear-v1");
    const auto& selected = resolvePolicyDecision(observation, first, config);
    assert(selected.legalIndex < safe.availableActions.size());
    assert(sameAction(selected.action, safe.availableActions[selected.legalIndex]));
}

void incompatibleAndCorruptModelsFallBackToHeuristic() {
    const auto incompatible = temporaryModel("BASILISK_LINEAR_POLICY 99 1 1 1 128\n");
    LearnedPolicy learned{incompatible.string()};
    assert(!learned.modelLoaded());
    assert(!learned.loadError().empty());

    auto safe = snapshot();
    AiKnowledgeState knowledge;
    knowledge.observe(safe);
    const AiConfig config{AiDifficulty::Hard, AiBehavior::Balanced, 7, 991};
    const auto observation = makePolicyObservation(safe, knowledge, config);
    HeuristicPolicy heuristic;
    const auto expected = heuristic.select(observation, config);
    const auto fallback = learned.select(observation, config);
    assert(fallback.legalActionIndex == expected.legalActionIndex);
    assert(fallback.policyMetadata == "learned-fallback:heuristic");

    LearnedPolicy missing{incompatible.string() + ".missing"};
    assert(!missing.modelLoaded());
    assert(missing.select(observation, config).legalActionIndex ==
        expected.legalActionIndex);
    std::filesystem::remove(incompatible);
}

void directLoaderRejectsCorruptWeights() {
    const auto corrupt = temporaryModel(
        "BASILISK_LINEAR_POLICY 1 1 1 1 128\n0 1 not-a-number\n");
    bool rejected = false;
    try {
        (void)LearnedPolicyModel::load(corrupt.string());
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
    std::filesystem::remove(corrupt);
}

void runtimePolicyModesPreserveAuthorityAndFallback() {
    auto safe = snapshot();
    AiKnowledgeState knowledge;
    knowledge.observe(safe);
    const AiConfig config{AiDifficulty::Hard, AiBehavior::Balanced, 7, 991};
    const auto observation = makePolicyObservation(safe, knowledge, config);

    RuntimeAiPolicy defaultPolicy;
    const auto heuristic = defaultPolicy.select(observation, config);
    assert(defaultPolicy.mode() == RuntimeAiPolicyMode::Heuristic);
    assert(!heuristic.learned.has_value());

    auto telemetry = std::make_shared<AiShadowTelemetry>();
    RuntimeAiPolicy shadow{{RuntimeAiPolicyMode::Shadow,
        BASILISK_TEST_LEARNED_MODEL, "runtime-shadow", telemetry}};
    const auto shadowed = shadow.select(observation, config);
    assert(shadow.learnedModelLoaded());
    assert(shadowed.learned.has_value());
    assert(shadowed.authoritative.legalActionIndex ==
        heuristic.authoritative.legalActionIndex);
    assert(resolvePolicyDecision(observation, shadowed.authoritative, config).legalIndex ==
        resolvePolicyDecision(observation, heuristic.authoritative, config).legalIndex);
    const auto aggregate = telemetry->aggregate();
    assert(aggregate.decisions == 1);
    assert(aggregate.byDifficulty[static_cast<std::size_t>(AiDifficulty::Hard)].decisions == 1);
    assert(aggregate.byBehavior[static_cast<std::size_t>(AiBehavior::Balanced)].decisions == 1);
    assert(telemetry->lastRecord()->round == safe.round);
    assert(telemetry->lastRecord()->player == safe.player);

    RuntimeAiPolicy learned{{RuntimeAiPolicyMode::Learned,
        BASILISK_TEST_LEARNED_MODEL, "runtime-learned", {}}};
    const auto selected = learned.select(observation, config);
    assert(selected.learned.has_value());
    assert(selected.authoritative.legalActionIndex ==
        selected.learned->legalActionIndex);
    (void)resolvePolicyDecision(observation, selected.authoritative, config);

    RuntimeAiPolicy missing{{RuntimeAiPolicyMode::Learned,
        std::string{BASILISK_TEST_LEARNED_MODEL} + ".missing", "fallback", {}}};
    const auto fallback = missing.select(observation, config);
    assert(fallback.learnedFallback);
    assert(fallback.authoritative.legalActionIndex ==
        heuristic.authoritative.legalActionIndex);
}

void shadowTelemetryIsDeterministicAndPublicSafe() {
    const auto outputPath = std::filesystem::temp_directory_path() /
        "basilisk-shadow-policy-test.jsonl";
    {
        auto telemetry = std::make_shared<AiShadowTelemetry>(outputPath.string());
        RuntimeAiPolicy policy{{RuntimeAiPolicyMode::Shadow,
            BASILISK_TEST_LEARNED_MODEL, "shadow-episode", telemetry}};
        auto safe = snapshot();
        AiKnowledgeState knowledge;
        knowledge.observe(safe);
        const AiConfig config{AiDifficulty::Hard, AiBehavior::ObjectiveFocused, 7, 991};
        const auto observation = makePolicyObservation(safe, knowledge, config);
        const auto first = policy.select(observation, config);
        const auto second = policy.select(observation, config);
        assert(first.authoritative.legalActionIndex == second.authoritative.legalActionIndex);
        assert(first.learned->legalActionIndex == second.learned->legalActionIndex);
        telemetry->recordOutcome("shadow-episode", MatchOutcome::BasiliskKilled,
            PlayerId{7});
        const auto aggregate = telemetry->aggregate();
        assert(aggregate.decisions == 2);
        assert(aggregate.outcomes == 1);
        assert(aggregate.byOutcome[static_cast<std::size_t>(
            MatchOutcome::BasiliskKilled)].decisions == 2);
        assert(telemetry->summary().find("decisions=2") != std::string::npos);
    }
    std::ifstream input(outputPath);
    std::stringstream contents;
    contents << input.rdbuf();
    const std::string serialized = contents.str();
    assert(serialized.find("\"kind\":\"decision\"") != std::string::npos);
    assert(serialized.find("\"kind\":\"outcome\"") != std::string::npos);
    assert(serialized.find("inventory") == std::string::npos);
    assert(serialized.find("health") == std::string::npos);
    assert(serialized.find("targetCave") == std::string::npos);
    assert(serialized.find("pending") == std::string::npos);
    std::filesystem::remove(outputPath);

    auto fallbackTelemetry = std::make_shared<AiShadowTelemetry>();
    RuntimeAiPolicy fallback{{RuntimeAiPolicyMode::Shadow,
        std::string{BASILISK_TEST_LEARNED_MODEL} + ".missing", "missing",
        fallbackTelemetry}};
    auto safe = snapshot();
    AiKnowledgeState knowledge;
    knowledge.observe(safe);
    const AiConfig config{AiDifficulty::Medium, AiBehavior::Explorer, 7, 992};
    const auto observation = makePolicyObservation(safe, knowledge, config);
    (void)fallback.select(observation, config);
    const auto aggregate = fallbackTelemetry->aggregate();
    assert(aggregate.fallbacks == 1);
    assert(aggregate.modelErrors == 1);
}

} // namespace

int main() {
    modelValidationAndDeterministicInference();
    incompatibleAndCorruptModelsFallBackToHeuristic();
    directLoaderRejectsCorruptWeights();
    runtimePolicyModesPreserveAuthorityAndFallback();
    shadowTelemetryIsDeterministicAndPublicSafe();
    std::cout << "Basilisk learned policy tests passed.\n";
}
