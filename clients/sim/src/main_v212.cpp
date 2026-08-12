#define BASILISK_SIM_V211_NO_MAIN
#include "main_v211.cpp"
#undef BASILISK_SIM_V211_NO_MAIN

#include <limits>

namespace {

struct V212Stats {
    std::uint64_t stalledZeroArrowHunters{0};
    std::uint64_t strandedArrowHunterPairs{0};
    std::uint64_t safeKnownReachable{0};
    std::uint64_t knownOnlyIfCrossingPitClue{0};
    std::uint64_t safeAuthoritativeButUnknown{0};
    std::uint64_t trulyPitBlocked{0};
    std::uint64_t spawnedBehindActiveSweep{0};
    std::uint64_t stalledArrowsSpawnedBehindSweep{0};
    std::uint64_t nearestSafeKnownDistanceTotal{0};
    std::uint64_t nearestSafeKnownDistanceSamples{0};
    int maxNearestSafeKnownDistance{0};
};

std::optional<int> knownGraphDistance(const PlayerRoundSnapshot& s, CaveId target, bool avoidPitClues) {
    if (s.currentCave == target) return 0;
    std::queue<std::pair<CaveId, int>> q;
    std::unordered_set<CaveId> seen;
    q.push({s.currentCave, 0});
    seen.insert(s.currentCave);
    while (!q.empty()) {
        const auto [cur, dist] = q.front(); q.pop();
        const auto* view = v25View(s, cur);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!tunnel.destination.has_value()) continue;
            if (avoidPitClues && !safeKnownConnection(s, cur, tunnel)) continue;
            const CaveId next = *tunnel.destination;
            if (!seen.insert(next).second) continue;
            if (next == target) return dist + 1;
            q.push({next, dist + 1});
        }
    }
    return std::nullopt;
}

std::optional<int> authoritativeDistanceAvoidingPit(const MatchState& state, CaveId start, CaveId target) {
    std::unordered_set<CaveId> pitCaves;
    for (const auto& pit : state.pits) if (pit.active) pitCaves.insert(pit.cave);
    if (pitCaves.contains(start) || pitCaves.contains(target)) return std::nullopt;
    if (start == target) return 0;

    std::queue<std::pair<CaveId, int>> q;
    std::unordered_set<CaveId> seen;
    q.push({start, 0});
    seen.insert(start);
    while (!q.empty()) {
        const auto [cur, dist] = q.front(); q.pop();
        for (const CaveId next : state.world.cave(cur).connections) {
            if (pitCaves.contains(next) || !seen.insert(next).second) continue;
            if (next == target) return dist + 1;
            q.push({next, dist + 1});
        }
    }
    return std::nullopt;
}

void diagnoseStrandedArrows(const MatchState& state,
                            const std::vector<GameEvent>& previousEvents,
                            const std::unordered_map<PlayerId, std::unordered_set<CaveId>>& spawnedBehind,
                            V212Stats& v212) {
    for (const auto& player : state.players) {
        if (!player.alive || player.arrows != 0) continue;
        ++v212.stalledZeroArrowHunters;
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
        std::optional<int> nearest;

        for (const CaveId arrow : state.looseArrows) {
            ++v212.strandedArrowHunterPairs;
            const auto safeKnown = knownGraphDistance(snapshot, arrow, true);
            const auto anyKnown = knownGraphDistance(snapshot, arrow, false);
            const auto safeAuthoritative = authoritativeDistanceAvoidingPit(state, player.cave, arrow);

            if (safeKnown.has_value()) {
                ++v212.safeKnownReachable;
                if (!nearest.has_value() || *safeKnown < *nearest) nearest = *safeKnown;
            } else if (anyKnown.has_value()) {
                ++v212.knownOnlyIfCrossingPitClue;
            } else if (safeAuthoritative.has_value()) {
                ++v212.safeAuthoritativeButUnknown;
            } else {
                ++v212.trulyPitBlocked;
            }

            const auto behindIt = spawnedBehind.find(player.id);
            if (behindIt != spawnedBehind.end() && behindIt->second.contains(arrow))
                ++v212.stalledArrowsSpawnedBehindSweep;
        }

        if (nearest.has_value()) {
            v212.nearestSafeKnownDistanceTotal += static_cast<std::uint64_t>(*nearest);
            ++v212.nearestSafeKnownDistanceSamples;
            v212.maxNearestSafeKnownDistance = std::max(v212.maxNearestSafeKnownDistance, *nearest);
        }
    }
}

