#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "AiSimulation.hpp"

namespace basilisk::sim {

inline constexpr int kAiBenchmarkSchemaVersion = 1;

enum class BenchmarkSuite { Difficulty, HardBehaviors, All };

struct BenchmarkMatchup {
    std::string name;
    AgentSpec a;
    AgentSpec b;
};

struct BenchmarkResult {
    BenchmarkMatchup matchup;
    std::uint64_t matchesPerOrientation{};
    std::uint64_t orientations{};
    std::uint64_t matches{};
    std::uint64_t p1Wins{};
    std::uint64_t p2Wins{};
    std::uint64_t draws{};
    std::uint64_t aWins{};
    std::uint64_t bWins{};
    std::uint64_t aP1Matches{};
    std::uint64_t aP1Wins{};
    std::uint64_t aP2Matches{};
    std::uint64_t aP2Wins{};
    std::uint64_t bP1Matches{};
    std::uint64_t bP1Wins{};
    std::uint64_t bP2Matches{};
    std::uint64_t bP2Wins{};
    std::uint64_t totalRounds{};
    std::uint64_t basiliskWins{};
    std::uint64_t extractionWins{};
    std::uint64_t moves{};
    std::uint64_t searches{};
    std::uint64_t shoots{};
    std::uint64_t itemUses{};
    std::uint64_t arrowsFired{};
    std::uint64_t arrowsHit{};
    std::uint64_t arrowsMissed{};
    std::uint64_t clashes{};
    std::uint64_t aClashWins{};
    std::uint64_t bClashWins{};
    std::map<std::string, std::uint64_t> deathCauses;
    double elapsedSeconds{};
};

[[nodiscard]] std::vector<BenchmarkMatchup> benchmarkMatchups(BenchmarkSuite suite);
[[nodiscard]] BenchmarkResult runBenchmarkMatchup(
    const BenchmarkMatchup& matchup, std::uint64_t seed,
    std::uint64_t matchesPerOrientation);
[[nodiscard]] std::string benchmarkCsvHeader();
[[nodiscard]] std::string benchmarkCsvRow(const BenchmarkResult& result);
void writeBenchmarkSummary(std::ostream& output,
    std::string_view suiteName, std::uint64_t seed,
    const std::vector<BenchmarkResult>& results);

} // namespace basilisk::sim
