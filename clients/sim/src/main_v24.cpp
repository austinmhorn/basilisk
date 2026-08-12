#define main basilisk_v23_main
#include "main_v23.cpp"
#undef main

#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace {

struct StallStats {
    std::uint64_t stalled{0};
    std::uint64_t twoAlive{0}, oneAlive{0}, zeroAlive{0};
    std::uint64_t basiliskAlive{0};
    std::uint64_t bothZeroArrows{0}, anyZeroArrows{0}, looseArrowsAvailable{0};
    std::uint64_t anyFrontier{0}, anySafeReachableFrontier{0}, pitQuarantine{0}, noFrontier{0};
    std::uint64_t objectiveNone{0}, bodyWaiting{0}, sigilHeld{0}, extractionActive{0};
    std::uint64_t highLooping{0}, severeLooping{0};
    std::uint64_t totalLivingDiscovered{0}, livingSamples{0};
    std::uint64_t totalBasiliskDistance{0}, basiliskDistanceSamples{0};
    std::vector<std::string> examples;
};

std::optional<int> graphDistance(const WorldGraph& world, CaveId start, CaveId target) {
    if (!world.contains(start) || !world.contains(target)) return std::nullopt;
    if (start == target) return 0;
    std::queue<std::pair<CaveId, int>> q;
    std::unordered_set<CaveId> seen;
    q.push({start, 0});
    seen.insert(start);
    while (!q.empty()) {
        const auto [cur, dist] = q.front(); q.pop();
        for (const CaveId next : world.cave(cur).connections) {
            if (!seen.insert(next).second) continue;
            if (next == target) return dist + 1;
            q.push({next, dist + 1});
        }
    }
    return std::nullopt;
}

bool caveHasFrontier(const DiscoveredCaveView& cave) {
    return std::any_of(cave.exits.begin(), cave.exits.end(),
        [](const TunnelView& t) { return !t.destination.has_value(); });
}

bool caveHasSafeFrontier(const DiscoveredCaveView& cave) {
    return std::any_of(cave.exits.begin(), cave.exits.end(),
        [](const TunnelView& t) { return !t.destination.has_value() && !t.strongColdDraft; });
}

const DiscoveredCaveView* findView(const PlayerRoundSnapshot& s, CaveId cave) {
    const auto it = std::find_if(s.map.caves.begin(), s.map.caves.end(),
        [cave](const DiscoveredCaveView& c) { return c.cave == cave; });
    return it == s.map.caves.end() ? nullptr : &*it;
}

bool connectionMarkedDangerous(const PlayerRoundSnapshot& s, CaveId from, CaveId to) {
    const auto* view = findView(s, from);
    if (!view) return false;
    return std::any_of(view->exits.begin(), view->exits.end(), [&](const TunnelView& t) {
        return t.destination == to && t.strongColdDraft;
    });
}

bool hasSafeReachableFrontier(const PlayerRoundSnapshot& s) {
    std::queue<CaveId> q;
    std::unordered_set<CaveId> seen;
    q.push(s.currentCave);
    seen.insert(s.currentCave);
    while (!q.empty()) {
        const CaveId cur = q.front(); q.pop();
        const auto* view = findView(s, cur);
        if (!view) continue;
        if (caveHasSafeFrontier(*view)) return true;
        for (const auto& tunnel : view->exits) {
            if (!tunnel.destination.has_value() || tunnel.strongColdDraft) continue;
            const CaveId next = *tunnel.destination;
            if (connectionMarkedDangerous(s, next, cur)) continue;
            if (seen.insert(next).second) q.push(next);
        }
    }
    return false;
}

std::size_t maxVisits(const BotMemory& memory) {
    std::size_t result = 0;
    for (const auto& [_, cave] : memory.caves)
        result = std::max(result, static_cast<std::size_t>(cave.visits));
    return result;
}

std::string objectiveLabel(const MatchState& state) {
    if (state.extraction.active) return "extraction-active";
    for (const auto& p : state.players) if (p.alive && p.heldSigilFrom.has_value()) return "sigil-held";
    for (const auto& b : state.bodies) if (b.sigilAvailable) return "body/sigil-waiting";
    return "none";
}

