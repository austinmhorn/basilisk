#include <array>
#include <iomanip>
#include <sstream>

// V3.13 includes the stable V3.9 simulator once, then replaces only bot decision
// selection and the match driver so Old Hunter's Map can be measured without
// creating another versioned-main include chain.
#define main basilisk_v39_main
#define chooseAction chooseActionV311Base
#include "main_v39.cpp"
#undef chooseAction
#undef main
#include "v310_enraged_telemetry.hpp"

namespace {

struct OldHuntersMapTelemetry {
    std::uint64_t found{0};
    std::uint64_t inventoryRejected{0};
    std::uint64_t reads{0};
    std::uint64_t totalTrueDistance{0};
    std::array<std::uint64_t, 4> distanceBands{}; // 0-2 / 3-5 / 6-8 / 9+
    std::array<std::uint64_t, 4> foundByStyle{};
    std::array<std::uint64_t, 4> readByStyle{};
    std::uint64_t encountersAfterRead{0};
    std::uint64_t roundsReadToEncounter{0};
};

struct OldHuntersMapMatchMemory {
    std::unordered_map<PlayerId, RoundNumber> lastReadRound;
};

struct MatchupCellV313 {
    std::uint64_t matches{0}, rowWins{0}, colWins{0};
    std::uint64_t basiliskWins{0}, extractionWins{0};
    std::uint64_t simultaneousBasiliskDraws{0}, otherDraws{0}, stalled{0};
    std::uint64_t pvpKills{0}, deaths{0}, rounds{0};
};
using MatchupGridV313 = std::array<std::array<MatchupCellV313, 4>, 4>;

std::optional<PlayerAction> chooseAction(
    Playstyle style,
    const PlayerRoundSnapshot& s,
    BotMemory& memory,
    MatchSeed matchSeed,
    Stats& stats,
    bool& stalenessMove) {

    // A hunter with ammunition but no immediate Basilisk clue can consult the
    // consumable map. Scavenger naturally benefits more because it searches and
    // therefore finds the item far more often.
    if (s.alive && s.arrows > 0 &&
        !hasAdjacentBasiliskClue(s) && !exactEnragedTarget(s).has_value()) {
        if (const auto* map = useItem(s, ItemType::OldHuntersMap)) {
            stalenessMove = false;
            return materialize(s.player, *map);
        }
    }

    // Preserve the V3.12 conversion experiment so this version measures the new
    // item on top of the current Scavenger decision policy.
    if (style == Playstyle::Scavenger && s.alive && s.health >= 75 &&
        !s.inventory.items.empty() && s.arrows == 1 && !memory.rivalDead &&
        hasObs(s, ObservationType::RivalNearby) && !hasAdjacentBasiliskClue(s) &&
        !exactEnragedTarget(s).has_value()) {
        const auto shots = actionsOfType(s, ActionType::Shoot);
        if (!shots.empty()) {
            const std::uint64_t salt = mix64(matchSeed ^
                (static_cast<std::uint64_t>(s.round) << 17U) ^
                static_cast<std::uint64_t>(s.player));
            if (const auto* shot = pick(shots, salt >> 3U)) {
                ++stats.pvpShots;
                stalenessMove = false;
                return materialize(s.player, *shot);
            }
        }
    }

    auto base = chooseActionV311Base(style, s, memory, matchSeed, stats, stalenessMove);
    if (!base.has_value() || style != Playstyle::Scavenger || base->type != ActionType::Search)
        return base;

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
    std::vector<const AvailableAction*> unknownMoves, knownMoves;
    for (const auto* move : safeMoves) {
        if (move->targetCave.has_value()) knownMoves.push_back(move);
        else unknownMoves.push_back(move);
    }
    const std::uint64_t salt = mix64(matchSeed ^
        (static_cast<std::uint64_t>(s.round) << 17U) ^ static_cast<std::uint64_t>(s.player));
    if (!unknownMoves.empty()) {
        if (const auto* move = pick(unknownMoves, salt >> 5U)) {
            ++stats.unexploredMoves;
            stalenessMove = false;
            return materialize(s.player, *move);
        }
    }
    if (const auto frontier = nearestFrontierStep(s); frontier.has_value() && *frontier != s.currentCave) {
        if (const auto* move = moveTo(s, *frontier)) {
            ++stats.frontierMoves; ++stats.knownMoves;
            stalenessMove = false;
            return materialize(s.player, *move);
        }
    }
    if (!knownMoves.empty()) {
        if (const auto* move = pick(knownMoves, salt >> 11U)) {
            ++stats.knownMoves;
            stalenessMove = false;
            return materialize(s.player, *move);
        }
    }
    return base;
}

void collectOldHuntersMapEvents(
    const std::vector<GameEvent>& events,
    RoundNumber round,
    const std::unordered_map<PlayerId, Playstyle>& styles,
    OldHuntersMapMatchMemory& memory,
    OldHuntersMapTelemetry& telemetry) {

    for (const auto& event : events) {
        if (event.type == GameEventType::OldHuntersMapFound && event.actor.has_value()) {
            ++telemetry.found;
            const auto it = styles.find(*event.actor);
            if (it != styles.end()) ++telemetry.foundByStyle[static_cast<std::size_t>(it->second)];
        } else if (event.type == GameEventType::InventoryFull &&
                   event.itemType == ItemType::OldHuntersMap) {
            ++telemetry.inventoryRejected;
        } else if (event.type == GameEventType::OldHuntersMapRead && event.actor.has_value()) {
            ++telemetry.reads;
            telemetry.totalTrueDistance += static_cast<std::uint64_t>(std::max(0, event.amount));
            const int d = std::max(0, event.amount);
            const std::size_t band = d <= 2 ? 0 : d <= 5 ? 1 : d <= 8 ? 2 : 3;
            ++telemetry.distanceBands[band];
            memory.lastReadRound[*event.actor] = round;
            const auto it = styles.find(*event.actor);
            if (it != styles.end()) ++telemetry.readByStyle[static_cast<std::size_t>(it->second)];
        } else if (event.type == GameEventType::ArrowReachedBasilisk && event.actor.has_value()) {
            const auto it = memory.lastReadRound.find(*event.actor);
            if (it != memory.lastReadRound.end()) {
                ++telemetry.encountersAfterRead;
                telemetry.roundsReadToEncounter += round >= it->second
                    ? static_cast<std::uint64_t>(round - it->second) : 0ULL;
                memory.lastReadRound.erase(it);
            }
        }
    }
}

void runOneV313(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
                Stats& stats, EnragedLethalityStats& enragedTelemetry,
                OldHuntersMapTelemetry& mapTelemetry, MatchupGridV313& grid) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, Playstyle> styles;
    std::unordered_map<PlayerId, BotMemory> memories;
    std::unordered_set<PlayerId> pitDeadPlayers;
    std::vector<GameEvent> previousEvents;
    HuntTiming huntTiming;
    PersonalityMatchTracker personalityTracker;
    personalityTracker.active = state.basilisk.behavior;
    EnragedMatchTracker enragedTracker;
    OldHuntersMapMatchMemory mapMemory;
    bool countedSecond = false, countedThird = false;