void runOneV212(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
                Stats& stats, V25Stats& legacy, V26Stats& v26,
                V27Stats& v27, V28Stats& v28, V210Stats& v210,
                V211Stats& v211, V212Stats& v212) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, BotMemory> memories;
    std::unordered_map<PlayerId, SweepMemory> sweeps;
    std::unordered_map<PlayerId, AmmoMemory> ammoMemories;
    std::unordered_map<PlayerId, std::unordered_set<CaveId>> spawnedBehind;
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
            if (const auto action = chooseActionV211(snapshot, memories[p.id], sweeps[p.id], ammoMemories[p.id],
                                                     matchSeed, stats, legacy, v26, v210, v211))
                selected.push_back(*action);
        }
        if (selected.empty()) break;
        bool ok = true;
        for (const auto& a : selected) ok &= coordinator.submitAction(a);
        if (!ok) break;
        for (const auto& a : selected) ok &= coordinator.lockAction(a.player);
        if (!ok) break;

        previousEvents = coordinator.lastEvents();
        collectEventStats(previousEvents, stats, state, pitDeadPlayers);
        collectV27Events(previousEvents, v27);
        collectV28Events(previousEvents, v28);

        for (const auto& event : previousEvents) {
            if (event.type == GameEventType::ArrowFound && event.actor.has_value()) {
                if (event.cave.has_value() && ammoMemories[*event.actor].seenLooseArrowCaves.erase(*event.cave) > 0)
                    ++v210.rememberedArrowRecoveries;
                if (zeroBefore.contains(*event.actor)) ++v26.zeroArrowRecoveries;
            }
            if (event.type == GameEventType::LooseArrowSpawned && event.cave.has_value()) {
                const CaveId cave = *event.cave;
                for (const auto& p : state.players) {
                    if (!p.alive || p.arrows != 0) continue;
                    auto& sweep = sweeps[p.id];
                    if (!sweep.initialized) continue;
                    const auto snapshot = SnapshotSystem::buildForPlayer(state, p.id, previousEvents);
                    const auto reachable = reachableSafeCaves(snapshot);
                    if (!reachable.contains(cave)) continue;
                    if (cave != snapshot.currentCave && !sweep.pendingTargets.contains(cave)) {
                        spawnedBehind[p.id].insert(cave);
                        ++v212.spawnedBehindActiveSweep;
                    }
                }
            }
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
        diagnoseV26Stall(state, previousEvents, v26);
        diagnoseStrandedArrows(state, previousEvents, spawnedBehind, v212);
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

    Stats stats; V25Stats legacy; V26Stats v26; V27Stats v27; V28Stats v28;
    V210Stats v210; V211Stats v211; V212Stats v212;
    for (std::uint64_t i = 0; i < matches; ++i)
        runOneV212(firstMapSeed + static_cast<MapSeed>(i), firstMatchSeed + static_cast<MatchSeed>(i),
                   maxRounds, stats, legacy, v26, v27, v28, v210, v211, v212);

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V2.12 STRANDED ARROW DIAGNOSTICS)\n";
    std::cout << "Matches: " << stats.matches << " | max rounds/match: " << maxRounds << " | loose-arrow cap: 8\n\n";
    std::cout << "OUTCOMES\n";
    printPercent("Completed", stats.completed, stats.matches);
    printPercent("Stalled at round cap", stats.stalled, stats.matches);
    printPercent("Basilisk kills", stats.basiliskWins, stats.matches);
    printPercent("Simultaneous Basilisk draws", stats.simultaneousBasiliskDraws, stats.matches);
    printPercent("Extraction wins", stats.extractionWins, stats.matches);
    printPercent("Other draws", stats.draws, stats.matches);

    printV26(v26);
    printV27(v27);
    std::cout << "Survey Fragments found/used: " << v28.surveysFound << '/' << v28.surveysUsed << '\n';
    std::cout << "Blood Bait found/used: " << v28.bloodBaitFound << '/' << v28.bloodBaitUsed << '\n';
    std::cout << "Blood Bait influenced Basilisk moves: " << v28.baitInfluencedMoves << '\n';

    std::cout << "\nV2.12 STRANDED ARROW AUTOPSY\n";
    std::cout << "Stalled zero-arrow hunters diagnosed: " << v212.stalledZeroArrowHunters << '\n';
    std::cout << "Stranded arrow/hunter pairs: " << v212.strandedArrowHunterPairs << '\n';
    std::cout << "Safely reachable through known graph: " << v212.safeKnownReachable << '\n';
    std::cout << "Known only if crossing Pit-clued edge: " << v212.knownOnlyIfCrossingPitClue << '\n';
    std::cout << "Safely reachable but not through known graph: " << v212.safeAuthoritativeButUnknown << '\n';
    std::cout << "Truly blocked by Pit topology: " << v212.trulyPitBlocked << '\n';
    std::cout << "Arrows spawned behind active sweep: " << v212.spawnedBehindActiveSweep << '\n';
    std::cout << "Stranded arrows that spawned behind sweep: " << v212.stalledArrowsSpawnedBehindSweep << '\n';
    if (v212.nearestSafeKnownDistanceSamples > 0) {
        const double avg = static_cast<double>(v212.nearestSafeKnownDistanceTotal) /
                           static_cast<double>(v212.nearestSafeKnownDistanceSamples);
        std::cout << "Average nearest safe-known arrow distance: " << avg << '\n';
        std::cout << "Maximum nearest safe-known arrow distance: " << v212.maxNearestSafeKnownDistance << '\n';
    }

    std::cout << "\nV2.11 SHOT DISCIPLINE TELEMETRY\n";
    std::cout << "Restless-noise shots suppressed: " << v211.restlessShotsSuppressed << '\n';
    std::cout << "Last-arrow PvP shots suppressed: " << v211.lastArrowPvpShotsSuppressed << '\n';
    std::cout << "Adjacent-Basilisk shots: " << v211.adjacentBasiliskShots << '\n';
    std::cout << "Exact Enraged shots: " << v211.exactEnragedShots << '\n';
    std::cout << "PvP shots taken: " << v211.pvpShots << '\n';

    std::cout << "\nCORE TELEMETRY\n";
    std::cout << "Pit deaths / mutual-Pit draws: " << stats.pitDeaths << '/' << stats.mutualPitDraws << '\n';
    std::cout << "Bodies created/found: " << stats.bodiesCreated << '/' << stats.bodiesFound << '\n';
    std::cout << "Sigils acquired / escapes: " << stats.sigilsAcquired << '/' << stats.escaped << '\n';
    std::cout << "Loose arrows spawned/found/fired: " << stats.looseArrowSpawns << '/' << stats.arrowsFound << '/' << stats.arrowsFired << '\n';
    std::cout << "Basilisk true encounters/evades: " << stats.basiliskEncounters << '/' << stats.basiliskEvades << '\n';
    return 0;
}
