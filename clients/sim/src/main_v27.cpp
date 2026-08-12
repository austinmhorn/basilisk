#include "main_v26_support.hpp"

namespace {

struct V27Stats {
    std::uint64_t healingFound{0};
    std::uint64_t mapsFound{0};
    std::uint64_t repellentsFound{0};
    std::uint64_t inventoryFullDrops{0};
    std::uint64_t exoticCallingCards{0};
    std::uint64_t healingUsed{0};
    std::uint64_t mapsUsed{0};
    std::uint64_t repellentsUsed{0};
    std::uint64_t jackalAttacksRepelled{0};
    std::uint64_t healedHp{0};
};

const AvailableAction* useItemAction(const PlayerRoundSnapshot& s, ItemType item) {
    for (const auto& a : s.availableActions) {
        if (a.type == ActionType::UseItem && a.targetItem == item) return &a;
    }
    return nullptr;
}

std::optional<PlayerAction> chooseActionV27(const PlayerRoundSnapshot& s,
                                             BotMemory& memory,
                                             SweepMemory& sweep,
                                             MatchSeed matchSeed,
                                             Stats& stats,
                                             V25Stats& legacy,
                                             V26Stats& v26) {
    if (s.health <= 60) {
        if (const auto* heal = useItemAction(s, ItemType::HealingDraught)) {
            return materialize(s.player, *heal);
        }
    }

    if (hasObs(s, ObservationType::JackalNearby)) {
        if (const auto* repel = useItemAction(s, ItemType::JackalRepellent)) {
            return materialize(s.player, *repel);
        }
    }

    if (hasObs(s, ObservationType::PitNearby) &&
        s.temporarilyRevealedPitCaves.empty()) {
        if (const auto* map = useItemAction(s, ItemType::OldMinersMap)) {
            return materialize(s.player, *map);
        }
    }

    return chooseActionV26(s, memory, sweep, matchSeed, stats, legacy, v26);
}

void collectV27Events(const std::vector<GameEvent>& events, V27Stats& v27) {
    for (const auto& event : events) {
        if (event.type == GameEventType::ItemFound && event.itemType.has_value()) {
            switch (*event.itemType) {
                case ItemType::HealingDraught: ++v27.healingFound; break;
                case ItemType::OldMinersMap: ++v27.mapsFound; break;
                case ItemType::JackalRepellent: ++v27.repellentsFound; break;
                case ItemType::SurveyFragment:
                case ItemType::BloodBait: break;
            }
        }
        if (event.type == GameEventType::InventoryFull) ++v27.inventoryFullDrops;
        if (event.type == GameEventType::ExoticCallingCardFound) ++v27.exoticCallingCards;
        if (event.type == GameEventType::JackalRepelled) ++v27.jackalAttacksRepelled;
        if (event.type == GameEventType::PlayerHealed)
            v27.healedHp += static_cast<std::uint64_t>(std::max(0, event.amount));

        if (event.type == GameEventType::ItemUsed && event.itemType.has_value()) {
            switch (*event.itemType) {
                case ItemType::HealingDraught: ++v27.healingUsed; break;
                case ItemType::OldMinersMap: ++v27.mapsUsed; break;
                case ItemType::JackalRepellent: ++v27.repellentsUsed; break;
                case ItemType::SurveyFragment:
                case ItemType::BloodBait: break;
            }
        }
    }
}

void runOneV27(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
               Stats& stats, V25Stats& legacy, V26Stats& v26, V27Stats& v27) {
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
            if (const auto action = chooseActionV27(snapshot, memories[p.id], sweeps[p.id],
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
        collectV27Events(previousEvents, v27);

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

void printV27(const V27Stats& v27) {
    std::cout << "\nSEARCH LOOT / ITEM TELEMETRY\n";
    std::cout << "Healing Draughts found/used: " << v27.healingFound << '/' << v27.healingUsed << '\n';
    std::cout << "Old Miner's Maps found/used: " << v27.mapsFound << '/' << v27.mapsUsed << '\n';
    std::cout << "Jackal Repellents found/used: " << v27.repellentsFound << '/' << v27.repellentsUsed << '\n';
    std::cout << "Inventory-full rejected drops: " << v27.inventoryFullDrops << '\n';
    std::cout << "Exotic Calling Cards discovered: " << v27.exoticCallingCards << '\n';
    std::cout << "Total HP restored: " << v27.healedHp << '\n';
    std::cout << "Jackal attacks repelled: " << v27.jackalAttacksRepelled << '\n';
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
    for (std::uint64_t i = 0; i < matches; ++i) {
        runOneV27(firstMapSeed + static_cast<MapSeed>(i),
                  firstMatchSeed + static_cast<MatchSeed>(i),
                  maxRounds, stats, legacy, v26, v27);
    }

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V2.7 LOOT)\n";
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