    for (const auto& player : state.players) {
        const Playstyle style = styleFor(matchSeed, player.id);
        styles[player.id] = style;
        ++stats.style[static_cast<std::size_t>(style)].assignments;
    }
    const auto row = static_cast<std::size_t>(styles[state.players[0].id]);
    const auto col = static_cast<std::size_t>(styles[state.players[1].id]);
    ++stats.matchups[row][col];

    const auto beforeStalled = stats.stalled;
    const auto beforeBasilisk = stats.basiliskWins;
    const auto beforeExtraction = stats.extractionWins;
    const auto beforeSim = stats.simultaneousBasiliskDraws;
    const auto beforeDraws = stats.draws;
    const auto beforeRounds = stats.totalRounds;
    const auto beforeRowWins = stats.style[row].wins;
    const auto beforeColWins = stats.style[col].wins;
    const auto beforeRowPvp = stats.style[row].pvpKills;
    const auto beforeColPvp = stats.style[col].pvpKills;
    const auto beforeRowDeaths = stats.style[row].deaths;
    const auto beforeColDeaths = stats.style[col].deaths;

    while (state.result.status == MatchStatus::Active && state.round <= maxRounds) {
        if (const auto index = personalityIndex(personalityTracker.active); index.has_value())
            ++stats.personality[*index].activeRounds;

        std::vector<PlayerAction> selected;
        std::unordered_set<PlayerId> zeroBefore;
        std::unordered_set<PlayerId> stalenessMovers;
        for (const auto& player : state.players) {
            if (!player.alive) continue;
            ++stats.style[static_cast<std::size_t>(styles[player.id])].roundsAlive;
            const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
            if (snapshot.arrows == 0) zeroBefore.insert(player.id);
            if (hasObs(snapshot, ObservationType::PitNearby)) ++stats.pitWarnings;
            bool stalenessMove = false;
            const auto action = chooseAction(styles[player.id], snapshot, memories[player.id], matchSeed, stats, stalenessMove);
            if (!action.has_value()) continue;
            selected.push_back(*action);
            auto& ss = stats.style[static_cast<std::size_t>(styles[player.id])];
            if (action->type == ActionType::Search) ++ss.searches;
            if (action->type == ActionType::Shoot) {
                ++ss.shots;
                if (hasObs(snapshot, ObservationType::RivalNearby)) ++ss.pvpShots;
            }
            if (stalenessMove) stalenessMovers.insert(player.id);
        }

        if (selected.empty()) break;
        bool submitOk = true;
        for (const auto& action : selected) submitOk &= coordinator.submitAction(action);
        if (!submitOk) break;
        bool lockOk = true;
        for (const auto& action : selected) lockOk &= coordinator.lockAction(action.player);
        if (!lockOk) break;

        previousEvents = coordinator.lastEvents();
        collectEvents(previousEvents, state, stats, styles, pitDeadPlayers, zeroBefore,
            stalenessMovers, memories, huntTiming, personalityTracker);
        collectEnragedEvents(previousEvents, state, enragedTracker, enragedTelemetry);
        collectOldHuntersMapEvents(previousEvents, state.round, styles, mapMemory, mapTelemetry);

        if (!countedSecond && state.basilisk.trueEncounters >= 2) { ++stats.secondEncounterMatches; countedSecond = true; }
        if (!countedThird && state.basilisk.trueEncounters >= 3) { ++stats.thirdEncounterMatches; countedThird = true; }
    }

