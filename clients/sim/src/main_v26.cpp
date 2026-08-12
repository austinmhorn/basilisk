#define main basilisk_v25_main
#include "main_v25.cpp"
#undef main

#include <sstream>

namespace {

struct SweepMemory {
    std::unordered_set<CaveId> pendingTargets;
    bool initialized{false};
};

struct V26Stats {
    std::uint64_t zeroArrowPlayerRounds{0};
    std::uint64_t exhaustiveSweepMoves{0};
    std::uint64_t sweepTargetsReached{0};
    std::uint64_t sweepCyclesCompleted{0};
    std::uint64_t zeroArrowRecoveries{0};
    std::uint64_t meaningfulFrontierExhausted{0};
    std::uint64_t pitOnlyFrontierRemaining{0};
    std::uint64_t stalled{0};
    std::uint64_t stalledAllZeroArrows{0};
    std::uint64_t stalledLooseArrowsAvailable{0};
    std::uint64_t stalledPitOnlyFrontier{0};
};

bool safeKnownConnection(const PlayerRoundSnapshot& s, CaveId from, const TunnelView& tunnel) {
    if (!tunnel.destination.has_value() || tunnel.strongColdDraft) return false;
    return !v25DangerousEdge(s, *tunnel.destination, from);
}

std::unordered_set<CaveId> reachableSafeCaves(const PlayerRoundSnapshot& s) {
    std::unordered_set<CaveId> reached;
    std::queue<CaveId> q;
    reached.insert(s.currentCave);
    q.push(s.currentCave);

    while (!q.empty()) {
        const CaveId cur = q.front();
        q.pop();
        const auto* view = v25View(s, cur);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!safeKnownConnection(s, cur, tunnel)) continue;
            const CaveId next = *tunnel.destination;
            if (reached.insert(next).second) q.push(next);
        }
    }
    return reached;
}

void beginSweep(const PlayerRoundSnapshot& s, SweepMemory& sweep) {
    sweep.pendingTargets = reachableSafeCaves(s);
    sweep.pendingTargets.erase(s.currentCave);
    sweep.initialized = true;
}

std::optional<CaveId> nextSweepStep(const PlayerRoundSnapshot& s,
                                    SweepMemory& sweep,
                                    V26Stats& v26) {
    if (!sweep.initialized) beginSweep(s, sweep);

    if (sweep.pendingTargets.erase(s.currentCave) > 0) {
        ++v26.sweepTargetsReached;
    }

    if (sweep.pendingTargets.empty()) {
        ++v26.sweepCyclesCompleted;
        beginSweep(s, sweep);
        if (sweep.pendingTargets.empty()) return std::nullopt;
    }

    std::queue<CaveId> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_set<CaveId> seen;
    q.push(s.currentCave);
    seen.insert(s.currentCave);

    std::optional<CaveId> target;
    while (!q.empty() && !target.has_value()) {
        const CaveId cur = q.front();
        q.pop();
        const auto* view = v25View(s, cur);
        if (!view) continue;

        std::vector<CaveId> nextCaves;
        for (const auto& tunnel : view->exits) {
            if (!safeKnownConnection(s, cur, tunnel)) continue;
            nextCaves.push_back(*tunnel.destination);
        }
        std::sort(nextCaves.begin(), nextCaves.end());

        for (const CaveId next : nextCaves) {
            if (!seen.insert(next).second) continue;
            parent[next] = cur;
            if (sweep.pendingTargets.contains(next)) {
                target = next;
                break;
            }
            q.push(next);
        }
    }

    if (!target.has_value()) {
        // Discovery/Pit knowledge can change while a sweep is in progress.
        // Rebuild the target set once before giving up.
        beginSweep(s, sweep);
        if (sweep.pendingTargets.empty()) return std::nullopt;
        return nextSweepStep(s, sweep, v26);
    }

    CaveId step = *target;
    while (parent.contains(step) && parent.at(step) != s.currentCave) {
        step = parent.at(step);
    }
    return step;
}

bool shouldUseLateGameSweep(const PlayerRoundSnapshot& s) {
    return s.arrows == 0 && !hasAnyMeaningfulFrontier(s);
}

