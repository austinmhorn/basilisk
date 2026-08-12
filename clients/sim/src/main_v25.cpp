#define main basilisk_v23_main
#include "main_v23.cpp"
#undef main

#include <limits>
#include <sstream>

namespace {

struct V25Stats {
    std::uint64_t zeroArrowPlayerRounds{0};
    std::uint64_t zeroArrowPatrolMoves{0};
    std::uint64_t zeroArrowRecoveries{0};
    std::uint64_t meaningfulFrontierExhausted{0};
    std::uint64_t pitOnlyFrontierRemaining{0};
    std::uint64_t stalled{0};
    std::uint64_t stalledAllZeroArrows{0};
    std::uint64_t stalledLooseArrowsAvailable{0};
    std::uint64_t stalledMeaningfulFrontier{0};
    std::uint64_t stalledPitOnlyFrontier{0};
};

const DiscoveredCaveView* v25View(const PlayerRoundSnapshot& s, CaveId cave) {
    return caveView(s, cave);
}

bool v25DangerousEdge(const PlayerRoundSnapshot& s, CaveId from, CaveId to) {
    const auto* view = v25View(s, from);
    if (!view) return false;
    return std::any_of(view->exits.begin(), view->exits.end(), [&](const TunnelView& t) {
        return t.destination == to && t.strongColdDraft;
    });
}

bool caveHasMeaningfulFrontier(const DiscoveredCaveView& cave) {
    return std::any_of(cave.exits.begin(), cave.exits.end(),
        [](const TunnelView& t) { return !t.destination.has_value() && !t.strongColdDraft; });
}

bool caveHasPitOnlyFrontier(const DiscoveredCaveView& cave) {
    bool anyUnknown = false;
    bool anyMeaningful = false;
    for (const auto& t : cave.exits) {
        if (t.destination.has_value()) continue;
        anyUnknown = true;
        if (!t.strongColdDraft) anyMeaningful = true;
    }
    return anyUnknown && !anyMeaningful;
}

std::optional<CaveId> nearestMeaningfulFrontierStep(const PlayerRoundSnapshot& s) {
    if (const auto* here = v25View(s, s.currentCave); here && caveHasMeaningfulFrontier(*here))
        return s.currentCave;

    std::queue<CaveId> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_set<CaveId> seen;
    q.push(s.currentCave);
    seen.insert(s.currentCave);

    while (!q.empty()) {
        const CaveId cur = q.front(); q.pop();
        const auto* view = v25View(s, cur);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!tunnel.destination.has_value() || tunnel.strongColdDraft) continue;
            const CaveId next = *tunnel.destination;
            if (v25DangerousEdge(s, next, cur)) continue;
            if (!seen.insert(next).second) continue;
            parent[next] = cur;
            const auto* nextView = v25View(s, next);
            if (nextView && caveHasMeaningfulFrontier(*nextView)) {
                CaveId step = next;
                while (parent.contains(step) && parent.at(step) != s.currentCave)
                    step = parent.at(step);
                return step;
            }
            q.push(next);
        }
    }
    return std::nullopt;
}

std::optional<CaveId> leastVisitedSafePatrolStep(const PlayerRoundSnapshot& s, const BotMemory& memory) {
    struct Node { CaveId cave; int distance; };
    std::queue<Node> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_set<CaveId> seen;
    q.push({s.currentCave, 0});
    seen.insert(s.currentCave);

    CaveId best = s.currentCave;
    int bestVisits = std::numeric_limits<int>::max();
    int bestDistance = std::numeric_limits<int>::max();

    while (!q.empty()) {
        const auto [cur, distance] = q.front(); q.pop();
        const auto memIt = memory.caves.find(cur);
        const int visits = memIt == memory.caves.end() ? 0 : memIt->second.visits;
        if (cur != s.currentCave &&
            (visits < bestVisits || (visits == bestVisits && distance < bestDistance) ||
             (visits == bestVisits && distance == bestDistance && cur < best))) {
            best = cur;
            bestVisits = visits;
            bestDistance = distance;
        }

        const auto* view = v25View(s, cur);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!tunnel.destination.has_value() || tunnel.strongColdDraft) continue;
            const CaveId next = *tunnel.destination;
            if (v25DangerousEdge(s, next, cur)) continue;
            if (!seen.insert(next).second) continue;
            parent[next] = cur;
            q.push({next, distance + 1});
        }
    }

    if (best == s.currentCave) return std::nullopt;
    CaveId step = best;
    while (parent.contains(step) && parent.at(step) != s.currentCave)
        step = parent.at(step);
    return step;
}

bool hasAnyMeaningfulFrontier(const PlayerRoundSnapshot& s) {
    return std::any_of(s.map.caves.begin(), s.map.caves.end(), caveHasMeaningfulFrontier);
}

bool hasAnyPitOnlyFrontier(const PlayerRoundSnapshot& s) {
    return std::any_of(s.map.caves.begin(), s.map.caves.end(), caveHasPitOnlyFrontier);
}

