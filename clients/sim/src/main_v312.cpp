#include <array>
#include <iomanip>
#include <sstream>

// V3.12 keeps the V3.11 simulation/telemetry baseline, but intercepts only
// Scavenger decisions so we can test whether better resource conversion closes
// its win-rate gap without changing core rules or the other playstyles.
#define main basilisk_v39_main
#define chooseAction chooseActionV311Base
#include "main_v39.cpp"
#undef chooseAction
#undef main

namespace {

struct ScavengerDecisionStatsV312 {
    std::uint64_t aggressivePvpShots{0};
    std::uint64_t oneArrowSearchesConverted{0};
    std::uint64_t convertedToUnexploredMove{0};
    std::uint64_t convertedToFrontierMove{0};
    std::uint64_t convertedToKnownMove{0};
};

ScavengerDecisionStatsV312 gScavengerV312;

std::optional<PlayerAction> chooseAction(
    Playstyle style,
    const PlayerRoundSnapshot& s,
    BotMemory& memory,
    MatchSeed matchSeed,
    Stats& stats,
    bool& stalenessMove) {

    // Conversion rule A: once a Scavenger has at least one useful item and is
    // healthy, one remaining arrow is no longer protected from a clean PvP
    // opportunity. Opportunist already behaves this way; this lets a stocked
    // Scavenger actually cash in its survival/resource advantage.
    if (style == Playstyle::Scavenger && s.alive && s.health >= 75 &&
        !s.inventory.items.empty() && s.arrows == 1 &&
        !memory.rivalDead && hasObs(s, ObservationType::RivalNearby) &&
        !hasAdjacentBasiliskClue(s) && !exactEnragedTarget(s).has_value()) {
        const auto shots = actionsOfType(s, ActionType::Shoot);
        if (!shots.empty()) {
            const std::uint64_t salt = mix64(matchSeed ^
                (static_cast<std::uint64_t>(s.round) << 17U) ^
                static_cast<std::uint64_t>(s.player));
            if (const auto* shot = pick(shots, salt >> 3U)) {
                ++stats.pvpShots;
                ++gScavengerV312.aggressivePvpShots;
                stalenessMove = false;
                return materialize(s.player, *shot);
            }
        }
    }

    auto base = chooseActionV311Base(style, s, memory, matchSeed, stats, stalenessMove);
    if (!base.has_value() || style != Playstyle::Scavenger ||
        base->type != ActionType::Search) return base;

    // Conversion rule B: V3.11 treats one arrow as a resource shortage, which
    // causes Scavenger to spend a huge number of turns searching despite already
    // owning useful inventory. When healthy + equipped, convert that ordinary
    // one-arrow search into forward movement. Preserve pit investigations,
    // objective/body searches, emergency health searches, and true zero-ammo
    // recovery behavior.
    const bool conversionEligible = s.arrows == 1 && s.health > 50 &&
        !s.inventory.items.empty() && !memory.rivalDead && !s.hasHunterSigil &&
        !hasObs(s, ObservationType::PitNearby);
    if (!conversionEligible) return base;

    const auto moves = actionsOfType(s, ActionType::Move);
    std::vector<const AvailableAction*> safeMoves;
    const auto pitTunnel = investigatedPitTunnel(s);
    for (const auto* move : moves) {
        if (pitTunnel.has_value() && actionUsesTunnel(*move, *pitTunnel)) continue;
        safeMoves.push_back(move);
    }

    std::vector<const AvailableAction*> unknownMoves;
    std::vector<const AvailableAction*> knownMoves;
    for (const auto* move : safeMoves) {
        if (move->targetCave.has_value()) knownMoves.push_back(move);
        else unknownMoves.push_back(move);
    }

    const std::uint64_t salt = mix64(matchSeed ^
        (static_cast<std::uint64_t>(s.round) << 17U) ^
        static_cast<std::uint64_t>(s.player));

    if (!unknownMoves.empty()) {
        if (const auto* move = pick(unknownMoves, salt >> 5U)) {
            ++stats.unexploredMoves;
            ++gScavengerV312.oneArrowSearchesConverted;
            ++gScavengerV312.convertedToUnexploredMove;
            stalenessMove = false;
            return materialize(s.player, *move);
        }
    }

    if (const auto frontier = nearestFrontierStep(s);
        frontier.has_value() && *frontier != s.currentCave) {
        if (const auto* move = moveTo(s, *frontier)) {
            ++stats.frontierMoves;
            ++stats.knownMoves;
            ++gScavengerV312.oneArrowSearchesConverted;
            ++gScavengerV312.convertedToFrontierMove;
            stalenessMove = false;
            return materialize(s.player, *move);
        }
    }

    if (!knownMoves.empty()) {
        if (const auto* move = pick(knownMoves, salt >> 11U)) {
            ++stats.knownMoves;
            ++gScavengerV312.oneArrowSearchesConverted;
            ++gScavengerV312.convertedToKnownMove;
            stalenessMove = false;
            return materialize(s.player, *move);
        }
    }

    return base;
}

} // namespace

#include "v310_enraged_telemetry.hpp"

