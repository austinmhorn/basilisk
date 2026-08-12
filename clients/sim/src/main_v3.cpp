#define main basilisk_v214_main
#include "main_v213.cpp"
#undef main

#include <array>
#include <iomanip>

namespace {

enum class Playstyle : std::size_t {
    Hunter = 0,
    Scavenger = 1,
    Opportunist = 2,
    Extractor = 3,
    Count = 4
};

constexpr std::size_t kStyleCount = static_cast<std::size_t>(Playstyle::Count);

const char* styleName(Playstyle style) {
    switch (style) {
        case Playstyle::Hunter: return "Hunter";
        case Playstyle::Scavenger: return "Scavenger";
        case Playstyle::Opportunist: return "Opportunist";
        case Playstyle::Extractor: return "Extractor";
        case Playstyle::Count: break;
    }
    return "Unknown";
}

std::uint64_t mix64(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

Playstyle styleFor(MatchSeed seed, PlayerId player) {
    const auto mixed = mix64(static_cast<std::uint64_t>(seed) ^
                             (static_cast<std::uint64_t>(player) * 0xD6E8FEB86659FD93ULL));
    return static_cast<Playstyle>(mixed % kStyleCount);
}

struct StyleStats {
    std::uint64_t assignments{0};
    std::uint64_t wins{0};
    std::uint64_t basiliskWins{0};
    std::uint64_t extractionWins{0};
    std::uint64_t searches{0};
    std::uint64_t shots{0};
    std::uint64_t pvpShots{0};
    std::uint64_t deaths{0};
};

struct V3Stats {
    std::array<StyleStats, kStyleCount> style{};
    std::array<std::array<std::uint64_t, kStyleCount>, kStyleCount> matchups{};
};

StyleStats& statsFor(V3Stats& stats, Playstyle style) {
    return stats.style[static_cast<std::size_t>(style)];
}

void stripObservation(PlayerRoundSnapshot& snapshot, ObservationType type) {
    snapshot.observations.erase(
        std::remove_if(snapshot.observations.begin(), snapshot.observations.end(),
            [type](const PlayerObservation& observation) {
                return observation.type == type;
            }),
        snapshot.observations.end());
}

const AvailableAction* deterministicActionOfType(
    const PlayerRoundSnapshot& snapshot,
    ActionType type,
    std::uint64_t salt) {
    std::vector<const AvailableAction*> choices;
    for (const auto& action : snapshot.availableActions)
        if (action.type == type) choices.push_back(&action);
    if (choices.empty()) return nullptr;
    return choices[static_cast<std::size_t>(salt % choices.size())];
}

bool extractorObjectiveMode(const PlayerRoundSnapshot& snapshot, const BotMemory& memory) {
    return memory.rivalDead || hasObs(snapshot, ObservationType::RivalDied) || snapshot.hasHunterSigil;
}

std::optional<PlayerAction> chooseActionV3(
    Playstyle style,
    const PlayerRoundSnapshot& original,
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

    if (!original.alive || original.availableActions.empty()) return std::nullopt;

    const std::uint64_t salt = mix64(static_cast<std::uint64_t>(matchSeed) ^
        (static_cast<std::uint64_t>(original.round) << 17U) ^
        static_cast<std::uint64_t>(original.player));

    // Everybody keeps emergency/objective actions above personality flavor.
    for (const auto& action : original.availableActions) {
        if (action.type == ActionType::Contextual &&
            action.contextualAction == ContextualActionType::Escape)
            return materialize(original.player, action);
    }

    if (original.health <= 60) {
        if (const auto* heal = useItemAction(original, ItemType::HealingDraught))
            return materialize(original.player, *heal);
    }

    // SCAVENGER: spend more tempo searching untouched caves for the weighted
    // loot table, but do not override immediate Pit investigation or extraction.
    if (style == Playstyle::Scavenger &&
        !original.hasHunterSigil &&
        !hasObs(original, ObservationType::PitNearby)) {
        auto& cave = memory.caves[original.currentCave];
        if (cave.searches == 0) {
            if (const auto* search = searchAction(original)) {
                ++cave.searches;
                return materialize(original.player, *search);
            }
        }
    }

    // OPPORTUNIST: rival noise is enough to justify a speculative shot. Unlike
    // V2.11, this personality is willing to spend its final arrow on PvP.
    if (style == Playstyle::Opportunist &&
        original.arrows > 0 &&
        hasObs(original, ObservationType::RivalNearby) &&
        !hasAdjacentBasiliskClue(original) &&
        !hasExactEnragedShot(original)) {
        if (const auto* shot = deterministicActionOfType(original, ActionType::Shoot, salt))
            return materialize(original.player, *shot);
    }

    PlayerRoundSnapshot filtered = original;

    // EXTRACTOR: after rival death the alternate objective is the mission.
    // Suppress combat distractions but retain hazards, utility information,
    // body searches, Sigil routing, and extraction from the shared brain.
    if (style == Playstyle::Extractor && extractorObjectiveMode(original, memory)) {
        memory.rivalDead = true;
        stripObservation(filtered, ObservationType::BasiliskNearby);
        stripObservation(filtered, ObservationType::BasiliskNearbySubtle);
        stripObservation(filtered, ObservationType::RestlessBasiliskNoise);
        stripObservation(filtered, ObservationType::EnragedLastKnownCave);
        stripObservation(filtered, ObservationType::RivalNearby);
    }

    return chooseActionV213(
        filtered, memory, sweep, ammo, stale, matchSeed,
        stats, legacy, v26, v210, v211, v213, choseStalenessMove);
}

void runOneV3(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds,
              Stats& stats, V25Stats& legacy, V26Stats& v26,
              V27Stats& v27, V28Stats& v28, V210Stats& v210,
              V211Stats& v211, V213Stats& v213, V3Stats& v3) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);

