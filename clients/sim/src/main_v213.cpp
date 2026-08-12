#define BASILISK_SIM_V211_NO_MAIN
#include "main_v211.cpp"
#undef BASILISK_SIM_V211_NO_MAIN

#include <limits>

namespace {

struct StalenessMemory {
    std::unordered_map<CaveId, RoundNumber> lastVisitedRound;
};

struct V213Stats {
    std::uint64_t stalenessPatrolMoves{0};
    std::uint64_t stalenessTargetsSelected{0};
    std::uint64_t targetAgeTotal{0};
    std::uint64_t arrowRecoveriesDuringStalenessPatrol{0};
    std::uint64_t stalledZeroArrowHuntersWithReachableArrow{0};
    std::uint64_t nearestReachableArrowDistanceTotal{0};
    std::uint64_t nearestReachableArrowDistanceSamples{0};
    int maxNearestReachableArrowDistance{0};
};

struct StalenessChoice {
    CaveId target{};
    CaveId firstStep{};
    RoundNumber age{};
};

std::optional<StalenessChoice> oldestSafeCaveStep(
    const PlayerRoundSnapshot& s,
    const StalenessMemory& memory) {

    struct Node { CaveId cave; int distance; };
    std::queue<Node> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_map<CaveId, int> distance;
    std::unordered_set<CaveId> seen;

    q.push({s.currentCave, 0});
    seen.insert(s.currentCave);
    distance[s.currentCave] = 0;

    std::optional<CaveId> best;
    RoundNumber bestLastVisited = std::numeric_limits<RoundNumber>::max();
    int bestDistance = std::numeric_limits<int>::max();

    while (!q.empty()) {
        const auto [cur, dist] = q.front();
        q.pop();

        if (cur != s.currentCave) {
            const auto it = memory.lastVisitedRound.find(cur);
            const RoundNumber lastVisited =
                it == memory.lastVisitedRound.end() ? RoundNumber{0} : it->second;

            if (!best.has_value() ||
                lastVisited < bestLastVisited ||
                (lastVisited == bestLastVisited && dist < bestDistance) ||
                (lastVisited == bestLastVisited && dist == bestDistance && cur < *best)) {
                best = cur;
                bestLastVisited = lastVisited;
                bestDistance = dist;
            }
        }

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
            distance[next] = dist + 1;
            q.push({next, dist + 1});
        }
    }

    if (!best.has_value()) return std::nullopt;

    CaveId step = *best;
    while (parent.contains(step) && parent.at(step) != s.currentCave)
        step = parent.at(step);

    const RoundNumber lastVisited = bestLastVisited == std::numeric_limits<RoundNumber>::max()
        ? RoundNumber{0}
        : bestLastVisited;
    const RoundNumber age = s.round >= lastVisited ? s.round - lastVisited : RoundNumber{0};
    return StalenessChoice{*best, step, age};
}

std::optional<int> safeKnownDistanceV213(const PlayerRoundSnapshot& s, CaveId target) {
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
            if (!safeKnownConnection(s, cur, tunnel)) continue;
            const CaveId next = *tunnel.destination;
            if (!seen.insert(next).second) continue;
            if (next == target) return dist + 1;
            q.push({next, dist + 1});
        }
    }
    return std::nullopt;
}