    ++stats.matches;
    ++stats.personalityChangesPerMatch[std::min<std::size_t>(3, static_cast<std::size_t>(personalityTracker.changes))];
    const auto rounds = std::min<std::uint64_t>(state.round, maxRounds);
    stats.totalRounds += rounds;
    stats.roundSamples.push_back(rounds);
    for (const auto& player : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
        stats.totalCaves += snapshot.map.caves.size();
        stats.totalFinalArrows += static_cast<std::uint64_t>(std::max(0, player.arrows));
    }

    if (const auto index = personalityIndex(personalityTracker.active); index.has_value()) {
        auto& ps = stats.personality[*index]; ++ps.matchesEnded;
        if (state.result.status == MatchStatus::Completed) {
            switch (state.result.outcome) {
                case MatchOutcome::BasiliskKilled:
                case MatchOutcome::SimultaneousBasiliskKill: ++ps.basiliskDefeatEnds; break;
                case MatchOutcome::EscapedWithSigil: ++ps.extractionEnds; break;
                case MatchOutcome::Draw: ++ps.drawEnds; break;
                case MatchOutcome::None: break;
            }
        }
    }

    if (state.result.status != MatchStatus::Completed) {
        ++stats.stalled;
        diagnoseStall(state, previousEvents, stats);
        finalizeEnragedMatch(state, enragedTracker, enragedTelemetry);
    } else {
        if (state.result.outcome == MatchOutcome::BasiliskKilled || state.result.outcome == MatchOutcome::SimultaneousBasiliskKill) {
            const auto encounter = static_cast<std::size_t>(std::clamp(state.basilisk.trueEncounters, 1, 3));
            ++stats.basiliskDeathMatchesByEncounter[encounter];
            stats.basiliskDeathRoundsByEncounter[encounter] += rounds;
        }
        ++stats.completed;
        switch (state.result.outcome) {
            case MatchOutcome::BasiliskKilled:
                ++stats.basiliskWins;
                if (state.result.winner.has_value()) { auto& ss = stats.style[static_cast<std::size_t>(styles[*state.result.winner])]; ++ss.wins; ++ss.basiliskWins; }
                break;
            case MatchOutcome::SimultaneousBasiliskKill: ++stats.simultaneousBasiliskDraws; break;
            case MatchOutcome::EscapedWithSigil:
                ++stats.extractionWins;
                if (state.result.winner.has_value()) { auto& ss = stats.style[static_cast<std::size_t>(styles[*state.result.winner])]; ++ss.wins; ++ss.extractionWins; }
                break;
            case MatchOutcome::Draw: ++stats.draws; if (pitDeadPlayers.size() >= 2) ++stats.mutualPitDraws; break;
            case MatchOutcome::None: break;
        }
        finalizeEnragedMatch(state, enragedTracker, enragedTelemetry);
    }

    auto& cell = grid[row][col];
    ++cell.matches;
    cell.rounds += stats.totalRounds - beforeRounds;
    cell.basiliskWins += stats.basiliskWins - beforeBasilisk;
    cell.extractionWins += stats.extractionWins - beforeExtraction;
    cell.simultaneousBasiliskDraws += stats.simultaneousBasiliskDraws - beforeSim;
    cell.otherDraws += stats.draws - beforeDraws;
    cell.stalled += stats.stalled - beforeStalled;
    if (row != col) {
        cell.rowWins += stats.style[row].wins - beforeRowWins;
        cell.colWins += stats.style[col].wins - beforeColWins;
        cell.pvpKills += (stats.style[row].pvpKills - beforeRowPvp) + (stats.style[col].pvpKills - beforeColPvp);
        cell.deaths += (stats.style[row].deaths - beforeRowDeaths) + (stats.style[col].deaths - beforeColDeaths);
    } else {
        cell.rowWins += stats.style[row].wins - beforeRowWins;
        cell.pvpKills += stats.style[row].pvpKills - beforeRowPvp;
        cell.deaths += stats.style[row].deaths - beforeRowDeaths;
    }
}

