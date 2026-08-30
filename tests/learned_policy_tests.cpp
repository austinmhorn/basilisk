#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "basilisk/client/ai/LearnedPolicy.hpp"

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

} // namespace

int main() {
    modelValidationAndDeterministicInference();
    incompatibleAndCorruptModelsFallBackToHeuristic();
    directLoaderRejectsCorruptWeights();
    std::cout << "Basilisk learned policy tests passed.\n";
}
