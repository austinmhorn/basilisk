#define BASILISK_SIM_V25_NO_MAIN
#include "main_v25.cpp"
#undef BASILISK_SIM_V25_NO_MAIN

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
        const CaveId cur = q.front(); q.pop();
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
    if (sweep.pendingTargets.erase(s.currentCave) > 0) ++v26.sweepTargetsReached;
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
        const CaveId cur = q.front(); q.pop();
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
        beginSweep(s, sweep);
        if (sweep.pendingTargets.empty()) return std::nullopt;
        return nextSweepStep(s, sweep, v26);
    }

    CaveId step = *target;
    while (parent.contains(step) && parent.at(step) != s.currentCave) step = parent.at(step);
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
    if (!shouldUseLateGameSweep(s)) {
        sweep.initialized = false;
        sweep.pendingTargets.clear();
        return chooseActionV25(s, memory, matchSeed, stats, legacy);
    }
    for (const auto& a : s.availableActions)
        if (a.type == ActionType::Contextual && a.contextualAction == ContextualActionType::Escape)
            return chooseActionV25(s, memory, matchSeed, stats, legacy);
    if (s.health <= 60)
        for (const auto& a : s.availableActions)
            if (a.type == ActionType::UseItem && a.targetItem == ItemType::HealingDraught)
                return chooseActionV25(s, memory, matchSeed, stats, legacy);
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
    return chooseActionV25(s, memory, matchSeed, stats, legacy);
}

void diagnoseV26Stall(const MatchState& state,
                      const std::vector<GameEvent>& previousEvents,
                      V26Stats& v26) {
    ++v26.stalled;
    bool allZero = true, anyLiving = false, meaningful = false, pitOnly = false;
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
