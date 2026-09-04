#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

void learnedPolicyRunsDeterministicallyThroughSimulator() {
    SimulationConfig config;
    config.seed = 24680;
    config.p1Policy = PolicyKind::Learned;
    config.p1ModelPath = BASILISK_TEST_LEARNED_MODEL;
    config.p2Policy = PolicyKind::Heuristic;
    CollectingTransitions first;
    config.transitionSink = &first;
    const auto firstEpisode = runEpisode(config, 0);
    CollectingTransitions second;
    config.transitionSink = &second;
    const auto secondEpisode = runEpisode(config, 0);
    assert(episodeJson(firstEpisode) == episodeJson(secondEpisode));
    assert(firstEpisode.status == MatchStatus::Completed);
    assert(first.values.size() == second.values.size());
    bool sawLearned = false;
    for (std::size_t index = 0; index < first.values.size(); ++index) {
        assert(transitionJson(first.values[index]) == transitionJson(second.values[index]));
        if (first.values[index].player == 1) {
            sawLearned = true;
            assert(first.values[index].policy == PolicyKind::Learned);
            assert(first.values[index].decision.policyMetadata.starts_with(
                "learned-linear-v3:"));
        }
    }
    assert(sawLearned);

    SimulationConfig fallback = config;
    fallback.transitionSink = nullptr;
    fallback.p1ModelPath = "/model/that/does/not/exist";
    SimulationConfig heuristic = fallback;
    heuristic.p1Policy = PolicyKind::Heuristic;
    assert(episodeJson(runEpisode(fallback, 1)) ==
        episodeJson(runEpisode(heuristic, 1)));
}

void canaryPolicyUsesRuntimeEligibilityAndTelemetry() {
    const auto outputPath = std::filesystem::temp_directory_path() /
        "basilisk-ai-sim-canary.jsonl";
    SimulationConfig config;
    config.seed = 717;
    config.p1Policy = PolicyKind::Canary;
    config.p2Policy = PolicyKind::Canary;
    config.p1ModelPath = BASILISK_TEST_LEARNED_MODEL;
    config.p2ModelPath = BASILISK_TEST_LEARNED_MODEL;
    config.canaryPercent = 100;
    config.p1.difficulty = client::ai::AiDifficulty::Easy;
    config.p2.difficulty = client::ai::AiDifficulty::Hard;
    {
        config.canaryTelemetry = std::make_shared<client::ai::AiShadowTelemetry>(
            outputPath.string());
        assert(runEpisode(config, 0).status == MatchStatus::Completed);
    }
    config.canaryTelemetry.reset();
    std::ifstream input(outputPath);
    std::stringstream contents;
    contents << input.rdbuf();
    input.close();
    const auto text = contents.str();
    assert(text.find("\"authoritativePolicy\":\"heuristic\"") != std::string::npos);
    assert(text.find("\"authoritativePolicy\":\"learned\"") != std::string::npos);
    assert(text.find("\"kind\":\"canary-outcome\"") != std::string::npos);
    std::filesystem::remove(outputPath);
}

void episode3077DoesNotRepeatTheKnownCaveOscillation() {
    SimulationConfig config;
    config.seed = 424242;
    config.p1 = {client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Survivalist};
    config.p2 = {client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Balanced};
    config.p1Policy = PolicyKind::Learned;
    config.p2Policy = PolicyKind::Learned;
    config.p1ModelPath = BASILISK_TEST_LEARNED_MODEL;
    config.p2ModelPath = BASILISK_TEST_LEARNED_MODEL;
    CollectingTransitions transitions;
    config.transitionSink = &transitions;
    const EpisodeTelemetry episode = runEpisode(config, 3077);
    assert(episode.status == MatchStatus::Completed);
    assert(episode.rounds < 250);

    std::vector<CaveId> p2Caves;
    for (const Transition& transition : transitions.values) {
        if (transition.player == 2)
            p2Caves.push_back(transition.observation.sourceSnapshot.currentCave);
    }
    std::size_t fourTurnCycles = 0;
    for (std::size_t index = 3; index < p2Caves.size(); ++index) {
        if (p2Caves[index] == p2Caves[index - 2] &&
            p2Caves[index - 1] == p2Caves[index - 3] &&
            p2Caves[index] != p2Caves[index - 1]) ++fourTurnCycles;
    }
    assert(fourTurnCycles == 0);
}