std::optional<PlayerAction> chooseActionV26(const PlayerRoundSnapshot& s,
                                             BotMemory& memory,
                                             SweepMemory& sweep,
                                             MatchSeed matchSeed,
                                             Stats& stats,
                                             V25Stats& legacy,
                                             V26Stats& v26) {
    if (!s.alive || s.availableActions.empty()) return std::nullopt;
    if (hasObs(s, ObservationType::RivalDied)) memory.rivalDead = true;

    // Preserve all higher-priority V2.5 behavior before entering ammo patrol:
    // escape, healing, extraction navigation, Pit investigation, and a fresh
    // post-death objective search all outrank the sweep.
    if (!shouldUseLateGameSweep(s)) {
        sweep.initialized = false;
        sweep.pendingTargets.clear();
        return chooseActionV25(s, memory, matchSeed, stats, legacy);
    }

    for (const auto& a : s.availableActions) {
        if (a.type == ActionType::Contextual && a.contextualAction == ContextualActionType::Escape)
            return chooseActionV25(s, memory, matchSeed, stats, legacy);
    }
    if (s.health <= 60) {
        for (const auto& a : s.availableActions) {
            if (a.type == ActionType::UseItem && a.targetItem == ItemType::HealingDraught)
                return chooseActionV25(s, memory, matchSeed, stats, legacy);
        }
    }
    if (s.hasHunterSigil && s.extractionCave.has_value())
        return chooseActionV25(s, memory, matchSeed, stats, legacy);

    const bool pitWarning = hasObs(s, ObservationType::PitNearby);
    const auto pitTunnel = investigatedPitTunnel(s);
    if (pitWarning && !pitTunnel.has_value())
        return chooseActionV25(s, memory, matchSeed, stats, legacy);

    auto& caveMemory = memory.caves[s.currentCave];
    if (memory.rivalDead && !s.hasHunterSigil && caveMemory.searches == 0)
        return chooseActionV25(s, memory, matchSeed, stats, legacy);

    ++caveMemory.visits;
    ++v26.zeroArrowPlayerRounds;
    ++v26.meaningfulFrontierExhausted;
    if (hasAnyPitOnlyFrontier(s)) ++v26.pitOnlyFrontierRemaining;

    if (const auto step = nextSweepStep(s, sweep, v26); step.has_value()) {
        if (const auto* move = moveTo(s, *step)) {
            ++v26.exhaustiveSweepMoves;
            ++stats.knownMoves;
            return materialize(s.player, *move);
        }
    }

    // Degenerate single-cave-safe-region fallback.
    return chooseActionV25(s, memory, matchSeed, stats, legacy);
}

void diagnoseV26Stall(const MatchState& state,
                      const std::vector<GameEvent>& previousEvents,
                      V26Stats& v26) {
    ++v26.stalled;
    bool allZero = true;
    bool anyLiving = false;
    bool meaningful = false;
    bool pitOnly = false;

    for (const auto& p : state.players) {
        if (!p.alive) continue;
        anyLiving = true;
        allZero &= p.arrows == 0;
        const auto snapshot = SnapshotSystem::buildForPlayer(state, p.id, previousEvents);
        meaningful |= hasAnyMeaningfulFrontier(snapshot);
        pitOnly |= hasAnyPitOnlyFrontier(snapshot);
    }

    if (anyLiving && allZero) ++v26.stalledAllZeroArrows;
    if (!state.looseArrows.empty()) ++v26.stalledLooseArrowsAvailable;
    if (!meaningful && pitOnly) ++v26.stalledPitOnlyFrontier;
}

void runOneV26(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
               Stats& stats, V25Stats& legacy, V26Stats& v26) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, BotMemory> memories;
    std::unordered_map<PlayerId, SweepMemory> sweeps;
    std::vector<GameEvent> previousEvents;
    std::unordered_set<PlayerId> pitDeadPlayers;
    bool countedSecond = false, countedThird = false;

    while (state.result.status == MatchStatus::Active && state.round <= maxRounds) {
        std::vector<PlayerAction> selected;
        std::unordered_set<PlayerId> zeroBefore;

        for (const auto& p : state.players) {
            if (!p.alive) continue;
            const auto snapshot = SnapshotSystem::buildForPlayer(state, p.id, previousEvents);
            if (snapshot.arrows == 0) zeroBefore.insert(p.id);
            if (hasObs(snapshot, ObservationType::PitNearby)) ++stats.pitWarnings;
            if (const auto action = chooseActionV26(snapshot, memories[p.id], sweeps[p.id],
                                                     matchSeed, stats, legacy, v26)) {
                selected.push_back(*action);
            }
        }

        if (selected.empty()) break;
        bool submitOk = true;
        for (const auto& a : selected) submitOk &= coordinator.submitAction(a);
        if (!submitOk) break;
        bool lockOk = true;
        for (const auto& a : selected) lockOk &= coordinator.lockAction(a.player);
        if (!lockOk) break;

        previousEvents = coordinator.lastEvents();
        collectEventStats(previousEvents, stats, state, pitDeadPlayers);
        for (const auto& event : previousEvents) {
            if (event.type == GameEventType::ArrowFound && event.actor.has_value() &&
                zeroBefore.contains(*event.actor)) {
                ++v26.zeroArrowRecoveries;
                sweeps[*event.actor].initialized = false;
                sweeps[*event.actor].pendingTargets.clear();
            }
        }
        if (!countedSecond && state.basilisk.trueEncounters >= 2) {
            ++stats.secondEncounterMatches;
            countedSecond = true;
        }
        if (!countedThird && state.basilisk.trueEncounters >= 3) {
            ++stats.thirdEncounterMatches;
            countedThird = true;
        }
    }

    ++stats.matches;
    const auto rounds = std::min<std::uint64_t>(state.round, maxRounds);
    stats.totalRounds += rounds;
    stats.roundSamples.push_back(rounds);
    for (const auto& p : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, p.id, previousEvents);
        stats.totalCaves += snapshot.map.caves.size();
        stats.totalFinalArrows += std::max(0, p.arrows);
    }

    if (state.result.status != MatchStatus::Completed) {
        ++stats.stalled;
        diagnoseV26Stall(state, previousEvents, v26);
        return;
    }

    ++stats.completed;
    switch (state.result.outcome) {
        case MatchOutcome::BasiliskKilled: ++stats.basiliskWins; break;
        case MatchOutcome::SimultaneousBasiliskKill: ++stats.simultaneousBasiliskDraws; break;
        case MatchOutcome::EscapedWithSigil: ++stats.extractionWins; break;
        case MatchOutcome::Draw:
            ++stats.draws;
            if (pitDeadPlayers.size() >= 2) ++stats.mutualPitDraws;
            break;
        case MatchOutcome::None: break;
    }
}

