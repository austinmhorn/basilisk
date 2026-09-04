#include "AiSimulation.hpp"
#include "AiBenchmark.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
using namespace basilisk::client::ai;

AiDifficulty difficulty(std::string_view value) {
    if (value == "easy") return AiDifficulty::Easy;
    if (value == "medium") return AiDifficulty::Medium;
    if (value == "hard") return AiDifficulty::Hard;
    throw std::invalid_argument("difficulty must be easy, medium, or hard");
}
AiBehavior behavior(std::string_view value) {
    if (value == "balanced") return AiBehavior::Balanced;
    if (value == "explorer") return AiBehavior::Explorer;
    if (value == "aggressive") return AiBehavior::Aggressive;
    if (value == "objective") return AiBehavior::ObjectiveFocused;
    if (value == "survivalist") return AiBehavior::Survivalist;
    if (value == "opportunist") return AiBehavior::Opportunist;
    if (value == "random") return AiBehavior::Random;
    throw std::invalid_argument("unknown AI behavior");
}
std::uint64_t number(std::string_view value, std::string_view option) {
    std::size_t used = 0;
    const auto parsed = std::stoull(std::string(value), &used);
    if (used != value.size()) throw std::invalid_argument(std::string(option) + " requires an integer");
    return parsed;
}
basilisk::sim::BenchmarkSuite suite(std::string_view value) {
    if (value == "difficulty") return basilisk::sim::BenchmarkSuite::Difficulty;
    if (value == "hard-behaviors") return basilisk::sim::BenchmarkSuite::HardBehaviors;
    if (value == "all") return basilisk::sim::BenchmarkSuite::All;
    throw std::invalid_argument("--benchmark must be difficulty, hard-behaviors, or all");
}
basilisk::sim::PolicyKind policy(std::string_view value) {
    if (value == "heuristic") return basilisk::sim::PolicyKind::Heuristic;
    if (value == "learned") return basilisk::sim::PolicyKind::Learned;
    if (value == "random") return basilisk::sim::PolicyKind::Random;
    if (value == "canary") return basilisk::sim::PolicyKind::Canary;
    throw std::invalid_argument("policy must be heuristic, learned, random, or canary");
}

class JsonlTransitionSink final : public basilisk::sim::TransitionSink {
public:
    explicit JsonlTransitionSink(const std::string& path)
        : output_(path, std::ios::out | std::ios::trunc) {
        if (!output_) throw std::runtime_error("unable to open transition output: " + path);
    }
    void write(const basilisk::sim::Transition& transition) override {
        output_ << basilisk::sim::transitionJson(transition) << '\n';
        if (!output_) throw std::runtime_error("unable to write transition output");
    }
private:
    std::ofstream output_;
};
}

