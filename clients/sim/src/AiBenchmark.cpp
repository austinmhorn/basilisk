#include "AiBenchmark.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace basilisk::sim {
namespace {
using client::ai::AiBehavior;
using client::ai::AiDifficulty;

bool sameSpec(const AgentSpec& a, const AgentSpec& b) {
    return a.difficulty == b.difficulty && a.behavior == b.behavior;
}

std::string specName(const AgentSpec& spec) {
    return std::string(client::ai::difficultyName(spec.difficulty)) + " " +
        client::ai::behaviorName(spec.behavior);
}

double percent(std::uint64_t part, std::uint64_t total) {
    return total == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(total);
}

double average(std::uint64_t value, std::uint64_t count) {
    return count == 0 ? 0.0 : static_cast<double>(value) / static_cast<double>(count);
}

void addEpisode(BenchmarkResult& result, const EpisodeTelemetry& episode,
                bool mirrored) {
    ++result.matches;
    result.totalRounds += episode.rounds;
    result.clashes += episode.clashes;
    result.basiliskWins += episode.basiliskKill;
    result.extractionWins += episode.extractionWin;
    if (!episode.winner) ++result.draws;
    else if (*episode.winner == episode.players[0].player) ++result.p1Wins;
    else ++result.p2Wins;

    for (std::size_t seat = 0; seat < episode.players.size(); ++seat) {
        const bool isA = mirrored ? seat == 1 : seat == 0;
        const bool won = episode.winner == episode.players[seat].player;
        const auto& player = episode.players[seat];
        result.moves += player.moves;
        result.searches += player.searches;
        result.shoots += player.shoots;
        result.itemUses += player.itemUses;
        result.arrowsFired += player.arrowsFired;
        result.arrowsHit += player.arrowHits;
        result.arrowsMissed += player.arrowMisses;
        if (player.deaths != 0) result.deathCauses[player.deathCause] += player.deaths;
        if (isA) {
            result.aWins += won;
            result.aClashWins += player.clashWins;
            if (seat == 0) { ++result.aP1Matches; result.aP1Wins += won; }
            else { ++result.aP2Matches; result.aP2Wins += won; }
        } else {
            result.bWins += won;
            result.bClashWins += player.clashWins;
            if (seat == 0) { ++result.bP1Matches; result.bP1Wins += won; }
            else { ++result.bP2Matches; result.bP2Wins += won; }
        }
    }
}

std::uint64_t cause(const BenchmarkResult& result, std::string_view name) {
    const auto it = result.deathCauses.find(std::string(name));
    return it == result.deathCauses.end() ? 0 : it->second;
}

BenchmarkMatchup matchup(std::string name, AiDifficulty aDifficulty,
    AiBehavior aBehavior, AiDifficulty bDifficulty, AiBehavior bBehavior) {
    return {std::move(name), {aDifficulty, aBehavior}, {bDifficulty, bBehavior}};
}

} // namespace

std::vector<BenchmarkMatchup> benchmarkMatchups(BenchmarkSuite suite) {
    std::vector<BenchmarkMatchup> result;
    if (suite == BenchmarkSuite::Difficulty || suite == BenchmarkSuite::All) {
        constexpr auto balanced = AiBehavior::Balanced;
        result.push_back(matchup("Easy vs Easy", AiDifficulty::Easy, balanced,
            AiDifficulty::Easy, balanced));
        result.push_back(matchup("Medium vs Medium", AiDifficulty::Medium, balanced,
            AiDifficulty::Medium, balanced));
        result.push_back(matchup("Hard vs Hard", AiDifficulty::Hard, balanced,
            AiDifficulty::Hard, balanced));
        result.push_back(matchup("Easy vs Medium", AiDifficulty::Easy, balanced,
            AiDifficulty::Medium, balanced));
        result.push_back(matchup("Medium vs Hard", AiDifficulty::Medium, balanced,
            AiDifficulty::Hard, balanced));
        result.push_back(matchup("Easy vs Hard", AiDifficulty::Easy, balanced,
            AiDifficulty::Hard, balanced));
    }
    if (suite == BenchmarkSuite::HardBehaviors || suite == BenchmarkSuite::All) {
        constexpr AiBehavior behaviors[]{AiBehavior::Balanced, AiBehavior::Explorer,
            AiBehavior::Aggressive, AiBehavior::ObjectiveFocused,
            AiBehavior::Survivalist, AiBehavior::Opportunist, AiBehavior::Random};
        for (std::size_t left = 0; left < std::size(behaviors); ++left) {
            for (std::size_t right = left; right < std::size(behaviors); ++right) {
                const AgentSpec a{AiDifficulty::Hard, behaviors[left]};
                const AgentSpec b{AiDifficulty::Hard, behaviors[right]};
                result.push_back({specName(a) + " vs " + specName(b), a, b});
            }
        }
    }
    return result;
}

