#define BASILISK_SIM_V27_NO_MAIN
#include "main_v27.cpp"
#undef BASILISK_SIM_V27_NO_MAIN

namespace {

struct V28Stats {
    std::uint64_t surveysFound{0};
    std::uint64_t surveysUsed{0};
    std::uint64_t bloodBaitFound{0};
    std::uint64_t bloodBaitUsed{0};
    std::uint64_t baitInfluencedMoves{0};
};

const AvailableAction* firstSurveyAction(const PlayerRoundSnapshot& s) {
    for (const auto& a : s.availableActions) {
        if (a.type == ActionType::UseItem &&
            a.targetItem == ItemType::SurveyFragment &&
            a.targetTunnel.has_value()) return &a;
    }
    return nullptr;
}

std::optional<PlayerAction> chooseActionV28(const PlayerRoundSnapshot& s,
                                             BotMemory& memory,
                                             SweepMemory& sweep,
                                             MatchSeed matchSeed,
                                             Stats& stats,
                                             V25Stats& legacy,
                                             V26Stats& v26) {
    if (s.health <= 60)
        if (const auto* heal = useItemAction(s, ItemType::HealingDraught))
            return materialize(s.player, *heal);

    if (hasObs(s, ObservationType::JackalNearby))
        if (const auto* repel = useItemAction(s, ItemType::JackalRepellent))
            return materialize(s.player, *repel);

    if (hasObs(s, ObservationType::PitNearby) && s.temporarilyRevealedPitCaves.empty())
        if (const auto* map = useItemAction(s, ItemType::OldMinersMap))
            return materialize(s.player, *map);

    if (s.arrows > 0 && basiliskClue(s))
        if (const auto* bait = useItemAction(s, ItemType::BloodBait))
            return materialize(s.player, *bait);

    if (const auto* survey = firstSurveyAction(s))
        return materialize(s.player, *survey);

    return chooseActionV26(s, memory, sweep, matchSeed, stats, legacy, v26);
}

void collectV28Events(const std::vector<GameEvent>& events, V28Stats& v28) {
    for (const auto& event : events) {
        if (event.type == GameEventType::ItemFound && event.itemType.has_value()) {
            if (*event.itemType == ItemType::SurveyFragment) ++v28.surveysFound;
            if (*event.itemType == ItemType::BloodBait) ++v28.bloodBaitFound;
        }
        if (event.type == GameEventType::ItemUsed && event.itemType.has_value()) {
            if (*event.itemType == ItemType::SurveyFragment) ++v28.surveysUsed;
            if (*event.itemType == ItemType::BloodBait) ++v28.bloodBaitUsed;
        }
        if (event.type == GameEventType::BasiliskBaitInfluencedMove)
            ++v28.baitInfluencedMoves;
    }
}

void runOneV28(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
               Stats& stats, V25Stats& legacy, V26Stats& v26,
               V27Stats& v27, V28Stats& v28) {
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
            if (const auto action = chooseActionV28(snapshot, memories[p.id], sweeps[p.id],
                                                     matchSeed, stats, legacy, v26))
                selected.push_back(*action);
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
            if (event.type == GameEventType::ArrowFound && event.actor.has_value() &&
                zeroBefore.contains(*event.actor)) {
                // V2.9 convergence fix: count the recovery, but deliberately
                // preserve the unfinished exhaustive sweep. If the recovered
                // arrow is fired, the hunter resumes the same patrol cycle.
                ++v26.zeroArrowRecoveries;
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

#ifndef BASILISK_SIM_V28_NO_MAIN
int main(int argc, char** argv) {
    std::uint64_t matches = 1000, maxRounds = 250;
    MapSeed firstMapSeed = 100000;
    MatchSeed firstMatchSeed = 500000;
    if (argc > 1) matches = std::stoull(argv[1]);
    if (argc > 2) maxRounds = std::stoull(argv[2]);
    if (argc > 3) firstMapSeed = static_cast<MapSeed>(std::stoull(argv[3]));
    if (argc > 4) firstMatchSeed = static_cast<MatchSeed>(std::stoull(argv[4]));

    Stats stats; V25Stats legacy; V26Stats v26; V27Stats v27; V28Stats v28;
    for (std::uint64_t i = 0; i < matches; ++i)
        runOneV28(firstMapSeed + static_cast<MapSeed>(i),
                  firstMatchSeed + static_cast<MatchSeed>(i),
                  maxRounds, stats, legacy, v26, v27, v28);

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V2.8 WEIGHTED LOOT)\n";
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
#endif