int main(int argc, char** argv) {
    try {
        basilisk::sim::SimulationConfig config;
        std::string outputPath;
        std::string transitionsOutput;
        std::string benchmarkOutput;
        std::string canaryTelemetryOutput;
        std::optional<basilisk::sim::BenchmarkSuite> benchmarkSuite;
        std::optional<std::uint64_t> episodeIndex;
        std::uint64_t matchesPerOrientation = 2000;
        for (int i = 1; i < argc; ++i) {
            const std::string_view option = argv[i];
            if (option == "--help") {
                std::cout << "Usage: BasiliskAiSim [--matches N | --episode-index N] [--seed N] [--output episodes.jsonl]\n"
                    "  [--p1-difficulty easy|medium|hard] [--p1-behavior balanced|explorer|aggressive|objective|survivalist|opportunist|random]\n"
                    "  [--p2-difficulty easy|medium|hard] [--p2-behavior ...]\n"
                    "  [--p1-policy heuristic|learned|random|canary] [--p2-policy heuristic|learned|random|canary]\n"
                    "  [--p1-model policy.model] [--p2-model policy.model]\n"
                    "  [--ai-canary-percent 0-100] [--ai-canary-difficulties medium,hard]\n"
                    "  [--ai-canary-telemetry canary.jsonl]\n"
                    "  [--transitions-output transitions.jsonl]\n"
                    "Benchmark: BasiliskAiSim --benchmark difficulty|hard-behaviors|all\n"
                    "  [--matches-per-orientation N] [--seed N] [--benchmark-output results.csv]\n";
                return 0;
            }
            if (i + 1 >= argc) throw std::invalid_argument(std::string(option) + " requires a value");
            const std::string_view value = argv[++i];
            if (option == "--matches") config.matches = number(value, option);
            else if (option == "--episode-index") episodeIndex = number(value, option);
            else if (option == "--seed") config.seed = number(value, option);
            else if (option == "--output") outputPath = value;
            else if (option == "--transitions-output") transitionsOutput = value;
            else if (option == "--benchmark") benchmarkSuite = suite(value);
            else if (option == "--matches-per-orientation")
                matchesPerOrientation = number(value, option);
            else if (option == "--benchmark-output") benchmarkOutput = value;
            else if (option == "--p1-difficulty") config.p1.difficulty = difficulty(value);
            else if (option == "--p2-difficulty") config.p2.difficulty = difficulty(value);
            else if (option == "--p1-behavior") config.p1.behavior = behavior(value);
            else if (option == "--p2-behavior") config.p2.behavior = behavior(value);
            else if (option == "--p1-policy") config.p1Policy = policy(value);
            else if (option == "--p2-policy") config.p2Policy = policy(value);
            else if (option == "--p1-model") config.p1ModelPath = value;
            else if (option == "--p2-model") config.p2ModelPath = value;
            else if (option == "--ai-canary-percent") {
                const auto percent = number(value, option);
                if (percent > 100) throw std::invalid_argument(
                    "--ai-canary-percent must be between 0 and 100");
                config.canaryPercent = static_cast<std::uint8_t>(percent);
            }
            else if (option == "--ai-canary-difficulties") {
                const auto difficulties = parseRuntimeAiCanaryDifficulties(value);
                if (!difficulties) throw std::invalid_argument(
                    "--ai-canary-difficulties must be a comma-separated subset of easy,medium,hard");
                config.canaryDifficulties = *difficulties;
            }
            else if (option == "--ai-canary-telemetry") canaryTelemetryOutput = value;
            else throw std::invalid_argument("unknown option: " + std::string(option));
        }
        if (benchmarkSuite) {
            if (episodeIndex)
                throw std::invalid_argument("--episode-index is unavailable in aggregate benchmark mode");
            if (matchesPerOrientation == 0)
                throw std::invalid_argument("--matches-per-orientation must be greater than zero");
            if (!outputPath.empty())
                throw std::invalid_argument("--output is episode JSONL; use --benchmark-output for benchmark CSV");
            if (!transitionsOutput.empty())
                throw std::invalid_argument("transition output is unavailable in aggregate benchmark mode");
            std::ofstream csv;
            if (!benchmarkOutput.empty()) {
                csv.open(benchmarkOutput, std::ios::out | std::ios::trunc);
                if (!csv) throw std::runtime_error("unable to open benchmark output: " + benchmarkOutput);
                csv << basilisk::sim::benchmarkCsvHeader() << '\n';
            }
            const auto matchups = basilisk::sim::benchmarkMatchups(*benchmarkSuite);
            std::vector<basilisk::sim::BenchmarkResult> results;
            results.reserve(matchups.size());
            for (const auto& matchup : matchups) {
                auto result = basilisk::sim::runBenchmarkMatchup(
                    matchup, config.seed, matchesPerOrientation);
                if (csv) {
                    csv << basilisk::sim::benchmarkCsvRow(result) << '\n';
                    csv.flush();
                }
                results.push_back(std::move(result));
            }
            const char* name = *benchmarkSuite == basilisk::sim::BenchmarkSuite::Difficulty
                ? "difficulty" : *benchmarkSuite == basilisk::sim::BenchmarkSuite::HardBehaviors
                ? "hard-behaviors" : "all";
            basilisk::sim::writeBenchmarkSummary(std::cout, name, config.seed, results);
            return 0;
        }
        if (!episodeIndex && config.matches == 0)
            throw std::invalid_argument("--matches must be greater than zero");
        if (!canaryTelemetryOutput.empty())
            config.canaryTelemetry = std::make_shared<AiShadowTelemetry>(canaryTelemetryOutput);
        std::unique_ptr<JsonlTransitionSink> transitionSink;
        if (!transitionsOutput.empty()) {
            transitionSink = std::make_unique<JsonlTransitionSink>(transitionsOutput);
            config.transitionSink = transitionSink.get();
        }
        std::ofstream episodes;
        if (!outputPath.empty()) {
            episodes.open(outputPath, std::ios::out | std::ios::trunc);
            if (!episodes) throw std::runtime_error("unable to open episode output: " + outputPath);
        }
        basilisk::sim::BatchAggregate aggregate;
        const auto start = std::chrono::steady_clock::now();
        const std::uint64_t firstEpisode = episodeIndex.value_or(0);
        const std::uint64_t episodeCount = episodeIndex ? 1 : config.matches;
        for (std::uint64_t offset = 0; offset < episodeCount; ++offset) {
            const std::uint64_t index = firstEpisode + offset;
            auto episode = basilisk::sim::runEpisode(config, index);
            aggregate.add(episode);
            if (episodes) episodes << basilisk::sim::episodeJson(episode) << '\n';
        }
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        basilisk::sim::writeSummary(std::cout, aggregate, elapsed);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "BasiliskAiSim: " << error.what() << '\n';
        return 1;
    }
}
