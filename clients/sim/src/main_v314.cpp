#include <array>
#include <iomanip>
#include <sstream>

// V3.14 keeps one simulator baseline include and layers Old Hunter's Map
// intelligence on top. No versioned-main include chain.
#define main basilisk_v39_main
#define chooseAction chooseActionV311Base
#include "main_v39.cpp"
#undef chooseAction
#undef main
#include "v310_enraged_telemetry.hpp"

namespace {

struct MapIntelStateV314 {
    bool active{false};
    CaveId origin{};
    int minDistance{0};
    int maxDistance{0};
    RoundNumber readRound{0};
    std::optional<CaveId> target;
};

struct OldHuntersMapTelemetryV314 {
    std::uint64_t found{0};
    std::uint64_t inventoryRejected{0};
    std::uint64_t useActionsSelected{0};
    std::uint64_t reads{0};
    std::uint64_t totalTrueDistance{0};
    std::array<std::uint64_t, 4> distanceBands{}; // 0-2 / 3-5 / 6-8 / 9+
    std::array<std::uint64_t, 4> foundByStyle{};
    std::array<std::uint64_t, 4> readByStyle{};
    std::array<std::uint64_t, 4> intelligenceMovesByStyle{};
    std::uint64_t intelligenceActivations{0};
    std::uint64_t intelligenceTargetsSelected{0};
    std::uint64_t intelligenceMoves{0};
    std::uint64_t intelligenceTargetsReached{0};
    std::uint64_t invalidatedByBasiliskMove{0};
    std::uint64_t invalidatedByEncounter{0};
    std::uint64_t encountersAfterRead{0};
    std::uint64_t roundsReadToEncounter{0};
};

struct MatchupCellV314 {
    std::uint64_t matches{0}, rowWins{0}, colWins{0};
    std::uint64_t basiliskWins{0}, extractionWins{0};
    std::uint64_t simultaneousBasiliskDraws{0}, otherDraws{0}, stalled{0};
    std::uint64_t pvpKills{0}, deaths{0}, rounds{0};
};
using MatchupGridV314 = std::array<std::array<MatchupCellV314, 4>, 4>;

std::unordered_map<PlayerId, MapIntelStateV314> gMapIntelV314;
OldHuntersMapTelemetryV314* gMapTelemetryV314 = nullptr;
const std::unordered_map<PlayerId, Playstyle>* gStylesV314 = nullptr;

std::optional<int> safeKnownDistanceBetweenV314(
    const PlayerRoundSnapshot& s, CaveId start, CaveId target) {
    if (start == target) return 0;
    if (!caveView(s, start) || !caveView(s, target)) return std::nullopt;
    std::queue<std::pair<CaveId, int>> q;
    std::unordered_set<CaveId> seen;
    q.push({start, 0});
    seen.insert(start);
    while (!q.empty()) {
        const auto [cur, distance] = q.front(); q.pop();
        const auto* view = caveView(s, cur);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!safeKnownConnection(s, cur, tunnel)) continue;
            const CaveId next = *tunnel.destination;
            if (!seen.insert(next).second) continue;
            if (next == target) return distance + 1;
            q.push({next, distance + 1});
        }
    }
    return std::nullopt;
}

std::optional<CaveId> chooseMapShellTargetV314(
    const PlayerRoundSnapshot& s,
    const BotMemory& memory,
    const MapIntelStateV314& intel,
    MatchSeed matchSeed) {

    struct Candidate {
        CaveId cave{};
        int visits{0};
        bool frontier{false};
    };
    std::vector<Candidate> candidates;
    for (const auto& cave : s.map.caves) {
        const auto distance = safeKnownDistanceBetweenV314(s, intel.origin, cave.cave);
        if (!distance.has_value() || *distance < intel.minDistance || *distance > intel.maxDistance)
            continue;
        const auto it = memory.caves.find(cave.cave);
        const int visits = it == memory.caves.end() ? 0 : it->second.visits;
        candidates.push_back(Candidate{cave.cave, visits, caveHasMeaningfulFrontier(cave)});
    }
    if (candidates.empty()) return std::nullopt;

    const auto best = std::min_element(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            if (a.frontier != b.frontier) return a.frontier > b.frontier;
            if (a.visits != b.visits) return a.visits < b.visits;
            return a.cave < b.cave;
        });
    if (best == candidates.end()) return std::nullopt;

    // Randomize among candidates equally good on frontier + visit count so the
    // fuzzy distance band does not collapse into deterministic cave-id bias.
    std::vector<CaveId> tied;
    for (const auto& candidate : candidates) {
        if (candidate.frontier == best->frontier && candidate.visits == best->visits)
            tied.push_back(candidate.cave);
    }
    const std::uint64_t salt = mix64(matchSeed ^
        (static_cast<std::uint64_t>(s.round) << 21U) ^
        static_cast<std::uint64_t>(s.player) ^ 0x314314ULL);
    return tied[static_cast<std::size_t>(salt % tied.size())];
}