void printPctV313(std::uint64_t n, std::uint64_t d) {
    std::cout << std::fixed << std::setprecision(1)
              << (d ? 100.0 * static_cast<double>(n) / static_cast<double>(d) : 0.0) << '%';
}

void printV313Report(const Stats& stats, const EnragedLethalityStats& enraged,
                     const OldHuntersMapTelemetry& map, const MatchupGridV313& grid,
                     std::uint64_t maxRounds) {
    std::ostringstream captured;
    auto* old = std::cout.rdbuf(captured.rdbuf());
    printV310Report(stats, enraged, maxRounds);
    std::cout.rdbuf(old);
    std::string report = captured.str();
    const std::string oldLabel = "(BOT V3.10 ENRAGED LETHALITY TELEMETRY)";
    const std::string newLabel = "(BOT V3.13 OLD HUNTER'S MAP)";
    if (const auto pos = report.find(oldLabel); pos != std::string::npos) report.replace(pos, oldLabel.size(), newLabel);
    std::cout << report;

    std::cout << "\nOLD HUNTER'S MAP TELEMETRY\n";
    std::cout << "Maps found / inventory-full rejected: " << map.found << '/' << map.inventoryRejected << '\n';
    std::cout << "Maps read: " << map.reads << '\n';
    std::cout << "Average true Basilisk distance when read: " << std::fixed << std::setprecision(1)
              << (map.reads ? static_cast<double>(map.totalTrueDistance) / map.reads : 0.0) << " caves\n";
    std::cout << "Read distance bands 0-2 / 3-5 / 6-8 / 9+: "
              << map.distanceBands[0] << '/' << map.distanceBands[1] << '/'
              << map.distanceBands[2] << '/' << map.distanceBands[3] << '\n';
    std::cout << "Found by Hunter/Scavenger/Opportunist/Extractor: "
              << map.foundByStyle[0] << '/' << map.foundByStyle[1] << '/'
              << map.foundByStyle[2] << '/' << map.foundByStyle[3] << '\n';
    std::cout << "Read by Hunter/Scavenger/Opportunist/Extractor: "
              << map.readByStyle[0] << '/' << map.readByStyle[1] << '/'
              << map.readByStyle[2] << '/' << map.readByStyle[3] << '\n';
    std::cout << "True encounters after a map reading: " << map.encountersAfterRead << '\n';
    std::cout << "Average rounds map reading -> true encounter: " << std::fixed << std::setprecision(1)
              << (map.encountersAfterRead ? static_cast<double>(map.roundsReadToEncounter) / map.encountersAfterRead : 0.0) << '\n';

    std::cout << "\nPLAYSTYLE MATCHUP TELEMETRY\n";
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            const auto& m = grid[r][c]; if (!m.matches) continue;
            std::cout << styleName(static_cast<Playstyle>(r)) << " vs " << styleName(static_cast<Playstyle>(c))
                      << " | matches=" << m.matches;
            if (r == c) { std::cout << " combinedWins=" << m.rowWins << " ("; printPctV313(m.rowWins, m.matches); std::cout << ')'; }
            else { std::cout << " P1wins=" << m.rowWins << " ("; printPctV313(m.rowWins, m.matches); std::cout << ") P2wins=" << m.colWins << " ("; printPctV313(m.colWins, m.matches); std::cout << ')'; }
            std::cout << " basilisk=" << m.basiliskWins << " extraction=" << m.extractionWins
                      << " simBasiliskDraw=" << m.simultaneousBasiliskDraws << " otherDraw=" << m.otherDraws
                      << " stalled=" << m.stalled << " pvpKills=" << m.pvpKills << " deaths=" << m.deaths
                      << " avgRounds=" << std::fixed << std::setprecision(1)
                      << (m.matches ? static_cast<double>(m.rounds) / m.matches : 0.0) << '\n';
        }
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
    OldHuntersMapTelemetry mapTelemetry;
    MatchupGridV313 matchups{};
    for (std::uint64_t i = 0; i < matches; ++i)
        runOneV313(mapBase + i, matchBase + i, maxRounds, stats, enraged, mapTelemetry, matchups);
    printV313Report(stats, enraged, mapTelemetry, matchups, maxRounds);
    return 0;
}
