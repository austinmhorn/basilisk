#include <array>
#include <iomanip>
#include <sstream>

// V3.11 includes the proven V3.9 simulator exactly once, then layers the
// main-free V3.10 telemetry helper and V3.11 matchup telemetry on top.
// No simulator source includes another versioned main file beyond this point.
#define main basilisk_v39_main
#include "main_v39.cpp"
#undef main
#include "v310_enraged_telemetry.hpp"

namespace {

struct MatchupCellV311 {
    std::uint64_t matches{0};
    std::uint64_t rowWins{0};
    std::uint64_t colWins{0};
    std::uint64_t basiliskWins{0};
    std::uint64_t extractionWins{0};
    std::uint64_t simultaneousBasiliskDraws{0};
    std::uint64_t otherDraws{0};
    std::uint64_t stalled{0};
    std::uint64_t pvpKills{0};
    std::uint64_t deaths{0};
    std::uint64_t rounds{0};
};

using MatchupGridV311 = std::array<std::array<MatchupCellV311, 4>, 4>;

std::uint64_t styleWinsTotal(const Stats& s, std::size_t i) { return s.style[i].wins; }
std::uint64_t stylePvpKillsTotal(const Stats& s, std::size_t i) { return s.style[i].pvpKills; }
std::uint64_t styleDeathsTotal(const Stats& s, std::size_t i) { return s.style[i].deaths; }

void runOneV311(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
                Stats& stats, EnragedLethalityStats& enraged, MatchupGridV311& grid) {
    // styleFor is deterministic from match seed + player id. Generated matches
    // currently use player ids 1 and 2; this is the same assignment logic used
    // by runOneV310.
    const auto row = static_cast<std::size_t>(styleFor(matchSeed, 1));
    const auto col = static_cast<std::size_t>(styleFor(matchSeed, 2));

    const auto beforeStalled = stats.stalled;
    const auto beforeBasilisk = stats.basiliskWins;
    const auto beforeExtraction = stats.extractionWins;
    const auto beforeSim = stats.simultaneousBasiliskDraws;
    const auto beforeDraws = stats.draws;
    const auto beforeRounds = stats.totalRounds;
    const auto beforeRowWins = styleWinsTotal(stats, row);
    const auto beforeColWins = styleWinsTotal(stats, col);
    const auto beforeRowPvp = stylePvpKillsTotal(stats, row);
    const auto beforeColPvp = stylePvpKillsTotal(stats, col);
    const auto beforeRowDeaths = styleDeathsTotal(stats, row);
    const auto beforeColDeaths = styleDeathsTotal(stats, col);

    runOneV310(mapSeed, matchSeed, maxRounds, stats, enraged);

    auto& cell = grid[row][col];
    ++cell.matches;
    cell.rounds += stats.totalRounds - beforeRounds;
    cell.basiliskWins += stats.basiliskWins - beforeBasilisk;
    cell.extractionWins += stats.extractionWins - beforeExtraction;
    cell.simultaneousBasiliskDraws += stats.simultaneousBasiliskDraws - beforeSim;
    cell.otherDraws += stats.draws - beforeDraws;
    cell.stalled += stats.stalled - beforeStalled;

    // For different-style matchups, style deltas identify which side won.
    // Mirror matches are intentionally left out of row/column win splits because
    // the cumulative style counter cannot distinguish P1 from P2; mirrors are
    // reported separately as combined wins below rather than fabricating a side.
    if (row != col) {
        cell.rowWins += styleWinsTotal(stats, row) - beforeRowWins;
        cell.colWins += styleWinsTotal(stats, col) - beforeColWins;
        cell.pvpKills += (stylePvpKillsTotal(stats, row) - beforeRowPvp)
                       + (stylePvpKillsTotal(stats, col) - beforeColPvp);
        cell.deaths += (styleDeathsTotal(stats, row) - beforeRowDeaths)
                     + (styleDeathsTotal(stats, col) - beforeColDeaths);
    } else {
        cell.rowWins += styleWinsTotal(stats, row) - beforeRowWins; // combined mirror wins
        cell.pvpKills += stylePvpKillsTotal(stats, row) - beforeRowPvp;
        cell.deaths += styleDeathsTotal(stats, row) - beforeRowDeaths;
    }
}

void printPctV311(std::uint64_t n, std::uint64_t d) {
    std::cout << std::fixed << std::setprecision(1)
              << (d ? 100.0 * static_cast<double>(n) / static_cast<double>(d) : 0.0) << '%';
}

void printV311Report(const Stats& stats, const EnragedLethalityStats& enraged,
                     const MatchupGridV311& grid, std::uint64_t maxRounds) {
    std::ostringstream captured;
    auto* old = std::cout.rdbuf(captured.rdbuf());
    printV310Report(stats, enraged, maxRounds);
    std::cout.rdbuf(old);

    std::string report = captured.str();
    const std::string oldLabel = "(BOT V3.10 ENRAGED LETHALITY TELEMETRY)";
    const std::string newLabel = "(BOT V3.11 PLAYSTYLE MATCHUP TELEMETRY)";
    if (const auto pos = report.find(oldLabel); pos != std::string::npos)
        report.replace(pos, oldLabel.size(), newLabel);
    std::cout << report;

    std::cout << "\nPLAYSTYLE MATCHUP TELEMETRY\n";
    std::cout << "Rows=P1 playstyle, columns=P2 playstyle. Win splits exclude positional splitting for mirror matches.\n\n";

    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            const auto& m = grid[r][c];
            if (!m.matches) continue;
            std::cout << styleName(static_cast<Playstyle>(r)) << " vs "
                      << styleName(static_cast<Playstyle>(c))
                      << " | matches=" << m.matches;
            if (r == c) {
                std::cout << " combinedWins=" << m.rowWins << " (";
                printPctV311(m.rowWins, m.matches);
                std::cout << ')';
            } else {
                std::cout << " P1wins=" << m.rowWins << " (";
                printPctV311(m.rowWins, m.matches);
                std::cout << ") P2wins=" << m.colWins << " (";
                printPctV311(m.colWins, m.matches);
                std::cout << ')';
            }
            std::cout << " basilisk=" << m.basiliskWins
                      << " extraction=" << m.extractionWins
                      << " simBasiliskDraw=" << m.simultaneousBasiliskDraws
                      << " otherDraw=" << m.otherDraws
                      << " stalled=" << m.stalled
                      << " pvpKills=" << m.pvpKills
                      << " deaths=" << m.deaths
                      << " avgRounds=" << std::fixed << std::setprecision(1)
                      << (m.matches ? static_cast<double>(m.rounds) / m.matches : 0.0)
                      << '\n';
        }
    }

    std::cout << "\nCROSS-PLAYSTYLE WIN-RATE MATRIX (P1 win %)\n";
    std::cout << std::setw(14) << "";
    for (std::size_t c = 0; c < 4; ++c) std::cout << std::setw(14) << styleName(static_cast<Playstyle>(c));
    std::cout << '\n';
    for (std::size_t r = 0; r < 4; ++r) {
        std::cout << std::setw(14) << styleName(static_cast<Playstyle>(r));
        for (std::size_t c = 0; c < 4; ++c) {
            const auto& m = grid[r][c];
            if (r == c || !m.matches) {
                std::cout << std::setw(14) << "mirror";
            } else {
                std::ostringstream value;
                value << std::fixed << std::setprecision(1)
                      << (100.0 * static_cast<double>(m.rowWins) / m.matches) << '%';
                std::cout << std::setw(14) << value.str();
            }
        }
        std::cout << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::uint64_t matches = argc > 1 ? std::stoull(argv[1]) : 1000;
    const std::uint64_t maxRounds = argc > 2 ? std::stoull(argv[2]) : 250;
    const std::uint64_t mapBase = argc > 3 ? std::stoull(argv[3]) : 100000;
    const std::uint64_t matchBase = argc > 4 ? std::stoull(argv[4]) : 500000;

    Stats stats;
    EnragedLethalityStats enraged;
    MatchupGridV311 matchups{};
    for (std::uint64_t i = 0; i < matches; ++i)
        runOneV311(mapBase + i, matchBase + i, maxRounds, stats, enraged, matchups);

    printV311Report(stats, enraged, matchups, maxRounds);
    return 0;
}