std::optional<PlayerAction> chooseActionV25(const PlayerRoundSnapshot& s,
                                             BotMemory& memory,
                                             MatchSeed matchSeed,
                                             Stats& stats,
                                             V25Stats& v25) {
    if (!s.alive || s.availableActions.empty()) return std::nullopt;
    if (hasObs(s, ObservationType::RivalDied)) memory.rivalDead = true;
    ++memory.caves[s.currentCave].visits;

    const std::uint64_t salt = static_cast<std::uint64_t>(matchSeed) ^
        (static_cast<std::uint64_t>(s.round) * 0x9E3779B97F4A7C15ULL) ^
        (static_cast<std::uint64_t>(s.player) * 0xBF58476D1CE4E5B9ULL);

    for (const auto& a : s.availableActions)
        if (a.type == ActionType::Contextual && a.contextualAction == ContextualActionType::Escape)
            return materialize(s.player, a);

    if (s.health <= 60) {
        for (const auto& a : s.availableActions)
            if (a.type == ActionType::UseItem && a.targetItem == ItemType::HealingDraught)
                return materialize(s.player, a);
    }

    if (s.hasHunterSigil && s.extractionCave.has_value()) {
        if (const auto step = nextStepTo(s, *s.extractionCave); step.has_value() && *step != s.currentCave) {
            if (const auto* move = moveTo(s, *step)) {
                ++stats.extractionPathMoves;
                ++stats.knownMoves;
                return materialize(s.player, *move);
            }
        }
    }

    auto& caveMemory = memory.caves[s.currentCave];
    const bool pitWarning = hasObs(s, ObservationType::PitNearby);
    const auto pitTunnel = investigatedPitTunnel(s);

    if (pitWarning && !pitTunnel.has_value()) {
        if (const auto* search = searchAction(s)) {
            ++stats.pitInvestigations;
            ++caveMemory.searches;
            if (memory.rivalDead) ++stats.objectiveSearches;
            return materialize(s.player, *search);
        }
    }

    if (memory.rivalDead && !s.hasHunterSigil && caveMemory.searches == 0) {
        if (const auto* search = searchAction(s)) {
            ++caveMemory.searches;
            ++stats.objectiveSearches;
            return materialize(s.player, *search);
        }
    }

    if (hasObs(s, ObservationType::EnragedLastKnownCave) && s.arrows > 0) {
        for (const auto& o : s.observations)
            if (o.type == ObservationType::EnragedLastKnownCave && o.cave.has_value())
                if (const auto* shot = shootTo(s, *o.cave)) return materialize(s.player, *shot);
    }

    if (!memory.rivalDead &&
        (basiliskClue(s) || hasObs(s, ObservationType::RivalNearby)) && s.arrows > 0) {
        const auto shots = actionsOfType(s, ActionType::Shoot);
        if (const auto* shot = pick(shots, salt)) return materialize(s.player, *shot);
    }

    const auto allMoves = actionsOfType(s, ActionType::Move);
    std::vector<const AvailableAction*> safeMoves;
    for (const auto* move : allMoves) {
        if (pitTunnel.has_value() && actionUsesTunnel(*move, *pitTunnel)) {
            ++stats.knownPitTunnelAvoidances;
            continue;
        }
        safeMoves.push_back(move);
    }

    std::vector<const AvailableAction*> unknown;
    std::vector<const AvailableAction*> known;
    for (const auto* move : safeMoves) {
        if (move->targetCave.has_value()) known.push_back(move);
        else unknown.push_back(move);
    }

    // V2.5: only unknown exits that are not positively identified Pit routes
    // count as meaningful exploration.
    if (!unknown.empty()) {
        if (const auto* move = pick(unknown, salt >> 5U)) {
            ++stats.unexploredMoves;
            return materialize(s.player, *move);
        }
    }

    if (const auto frontier = nearestMeaningfulFrontierStep(s); frontier.has_value() && *frontier != s.currentCave) {
        if (const auto* move = moveTo(s, *frontier)) {
            ++stats.frontierMoves;
            ++stats.knownMoves;
            return materialize(s.player, *move);
        }
    }

    // V2.5: with no arrows, patrol the safe discovered graph toward the
    // least-visited cave. The bot still has no knowledge of loose-arrow caves;
    // this merely produces a systematic sweep instead of random looping.
    if (s.arrows == 0) {
        ++v25.zeroArrowPlayerRounds;
        if (!hasAnyMeaningfulFrontier(s)) {
            ++v25.meaningfulFrontierExhausted;
            if (hasAnyPitOnlyFrontier(s)) ++v25.pitOnlyFrontierRemaining;
        }
        if (const auto step = leastVisitedSafePatrolStep(s, memory); step.has_value()) {
            if (const auto* move = moveTo(s, *step)) {
                ++v25.zeroArrowPatrolMoves;
                ++stats.knownMoves;
                return materialize(s.player, *move);
            }
        }
    }

    if (memory.rivalDead && basiliskClue(s) && s.arrows > 0) {
        const auto shots = actionsOfType(s, ActionType::Shoot);
        if (const auto* shot = pick(shots, salt >> 7U)) return materialize(s.player, *shot);
    }

    if (!known.empty()) {
        if (const auto* move = pick(known, salt >> 11U)) {
            ++stats.knownMoves;
            return materialize(s.player, *move);
        }
    }

    if (const auto* search = searchAction(s)) return materialize(s.player, *search);
    return materialize(s.player, s.availableActions.front());
}