void diagnoseStall(const MatchState& state,
                   const std::vector<GameEvent>& previousEvents,
                   const std::unordered_map<PlayerId, BotMemory>& memories,
                   MapSeed mapSeed,
                   MatchSeed matchSeed,
                   StallStats& diag) {
    ++diag.stalled;
    std::vector<const PlayerState*> living;
    for (const auto& p : state.players) if (p.alive) living.push_back(&p);
    if (living.size() == 2) ++diag.twoAlive;
    else if (living.size() == 1) ++diag.oneAlive;
    else ++diag.zeroAlive;
    if (state.basilisk.alive) ++diag.basiliskAlive;

    bool anyZero = false;
    bool allZero = !living.empty();
    bool frontier = false;
    bool safeFrontier = false;
    std::size_t worstVisits = 0;
    std::vector<std::string> playerBits;

    for (const auto* p : living) {
        anyZero |= p->arrows == 0;
        allZero &= p->arrows == 0;
        const auto snapshot = SnapshotSystem::buildForPlayer(state, p->id, previousEvents);
        diag.totalLivingDiscovered += snapshot.map.caves.size();
        ++diag.livingSamples;
        const bool playerFrontier = std::any_of(snapshot.map.caves.begin(), snapshot.map.caves.end(), caveHasFrontier);
        const bool playerSafeFrontier = hasSafeReachableFrontier(snapshot);
        frontier |= playerFrontier;
        safeFrontier |= playerSafeFrontier;
        const auto memIt = memories.find(p->id);
        const std::size_t visits = memIt == memories.end() ? 0 : maxVisits(memIt->second);
        worstVisits = std::max(worstVisits, visits);
        if (state.basilisk.alive) {
            if (const auto d = graphDistance(state.world, p->cave, state.basilisk.cave)) {
                diag.totalBasiliskDistance += *d;
                ++diag.basiliskDistanceSamples;
            }
        }
        std::ostringstream bit;
        bit << "P" << p->id << " cave=" << p->cave << " arrows=" << p->arrows
            << " discovered=" << snapshot.map.caves.size()
            << " frontier=" << (playerFrontier ? "Y" : "N")
            << " safeFrontier=" << (playerSafeFrontier ? "Y" : "N")
            << " maxVisits=" << visits;
        playerBits.push_back(bit.str());
    }

    if (anyZero) ++diag.anyZeroArrows;
    if (allZero) ++diag.bothZeroArrows;
    if (!state.looseArrows.empty()) ++diag.looseArrowsAvailable;
    if (frontier) ++diag.anyFrontier; else ++diag.noFrontier;
    if (safeFrontier) ++diag.anySafeReachableFrontier;
    if (frontier && !safeFrontier) ++diag.pitQuarantine;
    if (worstVisits >= 20) ++diag.highLooping;
    if (worstVisits >= 50) ++diag.severeLooping;

    const std::string objective = objectiveLabel(state);
    if (objective == "extraction-active") ++diag.extractionActive;
    else if (objective == "sigil-held") ++diag.sigilHeld;
    else if (objective == "body/sigil-waiting") ++diag.bodyWaiting;
    else ++diag.objectiveNone;

    if (diag.examples.size() < 12) {
        std::ostringstream out;
        out << "map=" << mapSeed << " match=" << matchSeed
            << " alive=" << living.size()
            << " basilisk=" << (state.basilisk.alive ? "alive" : "dead")
            << " objective=" << objective
            << " looseArrows=" << state.looseArrows.size()
            << " | ";
        for (std::size_t i = 0; i < playerBits.size(); ++i) {
            if (i) out << " ; ";
            out << playerBits[i];
        }
        diag.examples.push_back(out.str());
    }
}