namespace {

struct MatchupCellV312 {
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

using MatchupGridV312 = std::array<std::array<MatchupCellV312, 4>, 4>;

std::uint64_t styleWinsTotalV312(const Stats& s, std::size_t i) { return s.style[i].wins; }
std::uint64_t stylePvpKillsTotalV312(const Stats& s, std::size_t i) { return s.style[i].pvpKills; }
std::uint64_t styleDeathsTotalV312(const Stats& s, std::size_t i) { return s.style[i].deaths; }

void runOneV312(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
                Stats& stats, EnragedLethalityStats& enraged, MatchupGridV312& grid) {
    const auto row = static_cast<std::size_t>(styleFor(matchSeed, 1));
    const auto col = static_cast<std::size_t>(styleFor(matchSeed, 2));

    const auto beforeStalled = stats.stalled;
    const auto beforeBasilisk = stats.basiliskWins;
    const auto beforeExtraction = stats.extractionWins;
    const auto beforeSim = stats.simultaneousBasiliskDraws;
    const auto beforeDraws = stats.draws;
    const auto beforeRounds = stats.totalRounds;
    const auto beforeRowWins = styleWinsTotalV312(stats, row);
    const auto beforeColWins = styleWinsTotalV312(stats, col);
    const auto beforeRowPvp = stylePvpKillsTotalV312(stats, row);
    const auto beforeColPvp = stylePvpKillsTotalV312(stats, col);
    const auto beforeRowDeaths = styleDeathsTotalV312(stats, row);
    const auto beforeColDeaths = styleDeathsTotalV312(stats, col);

    runOneV310(mapSeed, matchSeed, maxRounds, stats, enraged);

    auto& cell = grid[row][col];
    ++cell.matches;
    cell.rounds += stats.totalRounds - beforeRounds;
    cell.basiliskWins += stats.basiliskWins - beforeBasilisk;
    cell.extractionWins += stats.extractionWins - beforeExtraction;
    cell.simultaneousBasiliskDraws += stats.simultaneousBasiliskDraws - beforeSim;
    cell.otherDraws += stats.draws - beforeDraws;
    cell.stalled += stats.stalled - beforeStalled;

    if (row != col) {
        cell.rowWins += styleWinsTotalV312(stats, row) - beforeRowWins;
        cell.colWins += styleWinsTotalV312(stats, col) - beforeColWins;
        cell.pvpKills += (stylePvpKillsTotalV312(stats, row) - beforeRowPvp)
                       + (stylePvpKillsTotalV312(stats, col) - beforeColPvp);
        cell.deaths += (styleDeathsTotalV312(stats, row) - beforeRowDeaths)
                     + (styleDeathsTotalV312(stats, col) - beforeColDeaths);
    } else {
        cell.rowWins += styleWinsTotalV312(stats, row) - beforeRowWins;
        cell.pvpKills += stylePvpKillsTotalV312(stats, row) - beforeRowPvp;
        cell.deaths += styleDeathsTotalV312(stats, row) - beforeRowDeaths;
    }
}

void printPctV312(std::uint64_t n, std::uint64_t d) {
    std::cout << std::fixed << std::setprecision(1)
              << (d ? 100.0 * static_cast<double>(n) / static_cast<double>(d) : 0.0) << '%';
}

void printV312Report(const Stats& stats, const EnragedLethalityStats& enraged,
                     const MatchupGridV312& grid, std::uint64_t maxRounds) {
    std::ostringstream captured;
    auto* old = std::cout.rdbuf(captured.rdbuf());
    printV310Report(stats, enraged, maxRounds);
    std::cout.rdbuf(old);

    std::string report = captured.str();
    const std::string oldLabel = "(BOT V3.10 ENRAGED LETHALITY TELEMETRY)";
    const std::string newLabel = "(BOT V3.12 SCAVENGER CONVERSION)";
    if (const auto pos = report.find(oldLabel); pos != std::string::npos)
        report.replace(pos, oldLabel.size(), newLabel);
    std::cout << report;

    std::cout << "\nV3.12 SCAVENGER DECISION TELEMETRY\n"
              << "Stocked one-arrow PvP shots enabled: " << gScavengerV312.aggressivePvpShots << '\n'
              << "One-arrow searches converted to movement: " << gScavengerV312.oneArrowSearchesConverted << '\n'
              << "Converted to unexplored/frontier/known moves: "
              << gScavengerV312.convertedToUnexploredMove << '/'
              << gScavengerV312.convertedToFrontierMove << '/'
              << gScavengerV312.convertedToKnownMove << "\n";

    std::cout << "\nPLAYSTYLE MATCHUP TELEMETRY\n";
    std::cout << "Rows=P1 playstyle, columns=P2 playstyle. Win splits exclude positional splitting for mirror matches.\n\n";
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            const auto& m = grid[r][c];
            if (!m.matches) continue;
            std::cout << styleName(static_cast<Playstyle>(r)) << " vs "
                      << styleName(static_cast<Playstyle>(c)) << " | matches=" << m.matches;
            if (r == c) {
                std::cout << " combinedWins=" << m.rowWins << " (";
                printPctV312(m.rowWins, m.matches);
                std::cout << ')';
            } else {
                std::cout << " P1wins=" << m.rowWins << " (";
                printPctV312(m.rowWins, m.matches);
                std::cout << ") P2wins=" << m.colWins << " (";
                printPctV312(m.colWins, m.matches);
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
    for (std::size_t c = 0; c < 4; ++c)
        std::cout << std::setw(14) << styleName(static_cast<Playstyle>(c));
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

    gScavengerV312 = {};
    Stats stats;
    EnragedLethalityStats enraged;
    MatchupGridV312 matchups{};
    for (std::uint64_t i = 0; i < matches; ++i)
        runOneV312(mapBase + i, matchBase + i, maxRounds, stats, enraged, matchups);

    printV312Report(stats, enraged, matchups, maxRounds);
    return 0;
}