std::optional<PlayerAction> chooseActionV213(
    const PlayerRoundSnapshot& s,
    BotMemory& memory,
    SweepMemory& sweep,
    AmmoMemory& ammo,
    StalenessMemory& stale,
    MatchSeed matchSeed,
    Stats& stats,
    V25Stats& legacy,
    V26Stats& v26,
    V210Stats& v210,
    V211Stats& v211,
    V213Stats& v213,
    bool& choseStalenessMove) {

    choseStalenessMove = false;
    stale.lastVisitedRound[s.currentCave] = s.round;

    if (s.looseArrowPresent) {
        const bool newlySeen = ammo.seenLooseArrowCaves.insert(s.currentCave).second;
        if (newlySeen && s.arrows >= s.maxArrows)
            ++v210.looseArrowSightingsAtCapacity;
    } else if (ammo.seenLooseArrowCaves.erase(s.currentCave) > 0) {
        ++v210.rememberedArrowLocationsInvalidated;
    }

    if (s.arrows != 0 || hasAnyMeaningfulFrontier(s)) {
        return chooseActionV211(
            s, memory, sweep, ammo, matchSeed, stats, legacy, v26, v210, v211);
    }

    if (!s.alive || s.availableActions.empty()) return std::nullopt;
    if (hasObs(s, ObservationType::RivalDied)) memory.rivalDead = true;

    for (const auto& a : s.availableActions)
        if (a.type == ActionType::Contextual && a.contextualAction == ContextualActionType::Escape)
            return materialize(s.player, a);

    if (s.health <= 60)
        if (const auto* heal = useItemAction(s, ItemType::HealingDraught))
            return materialize(s.player, *heal);

    if (s.hasHunterSigil && s.extractionCave.has_value()) {
        if (const auto step = nextStepTo(s, *s.extractionCave);
            step.has_value() && *step != s.currentCave) {
            if (const auto* move = moveTo(s, *step)) {
                ++stats.extractionPathMoves;
                ++stats.knownMoves;
                return materialize(s.player, *move);
            }
        }
    }

    if (hasObs(s, ObservationType::JackalNearby))
        if (const auto* repel = useItemAction(s, ItemType::JackalRepellent))
            return materialize(s.player, *repel);

    if (hasObs(s, ObservationType::PitNearby) && s.temporarilyRevealedPitCaves.empty())
        if (const auto* map = useItemAction(s, ItemType::OldMinersMap))
            return materialize(s.player, *map);

    const bool pitWarning = hasObs(s, ObservationType::PitNearby);
    const auto pitTunnel = investigatedPitTunnel(s);
    auto& caveMemory = memory.caves[s.currentCave];
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

    if (!s.hasHunterSigil) {
        if (const auto step = safeNextStepToRememberedArrow(s, ammo);
            step.has_value() && *step != s.currentCave) {
            if (const auto* move = moveTo(s, *step)) {
                ++v210.rememberedArrowPursuitMoves;
                ++stats.knownMoves;
                return materialize(s.player, *move);
            }
        }
    }

    ++v26.zeroArrowPlayerRounds;
    ++v26.meaningfulFrontierExhausted;
    if (hasAnyPitOnlyFrontier(s)) ++v26.pitOnlyFrontierRemaining;

    if (const auto choice = oldestSafeCaveStep(s, stale); choice.has_value()) {
        if (const auto* move = moveTo(s, choice->firstStep)) {
            ++v213.stalenessPatrolMoves;
            ++v213.stalenessTargetsSelected;
            v213.targetAgeTotal += choice->age;
            ++stats.knownMoves;
            choseStalenessMove = true;
            return materialize(s.player, *move);
        }
    }

    return chooseActionV211(
        s, memory, sweep, ammo, matchSeed, stats, legacy, v26, v210, v211);
}

void diagnoseV213Stall(
    const MatchState& state,
    const std::vector<GameEvent>& previousEvents,
    V213Stats& v213) {

    for (const auto& player : state.players) {
        if (!player.alive || player.arrows != 0) continue;
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
        std::optional<int> nearest;
        for (const CaveId arrow : state.looseArrows) {
            const auto dist = safeKnownDistanceV213(snapshot, arrow);
            if (!dist.has_value()) continue;
            if (!nearest.has_value() || *dist < *nearest) nearest = *dist;
        }
        if (!nearest.has_value()) continue;
        ++v213.stalledZeroArrowHuntersWithReachableArrow;
        v213.nearestReachableArrowDistanceTotal += static_cast<std::uint64_t>(*nearest);
        ++v213.nearestReachableArrowDistanceSamples;
        v213.maxNearestReachableArrowDistance =
            std::max(v213.maxNearestReachableArrowDistance, *nearest);
    }
}