void runOneV24(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds, Stats& stats, StallStats& diag) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, BotMemory> memories;
    std::vector<GameEvent> previousEvents;
    std::unordered_set<PlayerId> pitDeadPlayers;
    bool countedSecond = false, countedThird = false;

    while (state.result.status == MatchStatus::Active && state.round <= maxRounds) {
        std::vector<PlayerAction> selected;
        for (const auto& p : state.players) {
            if (!p.alive) continue;
            const auto snapshot = SnapshotSystem::buildForPlayer(state, p.id, previousEvents);
            if (hasObs(snapshot, ObservationType::PitNearby)) ++stats.pitWarnings;
            if (const auto action = chooseAction(snapshot, memories[p.id], matchSeed, stats)) selected.push_back(*action);
        }
        if (selected.empty()) break;
        for (const auto& a : selected) if (!coordinator.submitAction(a)) break;
        for (const auto& a : selected) if (!coordinator.lockAction(a.player)) break;
        previousEvents = coordinator.lastEvents();
        collectEventStats(previousEvents, stats, state, pitDeadPlayers);
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
        diagnoseStall(state, previousEvents, memories, mapSeed, matchSeed, diag);
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

void printDiag(const StallStats& d) {
    std::cout << "\nSTALL AUTOPSY\n";
    std::cout << "Stalled matches diagnosed: " << d.stalled << '\n';
    std::cout << "Alive at cap - two/one/zero: " << d.twoAlive << '/' << d.oneAlive << '/' << d.zeroAlive << '\n';
    std::cout << "Basilisk still alive: " << d.basiliskAlive << '\n';
    std::cout << "Any hunter at 0 arrows: " << d.anyZeroArrows << '\n';
    std::cout << "All living hunters at 0 arrows: " << d.bothZeroArrows << '\n';
    std::cout << "Loose arrows still on map: " << d.looseArrowsAvailable << '\n';
    std::cout << "Any unexplored frontier remains: " << d.anyFrontier << '\n';
    std::cout << "Safe reachable frontier remains: " << d.anySafeReachableFrontier << '\n';
    std::cout << "Frontier exists but all safe routes blocked by known Pit clues: " << d.pitQuarantine << '\n';
    std::cout << "No frontier remains: " << d.noFrontier << '\n';
    std::cout << "Objective none/body waiting/sigil held/extraction active: "
              << d.objectiveNone << '/' << d.bodyWaiting << '/' << d.sigilHeld << '/' << d.extractionActive << '\n';
    std::cout << "Matches with max cave visits >=20: " << d.highLooping << '\n';
    std::cout << "Matches with max cave visits >=50: " << d.severeLooping << '\n';
    const double avgCaves = d.livingSamples ? static_cast<double>(d.totalLivingDiscovered) / d.livingSamples : 0.0;
    const double avgDist = d.basiliskDistanceSamples ? static_cast<double>(d.totalBasiliskDistance) / d.basiliskDistanceSamples : 0.0;
    std::cout << "Average discovered caves among stalled living hunters: " << avgCaves << '\n';
    std::cout << "Average authoritative distance to Basilisk at cap: " << avgDist << '\n';

    std::cout << "\nSTALL EXAMPLES (first " << d.examples.size() << ")\n";
    for (const auto& example : d.examples) std::cout << "- " << example << '\n';
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
    StallStats diag;
    for (std::uint64_t i = 0; i < matches; ++i)
        runOneV24(firstMapSeed + static_cast<MapSeed>(i), firstMatchSeed + static_cast<MatchSeed>(i), maxRounds, stats, diag);

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V2.4 STALL DIAGNOSTICS)\n";
    std::cout << "Matches: " << stats.matches << " | max rounds/match: " << maxRounds << "\n\n";
    std::cout << "OUTCOMES\n";
    printPercent("Completed", stats.completed, stats.matches);
    printPercent("Stalled at round cap", stats.stalled, stats.matches);
    printPercent("Basilisk kills", stats.basiliskWins, stats.matches);
    printPercent("Simultaneous Basilisk draws", stats.simultaneousBasiliskDraws, stats.matches);
    printPercent("Extraction wins", stats.extractionWins, stats.matches);
    printPercent("Other draws", stats.draws, stats.matches);
    printDiag(diag);
    return 0;
}