void diagnoseV25Stall(const MatchState& state,
                      const std::vector<GameEvent>& previousEvents,
                      V25Stats& v25) {
    ++v25.stalled;
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
    if (anyLiving && allZero) ++v25.stalledAllZeroArrows;
    if (!state.looseArrows.empty()) ++v25.stalledLooseArrowsAvailable;
    if (meaningful) ++v25.stalledMeaningfulFrontier;
    if (!meaningful && pitOnly) ++v25.stalledPitOnlyFrontier;
}

void runOneV25(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
               Stats& stats, V25Stats& v25) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, BotMemory> memories;
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
            if (const auto action = chooseActionV25(snapshot, memories[p.id], matchSeed, stats, v25))
                selected.push_back(*action);
        }
        if (selected.empty()) break;
        for (const auto& a : selected) if (!coordinator.submitAction(a)) break;
        for (const auto& a : selected) if (!coordinator.lockAction(a.player)) break;
        previousEvents = coordinator.lastEvents();
        collectEventStats(previousEvents, stats, state, pitDeadPlayers);
        for (const auto& event : previousEvents) {
            if (event.type == GameEventType::ArrowFound && event.actor.has_value() && zeroBefore.contains(*event.actor))
                ++v25.zeroArrowRecoveries;
        }
        if (!countedSecond && state.basilisk.trueEncounters >= 2) { ++stats.secondEncounterMatches; countedSecond = true; }
        if (!countedThird && state.basilisk.trueEncounters >= 3) { ++stats.thirdEncounterMatches; countedThird = true; }
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
        diagnoseV25Stall(state, previousEvents, v25);
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
    V25Stats v25;
    for (std::uint64_t i = 0; i < matches; ++i)
        runOneV25(firstMapSeed + static_cast<MapSeed>(i), firstMatchSeed + static_cast<MatchSeed>(i), maxRounds, stats, v25);

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V2.5 STALL RECOVERY)\n";
    std::cout << "Matches: " << stats.matches << " | max rounds/match: " << maxRounds << "\n\n";
    std::cout << "OUTCOMES\n";
    printPercent("Completed", stats.completed, stats.matches);
    printPercent("Stalled at round cap", stats.stalled, stats.matches);
    printPercent("Basilisk kills", stats.basiliskWins, stats.matches);
    printPercent("Simultaneous Basilisk draws", stats.simultaneousBasiliskDraws, stats.matches);
    printPercent("Extraction wins", stats.extractionWins, stats.matches);
    printPercent("Other draws", stats.draws, stats.matches);

    std::cout << "\nV2.5 RECOVERY TELEMETRY\n";
    std::cout << "Zero-arrow player-rounds: " << v25.zeroArrowPlayerRounds << '\n';
    std::cout << "Zero-arrow patrol moves: " << v25.zeroArrowPatrolMoves << '\n';
    std::cout << "Zero-arrow recoveries: " << v25.zeroArrowRecoveries << '\n';
    std::cout << "Meaningful-frontier exhausted player-rounds: " << v25.meaningfulFrontierExhausted << '\n';
    std::cout << "Pit-only frontier remaining player-rounds: " << v25.pitOnlyFrontierRemaining << '\n';
    std::cout << "Stalled matches: " << v25.stalled << '\n';
    std::cout << "Stalled with all living hunters at zero arrows: " << v25.stalledAllZeroArrows << '\n';
    std::cout << "Stalled with loose arrows still on map: " << v25.stalledLooseArrowsAvailable << '\n';
    std::cout << "Stalled with meaningful frontier remaining: " << v25.stalledMeaningfulFrontier << '\n';
    std::cout << "Stalled with only Pit frontier remaining: " << v25.stalledPitOnlyFrontier << '\n';

    std::cout << "\nCORE TELEMETRY\n";
    std::cout << "Pit deaths / mutual-Pit draws: " << stats.pitDeaths << '/' << stats.mutualPitDraws << '\n';
    std::cout << "Bodies created/found: " << stats.bodiesCreated << '/' << stats.bodiesFound << '\n';
    std::cout << "Sigils acquired / escapes: " << stats.sigilsAcquired << '/' << stats.escaped << '\n';
    std::cout << "Loose arrows spawned/found/fired: " << stats.looseArrowSpawns << '/' << stats.arrowsFound << '/' << stats.arrowsFired << '\n';
    std::cout << "Basilisk true encounters/evades: " << stats.basiliskEncounters << '/' << stats.basiliskEvades << '\n';
    return 0;
}