BenchmarkResult runBenchmarkMatchup(const BenchmarkMatchup& matchup,
    std::uint64_t seed, std::uint64_t matchesPerOrientation) {
    BenchmarkResult result;
    result.matchup = matchup;
    result.matchesPerOrientation = matchesPerOrientation;
    result.orientations = sameSpec(matchup.a, matchup.b) ? 1 : 2;
    const auto start = std::chrono::steady_clock::now();
    SimulationConfig config;
    config.seed = seed;
    config.matches = matchesPerOrientation;
    config.p1 = matchup.a;
    config.p2 = matchup.b;
    for (std::uint64_t index = 0; index < matchesPerOrientation; ++index)
        addEpisode(result, runEpisode(config, index), false);
    if (result.orientations == 2) {
        std::swap(config.p1, config.p2);
        for (std::uint64_t index = 0; index < matchesPerOrientation; ++index)
            addEpisode(result, runEpisode(config, index), true);
    }
    result.elapsedSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    return result;
}

std::string benchmarkCsvHeader() {
    return "schema_version,matchup,config_a,config_b,matches_per_orientation,orientations,matches,"
        "p1_wins,p2_wins,draws,config_a_wins,config_b_wins,config_a_win_pct,config_b_win_pct,"
        "config_a_p1_win_pct,config_a_p2_win_pct,config_b_p1_win_pct,config_b_p2_win_pct,"
        "avg_rounds,basilisk_wins,extraction_wins,deaths_pit,deaths_jackal,deaths_hunter,"
        "deaths_basilisk_or_hazard,avg_moves,avg_searches,avg_shoots,avg_item_uses,"
        "avg_arrows_fired,avg_arrows_hit,avg_arrows_missed,clash_frequency,config_a_clash_wins,"
        "config_b_clash_wins,throughput_matches_per_sec";
}

std::string benchmarkCsvRow(const BenchmarkResult& r) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4)
        << kAiBenchmarkSchemaVersion << ",\"" << r.matchup.name << "\",\""
        << specName(r.matchup.a) << "\",\"" << specName(r.matchup.b) << "\","
        << r.matchesPerOrientation << ',' << r.orientations << ',' << r.matches << ','
        << r.p1Wins << ',' << r.p2Wins << ',' << r.draws << ',' << r.aWins << ',' << r.bWins << ','
        << percent(r.aWins, r.matches) << ',' << percent(r.bWins, r.matches) << ','
        << percent(r.aP1Wins, r.aP1Matches) << ',' << percent(r.aP2Wins, r.aP2Matches) << ','
        << percent(r.bP1Wins, r.bP1Matches) << ',' << percent(r.bP2Wins, r.bP2Matches) << ','
        << average(r.totalRounds, r.matches) << ',' << r.basiliskWins << ',' << r.extractionWins << ','
        << cause(r, "pit") << ',' << cause(r, "jackal") << ',' << cause(r, "hunter") << ','
        << cause(r, "basilisk_or_hazard") << ',' << average(r.moves, r.matches) << ','
        << average(r.searches, r.matches) << ',' << average(r.shoots, r.matches) << ','
        << average(r.itemUses, r.matches) << ',' << average(r.arrowsFired, r.matches) << ','
        << average(r.arrowsHit, r.matches) << ',' << average(r.arrowsMissed, r.matches) << ','
        << average(r.clashes, r.matches) << ',' << r.aClashWins << ',' << r.bClashWins << ','
        << (r.elapsedSeconds > 0.0 ? static_cast<double>(r.matches) / r.elapsedSeconds : 0.0);
    return out.str();
}