std::optional<PlayerAction> chooseAction(
    Playstyle style,
    const PlayerRoundSnapshot& s,
    BotMemory& memory,
    MatchSeed matchSeed,
    Stats& stats,
    bool& stalenessMove) {

    auto& intel = gMapIntelV314[s.player];

    // Immediate Basilisk information and extraction/objective state outrank a
    // fuzzy map clue. Otherwise, actually read the map as soon as one is usable.
    if (s.alive && !s.hasHunterSigil && !memory.rivalDead &&
        !hasAdjacentBasiliskClue(s) && !exactEnragedTarget(s).has_value() &&
        !intel.active) {
        if (const auto* map = useItem(s, ItemType::OldHuntersMap)) {
            if (gMapTelemetryV314) ++gMapTelemetryV314->useActionsSelected;
            stalenessMove = false;
            return materialize(s.player, *map);
        }
    }

    // Once a fuzzy reading exists, move through the known-safe graph toward a
    // plausible shell cave. Prefer frontier/low-visit candidates. If the known
    // graph cannot yet express the shell, continue toward meaningful frontier.
    if (intel.active && s.alive && !s.hasHunterSigil && !memory.rivalDead &&
        !hasAdjacentBasiliskClue(s) && !exactEnragedTarget(s).has_value()) {
        if (!intel.target.has_value() || !caveView(s, *intel.target)) {
            intel.target = chooseMapShellTargetV314(s, memory, intel, matchSeed);
            if (intel.target.has_value() && gMapTelemetryV314)
                ++gMapTelemetryV314->intelligenceTargetsSelected;
        }

        if (intel.target.has_value() && s.currentCave == *intel.target) {
            if (gMapTelemetryV314) ++gMapTelemetryV314->intelligenceTargetsReached;
            intel.target.reset();
        }

        if (intel.target.has_value()) {
            if (const auto step = safeStepTo(s, *intel.target);
                step.has_value() && *step != s.currentCave) {
                if (const auto* move = moveTo(s, *step)) {
                    ++stats.knownMoves;
                    if (gMapTelemetryV314) {
                        ++gMapTelemetryV314->intelligenceMoves;
                        ++gMapTelemetryV314->intelligenceMovesByStyle[static_cast<std::size_t>(style)];
                    }
                    stalenessMove = false;
                    return materialize(s.player, *move);
                }
            }
        }

        if (const auto frontier = nearestFrontierStep(s);
            frontier.has_value() && *frontier != s.currentCave) {
            if (const auto* move = moveTo(s, *frontier)) {
                ++stats.frontierMoves;
                ++stats.knownMoves;
                if (gMapTelemetryV314) {
                    ++gMapTelemetryV314->intelligenceMoves;
                    ++gMapTelemetryV314->intelligenceMovesByStyle[static_cast<std::size_t>(style)];
                }
                stalenessMove = false;
                return materialize(s.player, *move);
            }
        }

        const auto moves = actionsOfType(s, ActionType::Move);
        std::vector<const AvailableAction*> safeUnknown;
        const auto pitTunnel = investigatedPitTunnel(s);
        for (const auto* move : moves) {
            if (move->targetCave.has_value()) continue;
            if (pitTunnel.has_value() && actionUsesTunnel(*move, *pitTunnel)) continue;
            safeUnknown.push_back(move);
        }
        if (!safeUnknown.empty()) {
            const std::uint64_t salt = mix64(matchSeed ^
                (static_cast<std::uint64_t>(s.round) << 13U) ^
                static_cast<std::uint64_t>(s.player));
            if (const auto* move = pick(safeUnknown, salt)) {
                ++stats.unexploredMoves;
                if (gMapTelemetryV314) {
                    ++gMapTelemetryV314->intelligenceMoves;
                    ++gMapTelemetryV314->intelligenceMovesByStyle[static_cast<std::size_t>(style)];
                }
                stalenessMove = false;
                return materialize(s.player, *move);
            }
        }
    }

    // Preserve the V3.12 Scavenger conversion behavior.
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
    if (const auto frontier = nearestFrontierStep(s);
        frontier.has_value() && *frontier != s.currentCave) {
        if (const auto* move = moveTo(s, *frontier)) {
            ++stats.frontierMoves;
            ++stats.knownMoves;
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

void collectOldHuntersMapEventsV314(
    const std::vector<GameEvent>& events,
    RoundNumber round,
    const std::unordered_map<PlayerId, Playstyle>& styles,
    OldHuntersMapTelemetryV314& telemetry) {

    for (const auto& event : events) {
        if (event.type == GameEventType::OldHuntersMapFound && event.actor.has_value()) {
            ++telemetry.found;
            const auto it = styles.find(*event.actor);
            if (it != styles.end())
                ++telemetry.foundByStyle[static_cast<std::size_t>(it->second)];
            continue;
        }
        if (event.type == GameEventType::InventoryFull &&
            event.itemType == ItemType::OldHuntersMap) {
            ++telemetry.inventoryRejected;
            continue;
        }
        if (event.type == GameEventType::OldHuntersMapRead && event.actor.has_value()) {
            ++telemetry.reads;
            ++telemetry.intelligenceActivations;
            const int d = std::max(0, event.amount);
            telemetry.totalTrueDistance += static_cast<std::uint64_t>(d);
            const std::size_t band = d <= 2 ? 0 : d <= 5 ? 1 : d <= 8 ? 2 : 3;
            ++telemetry.distanceBands[band];
            const auto style = styles.find(*event.actor);
            if (style != styles.end())
                ++telemetry.readByStyle[static_cast<std::size_t>(style->second)];

            auto& intel = gMapIntelV314[*event.actor];
            intel.active = true;
            intel.origin = event.cave.value_or(CaveId{});
            intel.minDistance = std::max(0, d - 1);
            intel.maxDistance = d + 1;
            intel.readRound = round;
            intel.target.reset();
            continue;
        }
        if (event.type == GameEventType::ArrowReachedBasilisk && event.actor.has_value()) {
            auto it = gMapIntelV314.find(*event.actor);
            if (it != gMapIntelV314.end() && it->second.active) {
                ++telemetry.encountersAfterRead;
                ++telemetry.invalidatedByEncounter;
                telemetry.roundsReadToEncounter += round >= it->second.readRound
                    ? static_cast<std::uint64_t>(round - it->second.readRound) : 0ULL;
                it->second = {};
            }
            continue;
        }
        if (event.type == GameEventType::BasiliskMoved) {
            for (auto& [player, intel] : gMapIntelV314) {
                (void)player;
                if (!intel.active) continue;
                ++telemetry.invalidatedByBasiliskMove;
                intel = {};
            }
        }
    }
}

void runOneV314(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
                Stats& stats, EnragedLethalityStats& enragedTelemetry,
                OldHuntersMapTelemetryV314& mapTelemetry, MatchupGridV314& grid) {
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
    bool countedSecond = false, countedThird = false;

    gMapIntelV314.clear();
    gMapTelemetryV314 = &mapTelemetry;
    gStylesV314 = &styles;

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
            const auto action = chooseAction(styles[player.id], snapshot, memories[player.id],
                                             matchSeed, stats, stalenessMove);
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

        previousEvents = coordinator.authoritativeEvents();
        collectEvents(previousEvents, state, stats, styles, pitDeadPlayers, zeroBefore,
                      stalenessMovers, memories, huntTiming, personalityTracker);
        collectEnragedEvents(previousEvents, state, enragedTracker, enragedTelemetry);
        collectOldHuntersMapEventsV314(previousEvents, state.round, styles, mapTelemetry);

        if (!countedSecond && state.basilisk.trueEncounters >= 2) {
            ++stats.secondEncounterMatches; countedSecond = true;
        }
        if (!countedThird && state.basilisk.trueEncounters >= 3) {
            ++stats.thirdEncounterMatches; countedThird = true;
        }
    }

    ++stats.matches;
    ++stats.personalityChangesPerMatch[std::min<std::size_t>(
        3, static_cast<std::size_t>(personalityTracker.changes))];
    const auto rounds = std::min<std::uint64_t>(state.round, maxRounds);
    stats.totalRounds += rounds;
    stats.roundSamples.push_back(rounds);
    for (const auto& player : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
        stats.totalCaves += snapshot.map.caves.size();
        stats.totalFinalArrows += static_cast<std::uint64_t>(std::max(0, player.arrows));
    }

    if (const auto index = personalityIndex(personalityTracker.active); index.has_value()) {
        auto& ps = stats.personality[*index];
        ++ps.matchesEnded;
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
        if (state.result.outcome == MatchOutcome::BasiliskKilled ||
            state.result.outcome == MatchOutcome::SimultaneousBasiliskKill) {
            const auto encounter = static_cast<std::size_t>(
                std::clamp(state.basilisk.trueEncounters, 1, 3));
            ++stats.basiliskDeathMatchesByEncounter[encounter];
            stats.basiliskDeathRoundsByEncounter[encounter] += rounds;
        }
        ++stats.completed;
        switch (state.result.outcome) {
            case MatchOutcome::BasiliskKilled:
                ++stats.basiliskWins;
                if (state.result.winner.has_value()) {
                    auto& ss = stats.style[static_cast<std::size_t>(styles[*state.result.winner])];
                    ++ss.wins; ++ss.basiliskWins;
                }
                break;
            case MatchOutcome::SimultaneousBasiliskKill:
                ++stats.simultaneousBasiliskDraws;
                break;
            case MatchOutcome::EscapedWithSigil:
                ++stats.extractionWins;
                if (state.result.winner.has_value()) {
                    auto& ss = stats.style[static_cast<std::size_t>(styles[*state.result.winner])];
                    ++ss.wins; ++ss.extractionWins;
                }
                break;
            case MatchOutcome::Draw:
                ++stats.draws;
                if (pitDeadPlayers.size() >= 2) ++stats.mutualPitDraws;
                break;
            case MatchOutcome::None:
                break;
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
        cell.pvpKills += (stats.style[row].pvpKills - beforeRowPvp) +
                         (stats.style[col].pvpKills - beforeColPvp);
        cell.deaths += (stats.style[row].deaths - beforeRowDeaths) +
                       (stats.style[col].deaths - beforeColDeaths);
    } else {
        cell.rowWins += stats.style[row].wins - beforeRowWins;
        cell.pvpKills += stats.style[row].pvpKills - beforeRowPvp;
        cell.deaths += stats.style[row].deaths - beforeRowDeaths;
    }
}

void printPctV314(std::uint64_t n, std::uint64_t d) {
    std::cout << std::fixed << std::setprecision(1)
              << (d ? 100.0 * static_cast<double>(n) / static_cast<double>(d) : 0.0)
              << '%';
}

void printV314Report(const Stats& stats, const EnragedLethalityStats& enraged,
                     const OldHuntersMapTelemetryV314& map,
                     const MatchupGridV314& grid, std::uint64_t maxRounds) {
    std::ostringstream captured;
    auto* old = std::cout.rdbuf(captured.rdbuf());
    printV310Report(stats, enraged, maxRounds);
    std::cout.rdbuf(old);
    std::string report = captured.str();
    const std::string oldLabel = "(BOT V3.10 ENRAGED LETHALITY TELEMETRY)";
    const std::string newLabel = "(BOT V3.14 OLD HUNTER'S MAP INTELLIGENCE)";
    if (const auto pos = report.find(oldLabel); pos != std::string::npos)
        report.replace(pos, oldLabel.size(), newLabel);
    std::cout << report;

    std::cout << "\nOLD HUNTER'S MAP INTELLIGENCE TELEMETRY\n";
    std::cout << "Maps found / inventory-full rejected: " << map.found << '/'
              << map.inventoryRejected << '\n';
    std::cout << "Map use actions selected / map reads received: "
              << map.useActionsSelected << '/' << map.reads << '\n';
    std::cout << "Average true Basilisk distance when read: " << std::fixed
              << std::setprecision(1)
              << (map.reads ? static_cast<double>(map.totalTrueDistance) / map.reads : 0.0)
              << " caves\n";
    std::cout << "Read distance bands 0-2 / 3-5 / 6-8 / 9+: "
              << map.distanceBands[0] << '/' << map.distanceBands[1] << '/'
              << map.distanceBands[2] << '/' << map.distanceBands[3] << '\n';
    std::cout << "Found by Hunter/Scavenger/Opportunist/Extractor: "
              << map.foundByStyle[0] << '/' << map.foundByStyle[1] << '/'
              << map.foundByStyle[2] << '/' << map.foundByStyle[3] << '\n';
    std::cout << "Read by Hunter/Scavenger/Opportunist/Extractor: "
              << map.readByStyle[0] << '/' << map.readByStyle[1] << '/'
              << map.readByStyle[2] << '/' << map.readByStyle[3] << '\n';
    std::cout << "Intelligence activations / targets selected / moves / targets reached: "
              << map.intelligenceActivations << '/' << map.intelligenceTargetsSelected << '/'
              << map.intelligenceMoves << '/' << map.intelligenceTargetsReached << '\n';
    std::cout << "Intelligence moves by Hunter/Scavenger/Opportunist/Extractor: "
              << map.intelligenceMovesByStyle[0] << '/' << map.intelligenceMovesByStyle[1]
              << '/' << map.intelligenceMovesByStyle[2] << '/'
              << map.intelligenceMovesByStyle[3] << '\n';
    std::cout << "Intel invalidated by Basilisk move / true encounter: "
              << map.invalidatedByBasiliskMove << '/' << map.invalidatedByEncounter << '\n';
    std::cout << "True encounters after a map reading: " << map.encountersAfterRead << '\n';
    std::cout << "Average rounds map reading -> true encounter: " << std::fixed
              << std::setprecision(1)
              << (map.encountersAfterRead
                    ? static_cast<double>(map.roundsReadToEncounter) / map.encountersAfterRead
                    : 0.0) << '\n';

    std::cout << "\nPLAYSTYLE MATCHUP TELEMETRY\n";
    for (std::size_t r = 0; r < 4; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            const auto& m = grid[r][c];
            if (!m.matches) continue;
            std::cout << styleName(static_cast<Playstyle>(r)) << " vs "
                      << styleName(static_cast<Playstyle>(c))
                      << " | matches=" << m.matches;
            if (r == c) {
                std::cout << " combinedWins=" << m.rowWins << " (";
                printPctV314(m.rowWins, m.matches);
                std::cout << ')';
            } else {
                std::cout << " P1wins=" << m.rowWins << " (";
                printPctV314(m.rowWins, m.matches);
                std::cout << ") P2wins=" << m.colWins << " (";
                printPctV314(m.colWins, m.matches);
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
}

} // namespace

int main(int argc, char** argv) {
    const std::uint64_t matches = argc > 1 ? std::stoull(argv[1]) : 1000;
    const std::uint64_t maxRounds = argc > 2 ? std::stoull(argv[2]) : 250;
    const std::uint64_t mapBase = argc > 3 ? std::stoull(argv[3]) : 100000;
    const std::uint64_t matchBase = argc > 4 ? std::stoull(argv[4]) : 500000;

    Stats stats;
    EnragedLethalityStats enraged;
    OldHuntersMapTelemetryV314 mapTelemetry;
    MatchupGridV314 matchups{};
    for (std::uint64_t i = 0; i < matches; ++i)
        runOneV314(mapBase + i, matchBase + i, maxRounds,
                   stats, enraged, mapTelemetry, matchups);
    printV314Report(stats, enraged, mapTelemetry, matchups, maxRounds);
    return 0;
}