void episode277TakesTheSafeUnknownTunnelAfterPitResolution() {
    SimulationConfig config;
    config.seed = 424242;
    config.p1 = {client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Survivalist};
    config.p2 = {client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Random};
    config.p1Policy = PolicyKind::Learned;
    config.p2Policy = PolicyKind::Learned;
    config.p1ModelPath = BASILISK_TEST_LEARNED_MODEL;
    config.p2ModelPath = BASILISK_TEST_LEARNED_MODEL;
    CollectingTransitions transitions;
    config.transitionSink = &transitions;
    const EpisodeTelemetry episode = runEpisode(config, 277);
    assert(episode.status == MatchStatus::Completed);
    assert(episode.rounds < 100);

    // Investigation can now happen earlier on another frontier. Protect the
    // semantic Search -> resolved uncertainty -> safe unknown Move transition,
    // not an obsolete round/cave itinerary (covered separately by unit tests).
    bool exploredAfterInvestigation = false;
    for (const auto& search : transitions.values) {
        if (search.player != 1 || search.chosenAction.action.type != ActionType::Search ||
            search.observation.knowledgeState.unresolvedPitCandidateCount() == 0) continue;
        const auto next = std::ranges::find_if(transitions.values,
            [&](const Transition& transition) {
                return transition.player == search.player && transition.round == search.round + 1;
            });
        if (next != transitions.values.end() &&
            next->observation.knowledgeState.unresolvedPitCandidateCount() == 0 &&
            next->chosenAction.action.type == ActionType::Move &&
            next->chosenAction.action.targetTunnel.has_value())
            exploredAfterInvestigation = true;
    }
    assert(exploredAfterInvestigation);
}

void strategicStallCorpusCompletesWithoutLongTailLoops() {
    SimulationConfig config;
    config.seed = 424242;
    config.p1 = {client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Random};
    config.p2 = {client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Random};
    config.p1Policy = PolicyKind::Learned;
    config.p2Policy = PolicyKind::Learned;
    config.p1ModelPath = BASILISK_TEST_LEARNED_MODEL;
    config.p2ModelPath = BASILISK_TEST_LEARNED_MODEL;
    for (const std::size_t episodeIndex : {8533U, 381U, 482U, 4499U, 4174U}) {
        const EpisodeTelemetry episode = runEpisode(config, episodeIndex);
        assert(episode.status == MatchStatus::Completed);
        assert(episode.rounds < 100);
    }
    // Confirmed-but-unresolved Pit exits must not attract repeated routes,
    // and an investigation followed by relocation must not trap the hunter.
    for (const std::size_t episodeIndex : {6454U, 9338U, 1474U, 6231U, 9018U}) {
        const EpisodeTelemetry episode = runEpisode(config, episodeIndex);
        assert(episode.status == MatchStatus::Completed);
        assert(episode.rounds < 150);
    }
}

} // namespace

int main() {
    deterministicEpisodes();
    jsonAndAggregate();
    legalActionsRemainExactAcrossBatch();
    benchmarkAccountingAndDeterminism();
    trainingTransitionsAreSafeAndLinked();
    randomPolicyAndTransitionStreamsAreDeterministic();
    learnedPolicyRunsDeterministicallyThroughSimulator();
    canaryPolicyUsesRuntimeEligibilityAndTelemetry();
    episode3077DoesNotRepeatTheKnownCaveOscillation();
    episode277TakesTheSafeUnknownTunnelAfterPitResolution();
    strategicStallCorpusCompletesWithoutLongTailLoops();
    std::cout << "AI simulation tests passed\n";
}