    std::unordered_map<PlayerId, Playstyle> styles;
    std::unordered_map<PlayerId, BotMemory> memories;
    std::unordered_map<PlayerId, SweepMemory> sweeps;
    std::unordered_map<PlayerId, AmmoMemory> ammoMemories;
    std::unordered_map<PlayerId, StalenessMemory> staleMemories;
    std::unordered_set<PlayerId> stalenessMovers;
    std::unordered_set<PlayerId> pitDeadPlayers;
    std::unordered_set<PlayerId> deadCounted;
    std::vector<GameEvent> previousEvents;
    bool countedSecond = false, countedThird = false;

    for (const auto& player : state.players) {
        const auto style = styleFor(matchSeed, player.id);
        styles[player.id] = style;
        ++statsFor(v3, style).assignments;
    }
    if (state.players.size() >= 2) {
        const auto a = static_cast<std::size_t>(styles[state.players[0].id]);
        const auto b = static_cast<std::size_t>(styles[state.players[1].id]);
        ++v3.matchups[a][b];
    }

    while (state.result.status == MatchStatus::Active && state.round <= maxRounds) {
        std::vector<PlayerAction> selected;
        std::unordered_set<PlayerId> zeroBefore;
        stalenessMovers.clear();

        for (const auto& player : state.players) {
            if (!player.alive) continue;
            const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
            if (snapshot.arrows == 0) zeroBefore.insert(player.id);
            if (hasObs(snapshot, ObservationType::PitNearby)) ++stats.pitWarnings;

            bool choseStaleness = false;
            const auto action = chooseActionV3(
                styles[player.id], snapshot, memories[player.id], sweeps[player.id],
                ammoMemories[player.id], staleMemories[player.id], matchSeed,
                stats, legacy, v26, v210, v211, v213, choseStaleness);

            if (action.has_value()) {
                selected.push_back(*action);
                auto& styleStats = statsFor(v3, styles[player.id]);
                if (action->type == ActionType::Search) ++styleStats.searches;
                if (action->type == ActionType::Shoot) {
                    ++styleStats.shots;
                    if (hasObs(snapshot, ObservationType::RivalNearby)) ++styleStats.pvpShots;
                }
                if (choseStaleness) stalenessMovers.insert(player.id);
            }
        }

        if (selected.empty()) break;
        bool submitOk = true;
        for (const auto& action : selected) submitOk &= coordinator.submitAction(action);
        if (!submitOk) break;
        bool lockOk = true;
        for (const auto& action : selected) lockOk &= coordinator.lockAction(action.player);
        if (!lockOk) break;

        previousEvents = coordinator.lastEvents();
        collectEventStats(previousEvents, stats, state, pitDeadPlayers);
        collectV27Events(previousEvents, v27);
        collectV28Events(previousEvents, v28);

        for (const auto& event : previousEvents) {
            if (event.type == GameEventType::ArrowFound && event.actor.has_value()) {
                if (event.cave.has_value() &&
                    ammoMemories[*event.actor].seenLooseArrowCaves.erase(*event.cave) > 0)
                    ++v210.rememberedArrowRecoveries;
                if (zeroBefore.contains(*event.actor)) ++v26.zeroArrowRecoveries;
                if (stalenessMovers.contains(*event.actor))
                    ++v213.arrowRecoveriesDuringStalenessPatrol;
            }
            if (event.type == GameEventType::PlayerKilled && event.targetPlayer.has_value()) {
                const PlayerId dead = *event.targetPlayer;
                if (styles.contains(dead) && deadCounted.insert(dead).second)
                    ++statsFor(v3, styles[dead]).deaths;
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
    for (const auto& player : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
        stats.totalCaves += snapshot.map.caves.size();
        stats.totalFinalArrows += std::max(0, player.arrows);
    }

    if (state.result.status != MatchStatus::Completed) {
        ++stats.stalled;
        diagnoseV26Stall(state, previousEvents, v26);
        diagnoseV213Stall(state, previousEvents, v213);
        return;
    }

    ++stats.completed;
    switch (state.result.outcome) {
        case MatchOutcome::BasiliskKilled:
            ++stats.basiliskWins;
            if (state.result.winner.has_value() && styles.contains(*state.result.winner)) {
                auto& ss = statsFor(v3, styles[*state.result.winner]);
                ++ss.wins;
                ++ss.basiliskWins;
            }
            break;
        case MatchOutcome::SimultaneousBasiliskKill:
            ++stats.simultaneousBasiliskDraws;
            break;
        case MatchOutcome::EscapedWithSigil:
            ++stats.extractionWins;
            if (state.result.winner.has_value() && styles.contains(*state.result.winner)) {
                auto& ss = statsFor(v3, styles[*state.result.winner]);
                ++ss.wins;
                ++ss.extractionWins;
            }
            break;
        case MatchOutcome::Draw:
            ++stats.draws;
            if (pitDeadPlayers.size() >= 2) ++stats.mutualPitDraws;
            break;
        case MatchOutcome::None:
            break;
    }
}

void printStyleStats(const V3Stats& v3) {
    std::cout << "\nPLAYSTYLE PERFORMANCE\n";
    for (std::size_t i = 0; i < kStyleCount; ++i) {
        const auto style = static_cast<Playstyle>(i);
        const auto& s = v3.style[i];
        const double winRate = s.assignments == 0 ? 0.0 :
            100.0 * static_cast<double>(s.wins) / static_cast<double>(s.assignments);
        std::cout << styleName(style)
                  << " | assigned=" << s.assignments
                  << " wins=" << s.wins
                  << " (" << std::fixed << std::setprecision(1) << winRate << "%)"
                  << " basilisk=" << s.basiliskWins
                  << " extraction=" << s.extractionWins
                  << " searches=" << s.searches
                  << " shots=" << s.shots
                  << " pvpShots=" << s.pvpShots
                  << " deaths=" << s.deaths << '\n';
    }

    std::cout << "\nMATCHUP COUNTS (rows=P1, columns=P2)\n";
    std::cout << "             Hunter Scavenger Opportunist Extractor\n";
    for (std::size_t row = 0; row < kStyleCount; ++row) {
        std::cout << std::setw(11) << styleName(static_cast<Playstyle>(row)) << ' ';
        for (std::size_t col = 0; col < kStyleCount; ++col)
            std::cout << std::setw(10) << v3.matchups[row][col];
        std::cout << '\n';
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
    V213Stats v213;
    V3Stats v3;

    for (std::uint64_t i = 0; i < matches; ++i) {
        runOneV3(firstMapSeed + static_cast<MapSeed>(i),
                 firstMatchSeed + static_cast<MatchSeed>(i),
                 maxRounds, stats, legacy, v26, v27, v28, v210, v211, v213, v3);
    }

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V3 PLAYSTYLES)\n";
    std::cout << "Matches: " << stats.matches
              << " | max rounds/match: " << maxRounds
              << " | loose-arrow cap: 8 | spawn cadence: every 5 rounds\n\n";

    std::cout << "OUTCOMES\n";
    printPercent("Completed", stats.completed, stats.matches);
    printPercent("Stalled at round cap", stats.stalled, stats.matches);
    printPercent("Basilisk kills", stats.basiliskWins, stats.matches);
    printPercent("Simultaneous Basilisk draws", stats.simultaneousBasiliskDraws, stats.matches);
    printPercent("Extraction wins", stats.extractionWins, stats.matches);
    printPercent("Other draws", stats.draws, stats.matches);

    printStyleStats(v3);

    std::cout << "\nRESOURCE / OBJECTIVE SNAPSHOT\n";
    std::cout << "Pit deaths / mutual-Pit draws: " << stats.pitDeaths << '/' << stats.mutualPitDraws << '\n';
    std::cout << "Bodies created/found: " << stats.bodiesCreated << '/' << stats.bodiesFound << '\n';
    std::cout << "Sigils acquired / escapes: " << stats.sigilsAcquired << '/' << stats.escaped << '\n';
    std::cout << "Loose arrows spawned/found/fired: " << stats.looseArrowSpawns << '/'
              << stats.arrowsFound << '/' << stats.arrowsFired << '\n';
    std::cout << "Basilisk true encounters/evades: " << stats.basiliskEncounters << '/'
              << stats.basiliskEvades << '\n';
    std::cout << "Zero-arrow player-rounds: " << v26.zeroArrowPlayerRounds << '\n';
    std::cout << "Stalled zero-arrow hunters with safely reachable arrows: "
              << v213.stalledZeroArrowHuntersWithReachableArrow << '\n';

    return 0;
}