void writeBenchmarkSummary(std::ostream& out, std::string_view suiteName,
    std::uint64_t seed, const std::vector<BenchmarkResult>& results) {
    out << "Basilisk AI benchmark v" << kAiBenchmarkSchemaVersion << " — " << suiteName
        << " (seed " << seed << ")\n";
    for (const auto& r : results) {
        const double aP1 = percent(r.aP1Wins, r.aP1Matches);
        const double aP2 = percent(r.aP2Wins, r.aP2Matches);
        out << "  " << r.matchup.name << ": " << r.matches << " matches; A "
            << std::fixed << std::setprecision(1) << percent(r.aWins, r.matches)
            << "% / B " << percent(r.bWins, r.matches) << "% / draw "
            << percent(r.draws, r.matches) << "%; P1 " << percent(r.p1Wins, r.matches)
            << "% P2 " << percent(r.p2Wins, r.matches) << "%";
        if (sameSpec(r.matchup.a, r.matchup.b))
            out << "; seat delta " << percent(r.p1Wins, r.matches) - percent(r.p2Wins, r.matches) << "pp";
        else {
            out << "; A by seat " << aP1 << "%/" << aP2 << "% ("
                << std::abs(aP1 - aP2) << "pp)";
            const double bP1 = percent(r.bP1Wins, r.bP1Matches);
            const double bP2 = percent(r.bP2Wins, r.bP2Matches);
            out << "; B by seat " << bP1 << "%/" << bP2 << "% ("
                << std::abs(bP1 - bP2) << "pp)";
            if (std::max(std::abs(aP1 - aP2), std::abs(bP1 - bP2)) >= 5.0)
                out << " [seat-dependent]";
        }
        out << "; avg rounds " << average(r.totalRounds, r.matches)
            << "; " << (r.elapsedSeconds > 0.0 ? r.matches / r.elapsedSeconds : 0.0)
            << " matches/sec\n"
            << "    outcomes BSK/EXT " << r.basiliskWins << '/' << r.extractionWins
            << "; deaths pit/jackal/hunter/other " << cause(r, "pit") << '/'
            << cause(r, "jackal") << '/' << cause(r, "hunter") << '/'
            << cause(r, "basilisk_or_hazard")
            << "; avg M/S/F/I " << average(r.moves, r.matches) << '/'
            << average(r.searches, r.matches) << '/' << average(r.shoots, r.matches)
            << '/' << average(r.itemUses, r.matches)
            << "; arrows F/H/M " << average(r.arrowsFired, r.matches) << '/'
            << average(r.arrowsHit, r.matches) << '/' << average(r.arrowsMissed, r.matches)
            << "; clash freq " << average(r.clashes, r.matches)
            << " wins A/B " << r.aClashWins << '/' << r.bClashWins << '\n';
    }

    auto sanity = [&](std::string_view name, bool aShouldWin) {
        const auto it = std::find_if(results.begin(), results.end(),
            [&](const BenchmarkResult& value) { return value.matchup.name == name; });
        if (it == results.end()) return;
        const bool passed = aShouldWin ? it->aWins > it->bWins : it->bWins > it->aWins;
        out << "  difficulty sanity — " << name << ": " << (passed ? "PASS" : "FAIL") << '\n';
    };
    sanity("Easy vs Medium", false);
    sanity("Medium vs Hard", false);
    sanity("Easy vs Hard", false);
}

} // namespace basilisk::sim
