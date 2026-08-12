#define BASILISK_SIM_V210_NO_MAIN
#include "main_v210.cpp"
#undef BASILISK_SIM_V210_NO_MAIN

namespace {

struct V211Stats {
    std::uint64_t restlessShotsSuppressed{0};
    std::uint64_t lastArrowPvpShotsSuppressed{0};
    std::uint64_t adjacentBasiliskShots{0};
    std::uint64_t exactEnragedShots{0};
    std::uint64_t pvpShots{0};
};

bool hasAdjacentBasiliskClue(const PlayerRoundSnapshot& s) {
    return hasObs(s, ObservationType::BasiliskNearby) ||
           hasObs(s, ObservationType::BasiliskNearbySubtle);
}

bool hasExactEnragedShot(const PlayerRoundSnapshot& s) {
    if (!hasObs(s, ObservationType::EnragedLastKnownCave)) return false;
    for (const auto& observation : s.observations) {
        if (observation.type != ObservationType::EnragedLastKnownCave ||
            !observation.cave.has_value()) continue;
        if (shootTo(s, *observation.cave) != nullptr) return true;
    }
    return false;
}

void removeObservation(PlayerRoundSnapshot& s, ObservationType type) {
    s.observations.erase(
        std::remove_if(s.observations.begin(), s.observations.end(),
            [type](const PlayerObservation& observation) {
                return observation.type == type;
            }),
        s.observations.end());
}

std::optional<PlayerAction> chooseActionV211(
    const PlayerRoundSnapshot& original,
    BotMemory& memory,
    SweepMemory& sweep,
    AmmoMemory& ammo,
    MatchSeed matchSeed,
    Stats& stats,
    V25Stats& legacy,
    V26Stats& v26,
    V210Stats& v210,
    V211Stats& v211) {

    PlayerRoundSnapshot filtered = original;

    const bool adjacentBasilisk = hasAdjacentBasiliskClue(original);
    const bool exactEnraged = hasExactEnragedShot(original);
    const bool restlessOnly = hasObs(original, ObservationType::RestlessBasiliskNoise) &&
                              !adjacentBasilisk && !exactEnraged;

    if (restlessOnly && original.arrows > 0) {
        removeObservation(filtered, ObservationType::RestlessBasiliskNoise);
        ++v211.restlessShotsSuppressed;
    }

    if (original.arrows == 1 && hasObs(original, ObservationType::RivalNearby) &&
        !adjacentBasilisk && !exactEnraged) {
        removeObservation(filtered, ObservationType::RivalNearby);
        ++v211.lastArrowPvpShotsSuppressed;
    }

    const auto action = chooseActionV210(
        filtered, memory, sweep, ammo, matchSeed, stats, legacy, v26, v210);

    if (action.has_value() && action->type == ActionType::Shoot) {
        if (exactEnraged) ++v211.exactEnragedShots;
        else if (adjacentBasilisk) ++v211.adjacentBasiliskShots;
        else if (hasObs(original, ObservationType::RivalNearby)) ++v211.pvpShots;
    }

    return action;
}

void runOneV211(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
                Stats& stats, V25Stats& legacy, V26Stats& v26,
                V27Stats& v27, V28Stats& v28, V210Stats& v210,
                V211Stats& v211) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, BotMemory> memories;
    std::unordered_map<PlayerId, SweepMemory> sweeps;
    std::unordered_map<PlayerId, AmmoMemory> ammoMemories;
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
            if (const auto action = chooseActionV211(
                    snapshot, memories[p.id], sweeps[p.id], ammoMemories[p.id],
                    matchSeed, stats, legacy, v26, v210, v211)) {
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
        collectV27Events(previousEvents, v27);
        collectV28Events(previousEvents, v28);

        for (const auto& event : previousEvents) {
            if (event.type == GameEventType::ArrowFound && event.actor.has_value()) {
                if (event.cave.has_value() &&
                    ammoMemories[*event.actor].seenLooseArrowCaves.erase(*event.cave) > 0) {
                    ++v210.rememberedArrowRecoveries;
                }
                if (zeroBefore.contains(*event.actor)) ++v26.zeroArrowRecoveries;
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
    V27Stats v27;
    V28Stats v28;
    V210Stats v210;
    V211Stats v211;

    for (std::uint64_t i = 0; i < matches; ++i) {
        runOneV211(firstMapSeed + static_cast<MapSeed>(i),
                   firstMatchSeed + static_cast<MatchSeed>(i),
                   maxRounds, stats, legacy, v26, v27, v28, v210, v211);
    }

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V2.11 SHOT DISCIPLINE)\n";
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
    printV27(v27);
    std::cout << "Survey Fragments found/used: " << v28.surveysFound << '/' << v28.surveysUsed << '\n';
    std::cout << "Blood Bait found/used: " << v28.bloodBaitFound << '/' << v28.bloodBaitUsed << '\n';
    std::cout << "Blood Bait influenced Basilisk moves: " << v28.baitInfluencedMoves << '\n';

    std::cout << "\nV2.10 AMMO MEMORY TELEMETRY\n";
    std::cout << "Loose arrows spotted while at capacity: " << v210.looseArrowSightingsAtCapacity << '\n';
    std::cout << "Remembered-arrow pursuit moves: " << v210.rememberedArrowPursuitMoves << '\n';
    std::cout << "Remembered-arrow recoveries: " << v210.rememberedArrowRecoveries << '\n';
    std::cout << "Remembered locations later found empty: " << v210.rememberedArrowLocationsInvalidated << '\n';

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