void printV26(const V26Stats& v26) {
    std::cout << "\nV2.6 CONVERGENCE TELEMETRY\n";
    std::cout << "Zero-arrow player-rounds: " << v26.zeroArrowPlayerRounds << '\n';
    std::cout << "Exhaustive sweep moves: " << v26.exhaustiveSweepMoves << '\n';
    std::cout << "Sweep targets reached: " << v26.sweepTargetsReached << '\n';
    std::cout << "Full sweep cycles completed: " << v26.sweepCyclesCompleted << '\n';
    std::cout << "Zero-arrow recoveries: " << v26.zeroArrowRecoveries << '\n';
    std::cout << "Meaningful-frontier exhausted player-rounds: " << v26.meaningfulFrontierExhausted << '\n';
    std::cout << "Pit-only frontier remaining player-rounds: " << v26.pitOnlyFrontierRemaining << '\n';
    std::cout << "Stalled matches: " << v26.stalled << '\n';
    std::cout << "Stalled with all living hunters at zero arrows: " << v26.stalledAllZeroArrows << '\n';
    std::cout << "Stalled with loose arrows still on map: " << v26.stalledLooseArrowsAvailable << '\n';
    std::cout << "Stalled with only Pit frontier remaining: " << v26.stalledPitOnlyFrontier << '\n';
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t matches = 1000, maxRounds = 250;
    MapSeed firstMapSeed = 100000;
    MatchSeed firstMatchSeed = 500000;
    if (argc > 1) matches = std::stoull(argv[1]);
    if (argc > 2) maxRounds = std::stoull(argv[2]);
    if (argc > 3) firstMapSeed = static_cast<MapSeed>(std::stoull(argv[3]));
    if (argc > 4) firstMatchSeed = static_cast<MatchSeed>(std::stoull(argv[4]));

    Stats stats;
    V25Stats legacy;
    V26Stats v26;
    for (std::uint64_t i = 0; i < matches; ++i) {
        runOneV26(firstMapSeed + static_cast<MapSeed>(i),
                  firstMatchSeed + static_cast<MatchSeed>(i),
                  maxRounds, stats, legacy, v26);
    }

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V2.6 CONVERGENCE)\n";
    std::cout << "Matches: " << stats.matches << " | max rounds/match: " << maxRounds
              << " | loose-arrow cap: 8\n\n";

    std::cout << "OUTCOMES\n";
    printPercent("Completed", stats.completed, stats.matches);
    printPercent("Stalled at round cap", stats.stalled, stats.matches);
    printPercent("Basilisk kills", stats.basiliskWins, stats.matches);
    printPercent("Simultaneous Basilisk draws", stats.simultaneousBasiliskDraws, stats.matches);
    printPercent("Extraction wins", stats.extractionWins, stats.matches);
    printPercent("Other draws", stats.draws, stats.matches);

    printV26(v26);

    std::cout << "\nCORE TELEMETRY\n";
    std::cout << "Pit deaths / mutual-Pit draws: " << stats.pitDeaths << '/' << stats.mutualPitDraws << '\n';
    std::cout << "Bodies created/found: " << stats.bodiesCreated << '/' << stats.bodiesFound << '\n';
    std::cout << "Sigils acquired / escapes: " << stats.sigilsAcquired << '/' << stats.escaped << '\n';
    std::cout << "Loose arrows spawned/found/fired: " << stats.looseArrowSpawns << '/'
              << stats.arrowsFound << '/' << stats.arrowsFired << '\n';
    std::cout << "Basilisk true encounters/evades: " << stats.basiliskEncounters << '/'
              << stats.basiliskEvades << '\n';
    return 0;
}