void runOneV213(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
                Stats& stats, V25Stats& legacy, V26Stats& v26,
                V27Stats& v27, V28Stats& v28, V210Stats& v210,
                V211Stats& v211, V213Stats& v213) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, BotMemory> memories;
    std::unordered_map<PlayerId, SweepMemory> sweeps;
    std::unordered_map<PlayerId, AmmoMemory> ammoMemories;
    std::unordered_map<PlayerId, StalenessMemory> staleMemories;
    std::unordered_set<PlayerId> stalenessMovers;
    std::vector<GameEvent> previousEvents;
    std::unordered_set<PlayerId> pitDeadPlayers;
    bool countedSecond = false, countedThird = false;

    while (state.result.status == MatchStatus::Active && state.round <= maxRounds) {
        std::vector<PlayerAction> selected;
        std::unordered_set<PlayerId> zeroBefore;
        stalenessMovers.clear();

        for (const auto& p : state.players) {
            if (!p.alive) continue;
            const auto snapshot = SnapshotSystem::buildForPlayer(state, p.id, previousEvents);
            if (snapshot.arrows == 0) zeroBefore.insert(p.id);
            if (hasObs(snapshot, ObservationType::PitNearby)) ++stats.pitWarnings;
            bool choseStaleness = false;
            if (const auto action = chooseActionV213(
                    snapshot, memories[p.id], sweeps[p.id], ammoMemories[p.id],
                    staleMemories[p.id], matchSeed, stats, legacy, v26, v210, v211,
                    v213, choseStaleness)) {
                selected.push_back(*action);
                if (choseStaleness) stalenessMovers.insert(p.id);
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
        collectV27Events(previousEvents, v27);
        collectV28Events(previousEvents, v28);

        for (const auto& event : previousEvents) {
            if (event.type == GameEventType::ArrowFound && event.actor.has_value()) {
                if (event.cave.has_value() &&
                    ammoMemories[*event.actor].seenLooseArrowCaves.erase(*event.cave) > 0) {
                    ++v210.rememberedArrowRecoveries;
                }
                if (zeroBefore.contains(*event.actor)) ++v26.zeroArrowRecoveries;
                if (stalenessMovers.contains(*event.actor))
                    ++v213.arrowRecoveriesDuringStalenessPatrol;
            }
        }

        if (!countedSecond && state.basilisk.trueEncounters >= 2) {
            ++stats.secondEncounterMatches; countedSecond = true;
        }
        if (!countedThird && state.basilisk.trueEncounters >= 3) {
            ++stats.thirdEncounterMatches; countedThird = true;
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
        diagnoseV213Stall(state, previousEvents, v213);
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
    V210Stats v210; V211Stats v211; V213Stats v213;

    for (std::uint64_t i = 0; i < matches; ++i)
        runOneV213(firstMapSeed + static_cast<MapSeed>(i),
                   firstMatchSeed + static_cast<MatchSeed>(i),
                   maxRounds, stats, legacy, v26, v27, v28, v210, v211, v213);

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V2.14 CADENCE A/B)\n";
    std::cout << "Matches: " << stats.matches << " | max rounds/match: " << maxRounds
              << " | loose-arrow cap: 8 | spawn cadence: every 5 rounds\n\n";

    std::cout << "OUTCOMES\n";
    printPercent("Completed", stats.completed, stats.matches);
    printPercent("Stalled at round cap", stats.stalled, stats.matches);
    printPercent("Basilisk kills", stats.basiliskWins, stats.matches);
    printPercent("Simultaneous Basilisk draws", stats.simultaneousBasiliskDraws, stats.matches);
    printPercent("Extraction wins", stats.extractionWins, stats.matches);
    printPercent("Other draws", stats.draws, stats.matches);

    printV27(v27);
    std::cout << "Survey Fragments found/used: " << v28.surveysFound << '/' << v28.surveysUsed << '\n';
    std::cout << "Blood Bait found/used: " << v28.bloodBaitFound << '/' << v28.bloodBaitUsed << '\n';
    std::cout << "Blood Bait influenced Basilisk moves: " << v28.baitInfluencedMoves << '\n';

    std::cout << "\nV2.13 STALENESS PATROL TELEMETRY\n";
    std::cout << "Zero-arrow player-rounds: " << v26.zeroArrowPlayerRounds << '\n';
    std::cout << "Staleness-patrol moves: " << v213.stalenessPatrolMoves << '\n';
    std::cout << "Staleness targets selected: " << v213.stalenessTargetsSelected << '\n';
    const double avgAge = v213.stalenessTargetsSelected == 0 ? 0.0 :
        static_cast<double>(v213.targetAgeTotal) / static_cast<double>(v213.stalenessTargetsSelected);
    std::cout << "Average oldest-target age: " << avgAge << '\n';
    std::cout << "Zero-arrow recoveries: " << v26.zeroArrowRecoveries << '\n';
    std::cout << "Arrow recoveries during staleness patrol: "
              << v213.arrowRecoveriesDuringStalenessPatrol << '\n';
    std::cout << "Remembered-arrow pursuit moves: " << v210.rememberedArrowPursuitMoves << '\n';
    std::cout << "Remembered-arrow recoveries: " << v210.rememberedArrowRecoveries << '\n';
    std::cout << "Stalled matches: " << v26.stalled << '\n';
    std::cout << "Stalled with all living hunters at zero arrows: "
              << v26.stalledAllZeroArrows << '\n';
    std::cout << "Stalled zero-arrow hunters with safely reachable arrows: "
              << v213.stalledZeroArrowHuntersWithReachableArrow << '\n';
    const double avgNearest = v213.nearestReachableArrowDistanceSamples == 0 ? 0.0 :
        static_cast<double>(v213.nearestReachableArrowDistanceTotal) /
        static_cast<double>(v213.nearestReachableArrowDistanceSamples);
    std::cout << "Average nearest reachable arrow distance at cap: " << avgNearest << '\n';
    std::cout << "Maximum nearest reachable arrow distance at cap: "
              << v213.maxNearestReachableArrowDistance << '\n';

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
    std::cout << "Loose arrows spawned/found/fired: " << stats.looseArrowSpawns << '/'
              << stats.arrowsFound << '/' << stats.arrowsFired << '\n';
    std::cout << "Basilisk true encounters/evades: " << stats.basiliskEncounters << '/'
              << stats.basiliskEvades << '\n';
    return 0;
}
