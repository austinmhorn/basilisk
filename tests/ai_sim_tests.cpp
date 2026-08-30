#include <cassert>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>

#include "AiSimulation.hpp"
#include "AiBenchmark.hpp"

using namespace basilisk;
using namespace basilisk::sim;

namespace {

class CollectingTransitions final : public TransitionSink {
public:
    void write(const Transition& transition) override { values.push_back(transition); }
    std::vector<Transition> values;
};

class CountingTransitions final : public TransitionSink {
public:
    void write(const Transition&) override { ++count; }
    std::size_t count{};
};

void deterministicEpisodes() {
    SimulationConfig config;
    config.seed = 87123;
    config.p1 = {client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Random};
    config.p2 = {client::ai::AiDifficulty::Medium, client::ai::AiBehavior::Random};
    const auto first = runEpisode(config, 0);
    const auto again = runEpisode(config, 0);
    assert(episodeJson(first) == episodeJson(again));
    assert(first.status == MatchStatus::Completed);
    assert(first.players.size() == 2);
    assert(first.players[0].resolvedBehavior != client::ai::AiBehavior::Random);
    assert(first.players[1].resolvedBehavior != client::ai::AiBehavior::Random);
    assert(first.players[0].aiSeed != first.players[1].aiSeed);

    const auto different = runEpisode(config, 1);
    assert(episodeJson(first) != episodeJson(different));
}

void jsonAndAggregate() {
    SimulationConfig config;
    config.seed = 919191;
    BatchAggregate aggregate;
    for (std::uint64_t index = 0; index < 3; ++index) {
        const auto episode = runEpisode(config, index);
        const std::string json = episodeJson(episode);
        assert(json.starts_with("{\"schemaVersion\":1,"));
        assert(json.find("\"players\":[") != std::string::npos);
        assert(json.find("\"resolvedBehavior\":") != std::string::npos);
        assert(json.find("\"actions\":") != std::string::npos);
        aggregate.add(episode);
    }
    assert(aggregate.matches == 3);
    assert(aggregate.completed + aggregate.unfinished == 3);
    assert(aggregate.p1Wins + aggregate.p2Wins + aggregate.draws == 3);
}

void legalActionsRemainExactAcrossBatch() {
    SimulationConfig config;
    config.seed = 123;
    config.p1 = {client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Balanced};
    config.p2 = {client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Balanced};
    for (std::uint64_t index = 0; index < 10; ++index)
        assert(runEpisode(config, index).status == MatchStatus::Completed);
}

void benchmarkAccountingAndDeterminism() {
    const BenchmarkMatchup asymmetric{"Easy vs Hard",
        {client::ai::AiDifficulty::Easy, client::ai::AiBehavior::Balanced},
        {client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Balanced}};
    auto first = runBenchmarkMatchup(asymmetric, 4401, 3);
    auto again = runBenchmarkMatchup(asymmetric, 4401, 3);
    assert(first.orientations == 2);
    assert(first.matches == 6);
    assert(first.aP1Matches == 3 && first.aP2Matches == 3);
    assert(first.bP1Matches == 3 && first.bP2Matches == 3);
    assert(first.p1Wins + first.p2Wins + first.draws == first.matches);
    assert(first.aWins + first.bWins + first.draws == first.matches);
    first.elapsedSeconds = 1.0;
    again.elapsedSeconds = 1.0;
    assert(benchmarkCsvRow(first) == benchmarkCsvRow(again));

    const BenchmarkMatchup identical{"Hard vs Hard",
        {client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Balanced},
        {client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Balanced}};
    const auto same = runBenchmarkMatchup(identical, 4401, 3);
    assert(same.orientations == 1);
    assert(same.matches == 3);
    assert(same.aP1Matches == 3 && same.aP2Matches == 0);
    assert(same.bP1Matches == 0 && same.bP2Matches == 3);
    assert(benchmarkCsvHeader().starts_with("schema_version,"));
}

void trainingTransitionsAreSafeAndLinked() {
    SimulationConfig baseline;
    baseline.seed = 7719;
    baseline.p1 = {client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Balanced};
    baseline.p2 = {client::ai::AiDifficulty::Medium, client::ai::AiBehavior::Explorer};
    const std::string unchangedEpisode = episodeJson(runEpisode(baseline, 0));

    CollectingTransitions transitions;
    baseline.transitionSink = &transitions;
    const auto withTransitions = runEpisode(baseline, 0);
    assert(episodeJson(withTransitions) == unchangedEpisode);
    assert(!transitions.values.empty());
    bool foundTerminal = false;
    for (std::size_t index = 0; index < transitions.values.size(); ++index) {
        const auto& transition = transitions.values[index];
        assert(transition.observation.legalActions.size() <=
            transition.observation.sourceSnapshot.availableActions.size());
        for (std::size_t action = 0; action < transition.observation.legalActions.size(); ++action) {
            const std::size_t sourceIndex =
                transition.observation.legalActions[action].legalIndex;
            assert(sourceIndex <
                transition.observation.sourceSnapshot.availableActions.size());
            assert(transition.observation.legalActions[action].action.type ==
                transition.observation.sourceSnapshot.availableActions[sourceIndex].type);
        }
        assert(transition.decision.legalActionIndex <
            transition.observation.legalActions.size());
        assert(transition.chosenAction.legalIndex ==
            transition.observation.legalActions[
                transition.decision.legalActionIndex].legalIndex);
        assert(transition.nextObservation.sourceSnapshot.round >=
            transition.observation.sourceSnapshot.round);
        const std::string json = transitionJson(transition);
        assert(json.find("\"schemaVersion\":1") != std::string::npos);
        assert(json.find("basiliskCave") == std::string::npos);
        assert(json.find("jackalCave") == std::string::npos);
        assert(json.find("hiddenTopology") == std::string::npos);
        if (transition.terminal) {
            foundTerminal = true;
            assert(transition.reward.total() >= -1.0 && transition.reward.total() <= 1.0);
            if (transition.outcome == MatchOutcome::None)
                assert(transition.reward.loss == -1.0);
        }
    }
    assert(foundTerminal);

    AgentDecision illegal;
    illegal.legalActionIndex = transitions.values.front().observation.legalActions.size();
    bool rejected = false;
    try {
        const auto& transition = transitions.values.front();
        (void)resolveDecision(transitions.values.front().observation, illegal,
            {transition.config.difficulty, transition.resolvedBehavior,
                transition.player, 0});
    }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
}

void randomPolicyAndTransitionStreamsAreDeterministic() {
    SimulationConfig config;
    config.seed = 8128;
    config.p1Policy = PolicyKind::Heuristic;
    config.p2Policy = PolicyKind::Random;
    CollectingTransitions first;
    config.transitionSink = &first;
    const auto firstEpisode = runEpisode(config, 0);
    CollectingTransitions second;
    config.transitionSink = &second;
    const auto secondEpisode = runEpisode(config, 0);
    assert(episodeJson(firstEpisode) == episodeJson(secondEpisode));
    assert(first.values.size() == second.values.size());
    for (std::size_t index = 0; index < first.values.size(); ++index)
        assert(transitionJson(first.values[index]) == transitionJson(second.values[index]));

    CountingTransitions counting;
    config.transitionSink = &counting;
    (void)runEpisode(config, 1);
    assert(counting.count > 0);
}

} // namespace

int main() {
    deterministicEpisodes();
    jsonAndAggregate();
    legalActionsRemainExactAcrossBatch();
    benchmarkAccountingAndDeterminism();
    trainingTransitionsAreSafeAndLinked();
    randomPolicyAndTransitionStreamsAreDeterministic();
    std::cout << "AI simulation tests passed\n";
}
