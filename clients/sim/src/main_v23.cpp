#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/Observation.hpp"
#include "basilisk/systems/MatchCoordinator.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

struct CaveMemory {
    int visits{0};
    int searches{0};
};

struct BotMemory {
    std::unordered_map<CaveId, CaveMemory> caves;
    bool rivalDead{false};
    std::size_t lastDiscoveredCount{0};
};

struct Stats {
    std::uint64_t matches{0}, completed{0}, stalled{0};
    std::uint64_t basiliskWins{0}, simultaneousBasiliskDraws{0}, extractionWins{0}, draws{0};
    std::uint64_t pitDeaths{0}, mutualPitDraws{0}, pvpDeaths{0}, pvpHits{0};
    std::uint64_t pitWarnings{0}, pitInvestigations{0}, pitClueSuccesses{0}, pitClueInconclusive{0};
    std::uint64_t knownPitTunnelAvoidances{0}, pitRiskMovesWithoutClue{0};
    std::uint64_t bodiesCreated{0}, bodiesFound{0}, sigilsAcquired{0}, extractionsActivated{0};
    std::uint64_t escapeAvailable{0}, escaped{0}, objectiveSearches{0}, extractionPathMoves{0};
    std::uint64_t basiliskEncounters{0}, basiliskEvades{0};
    std::uint64_t firstKills{0}, secondKills{0}, thirdKills{0};
    std::uint64_t secondEncounterMatches{0}, thirdEncounterMatches{0};
    std::uint64_t looseArrowSpawns{0}, arrowsFound{0}, arrowsFired{0};
    std::uint64_t searches{0}, unexploredMoves{0}, knownMoves{0}, frontierMoves{0};
    std::uint64_t totalRounds{0}, totalCaves{0}, totalFinalArrows{0};
    std::vector<std::uint64_t> roundSamples;
};

bool hasObs(const PlayerRoundSnapshot& s, ObservationType type) {
    return std::any_of(s.observations.begin(), s.observations.end(),
        [type](const PlayerObservation& o) { return o.type == type; });
}

bool basiliskClue(const PlayerRoundSnapshot& s) {
    return hasObs(s, ObservationType::BasiliskNearby) ||
           hasObs(s, ObservationType::BasiliskNearbySubtle) ||
           hasObs(s, ObservationType::RestlessBasiliskNoise) ||
           hasObs(s, ObservationType::EnragedLastKnownCave);
}

PlayerAction materialize(PlayerId player, const AvailableAction& a) {
    PlayerAction out;
    out.player = player;
    out.type = a.type;
    out.targetCave = a.targetCave;
    out.targetTunnel = a.targetTunnel;
    out.targetItem = a.targetItem;
    out.contextualAction = a.contextualAction;
    return out;
}

const DiscoveredCaveView* caveView(const PlayerRoundSnapshot& s, CaveId cave) {
    const auto it = std::find_if(s.map.caves.begin(), s.map.caves.end(),
        [cave](const DiscoveredCaveView& c) { return c.cave == cave; });
    return it == s.map.caves.end() ? nullptr : &*it;
}

const AvailableAction* searchAction(const PlayerRoundSnapshot& s) {
    for (const auto& a : s.availableActions)
        if (a.type == ActionType::Search) return &a;
    return nullptr;
}

std::vector<const AvailableAction*> actionsOfType(const PlayerRoundSnapshot& s, ActionType type) {
    std::vector<const AvailableAction*> out;
    for (const auto& a : s.availableActions)
        if (a.type == type) out.push_back(&a);
    return out;
}

const AvailableAction* pick(const std::vector<const AvailableAction*>& choices, std::uint64_t salt) {
    if (choices.empty()) return nullptr;
    return choices[static_cast<std::size_t>(salt % choices.size())];
}

const AvailableAction* moveTo(const PlayerRoundSnapshot& s, CaveId cave) {
    for (const auto& a : s.availableActions)
        if (a.type == ActionType::Move && a.targetCave == cave) return &a;
    return nullptr;
}

const AvailableAction* shootTo(const PlayerRoundSnapshot& s, CaveId cave) {
    for (const auto& a : s.availableActions)
        if (a.type == ActionType::Shoot && a.targetCave == cave) return &a;
    return nullptr;
}

std::optional<TunnelView> investigatedPitTunnel(const PlayerRoundSnapshot& s) {
    const auto* cave = caveView(s, s.currentCave);
    if (!cave) return std::nullopt;
    const auto it = std::find_if(cave->exits.begin(), cave->exits.end(),
        [](const TunnelView& tunnel) { return tunnel.strongColdDraft; });
    if (it == cave->exits.end()) return std::nullopt;
    return *it;
}

bool actionUsesTunnel(const AvailableAction& action, const TunnelView& tunnel) {
    if (action.targetTunnel.has_value() && action.targetTunnel == tunnel.id) return true;
    if (tunnel.destination.has_value() && action.targetCave == tunnel.destination) return true;
    return false;
}

bool hasUnknownExit(const PlayerRoundSnapshot& s, CaveId cave) {
    const auto* view = caveView(s, cave);
    return view && std::any_of(view->exits.begin(), view->exits.end(),
        [](const TunnelView& tunnel) { return !tunnel.destination.has_value(); });
}

std::optional<CaveId> nextStepTo(const PlayerRoundSnapshot& s, CaveId target) {
    if (s.currentCave == target) return target;
    std::queue<CaveId> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_set<CaveId> seen;
    q.push(s.currentCave);
    seen.insert(s.currentCave);

    while (!q.empty()) {
        const CaveId current = q.front(); q.pop();
        const auto* view = caveView(s, current);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!tunnel.destination.has_value()) continue;
            const CaveId next = *tunnel.destination;
            if (!seen.insert(next).second) continue;
            parent[next] = current;
            if (next == target) {
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

std::optional<CaveId> nearestFrontierStep(const PlayerRoundSnapshot& s) {
    if (hasUnknownExit(s, s.currentCave)) return s.currentCave;
    std::queue<CaveId> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_set<CaveId> seen;
    q.push(s.currentCave);
    seen.insert(s.currentCave);

    while (!q.empty()) {
        const CaveId current = q.front(); q.pop();
        const auto* view = caveView(s, current);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!tunnel.destination.has_value()) continue;
            const CaveId next = *tunnel.destination;
            if (!seen.insert(next).second) continue;
            parent[next] = current;
            if (hasUnknownExit(s, next)) {
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

std::optional<PlayerAction> chooseAction(
    const PlayerRoundSnapshot& s,
    BotMemory& memory,
    MatchSeed matchSeed,
    Stats& stats) {

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

    // V2.3: while warned and the dangerous tunnel is still unknown, spend the
    // round investigating. Inconclusive results are retried next round. Once a
    // clue succeeds, the marked tunnel is permanently excluded from movement.
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
        for (const auto& o : s.observations) {
            if (o.type == ObservationType::EnragedLastKnownCave && o.cave.has_value()) {
                if (const auto* shot = shootTo(s, *o.cave)) return materialize(s.player, *shot);
            }
        }
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

    if (!unknown.empty()) {
        if (const auto* move = pick(unknown, salt >> 5U)) {
            ++stats.unexploredMoves;
            return materialize(s.player, *move);
        }
    }

    if (const auto frontier = nearestFrontierStep(s); frontier.has_value() && *frontier != s.currentCave) {
        if (const auto* move = moveTo(s, *frontier)) {
            if (!pitTunnel.has_value() || !actionUsesTunnel(*move, *pitTunnel)) {
                ++stats.frontierMoves;
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

void collectEventStats(const std::vector<GameEvent>& events, Stats& stats, const MatchState& state,
                       std::unordered_set<PlayerId>& pitDeadPlayers) {
    for (const auto& event : events) {
        switch (event.type) {
            case GameEventType::ArrowFired: ++stats.arrowsFired; break;
            case GameEventType::ArrowHitPlayer: ++stats.pvpHits; break;
            case GameEventType::ArrowReachedBasilisk: ++stats.basiliskEncounters; break;
            case GameEventType::BasiliskEvaded: ++stats.basiliskEvades; break;
            case GameEventType::PitTriggered:
                ++stats.pitDeaths;
                if (event.targetPlayer.has_value()) pitDeadPlayers.insert(*event.targetPlayer);
                break;
            case GameEventType::PitInvestigationSucceeded: ++stats.pitClueSuccesses; break;
            case GameEventType::PitInvestigationInconclusive: ++stats.pitClueInconclusive; break;
            case GameEventType::BodyCreated: ++stats.bodiesCreated; break;
            case GameEventType::BodyFound: ++stats.bodiesFound; break;
            case GameEventType::SigilAcquired: ++stats.sigilsAcquired; break;
            case GameEventType::ExtractionActivated: ++stats.extractionsActivated; break;
            case GameEventType::EscapeAvailable: ++stats.escapeAvailable; break;
            case GameEventType::PlayerEscaped: ++stats.escaped; break;
            case GameEventType::LooseArrowSpawned: ++stats.looseArrowSpawns; break;
            case GameEventType::ArrowFound: stats.arrowsFound += std::max(0, event.amount); break;
            case GameEventType::SearchCompleted: ++stats.searches; break;
            default: break;
        }
    }

    for (const auto& event : events) {
        if (event.type != GameEventType::PlayerKilled || !event.targetPlayer.has_value()) continue;
        if (!pitDeadPlayers.contains(*event.targetPlayer)) ++stats.pvpDeaths;
    }

    const bool killed = std::any_of(events.begin(), events.end(),
        [](const GameEvent& event) { return event.type == GameEventType::BasiliskKilled; });
    if (killed) {
        if (state.basilisk.trueEncounters <= 1) ++stats.firstKills;
        else if (state.basilisk.trueEncounters == 2) ++stats.secondKills;
        else ++stats.thirdKills;
    }
}

void runOne(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds, Stats& stats) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, BotMemory> memories;
    std::vector<GameEvent> previousEvents;
    std::unordered_set<PlayerId> pitDeadPlayers;
    bool reachedSecond = false, reachedThird = false;

    while (state.result.status == MatchStatus::Active && state.round <= maxRounds) {
        std::vector<PlayerAction> selected;
        for (const auto& player : state.players) {
            if (!player.alive) continue;
            const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
            if (hasObs(snapshot, ObservationType::PitNearby)) ++stats.pitWarnings;
            if (const auto action = chooseAction(snapshot, memories[player.id], matchSeed, stats))
                selected.push_back(*action);
        }
        if (selected.empty()) break;

        for (const auto& action : selected)
            if (!coordinator.submitAction(action)) break;
        for (const auto& action : selected)
            if (!coordinator.lockAction(action.player)) break;

        previousEvents = coordinator.lastEvents();
        collectEventStats(previousEvents, stats, state, pitDeadPlayers);

        if (!reachedSecond && state.basilisk.trueEncounters >= 2) {
            ++stats.secondEncounterMatches;
            reachedSecond = true;
        }
        if (!reachedThird && state.basilisk.trueEncounters >= 3) {
            ++stats.thirdEncounterMatches;
            reachedThird = true;
        }
    }

    ++stats.matches;
    const auto rounds = std::min<std::uint64_t>(state.round, maxRounds);
    stats.totalRounds += rounds;
    stats.roundSamples.push_back(rounds);

    for (const auto& player : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
        stats.totalCaves += snapshot.map.caves.size();
        stats.totalFinalArrows += static_cast<std::uint64_t>(std::max(0, player.arrows));
    }

    if (state.result.status != MatchStatus::Completed) {
        ++stats.stalled;
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

void printPercent(const char* label, std::uint64_t value, std::uint64_t total) {
    const double pct = total == 0 ? 0.0 : 100.0 * static_cast<double>(value) / total;
    std::cout << label << ": " << value << " (" << pct << "%)\n";
}

std::uint64_t percentile(std::vector<std::uint64_t> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1))];
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
    for (std::uint64_t i = 0; i < matches; ++i)
        runOne(firstMapSeed + static_cast<MapSeed>(i),
               firstMatchSeed + static_cast<MatchSeed>(i), maxRounds, stats);

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V2.3)\n";
    std::cout << "Matches: " << stats.matches << " | max rounds/match: " << maxRounds << "\n\n";

    std::cout << "OUTCOMES\n";
    printPercent("Completed", stats.completed, stats.matches);
    printPercent("Stalled at round cap", stats.stalled, stats.matches);
    printPercent("Basilisk kills", stats.basiliskWins, stats.matches);
    printPercent("Simultaneous Basilisk draws", stats.simultaneousBasiliskDraws, stats.matches);
    printPercent("Extraction wins", stats.extractionWins, stats.matches);
    printPercent("Other draws", stats.draws, stats.matches);

    const double avgRounds = stats.matches ? static_cast<double>(stats.totalRounds) / stats.matches : 0.0;
    const double avgCaves = stats.matches ? static_cast<double>(stats.totalCaves) / (stats.matches * 2.0) : 0.0;
    const double avgArrows = stats.matches ? static_cast<double>(stats.totalFinalArrows) / (stats.matches * 2.0) : 0.0;

    std::cout << "\nMATCH LENGTH / EXPLORATION\n";
    std::cout << "Average rounds: " << avgRounds << '\n';
    std::cout << "Median rounds: " << percentile(stats.roundSamples, .50) << '\n';
    std::cout << "P90/P95 rounds: " << percentile(stats.roundSamples, .90) << '/' << percentile(stats.roundSamples, .95) << '\n';
    std::cout << "Minimum/maximum rounds: " << percentile(stats.roundSamples, 0.0) << '/' << percentile(stats.roundSamples, 1.0) << '\n';
    std::cout << "Average caves discovered/hunter: " << avgCaves << '\n';
    std::cout << "Frontier-seeking moves: " << stats.frontierMoves << '\n';

    std::cout << "\nPIT INVESTIGATION TELEMETRY\n";
    std::cout << "Pit warning player-rounds: " << stats.pitWarnings << '\n';
    std::cout << "Investigation actions: " << stats.pitInvestigations << '\n';
    std::cout << "Directional successes: " << stats.pitClueSuccesses << '\n';
    std::cout << "Inconclusive results: " << stats.pitClueInconclusive << '\n';
    std::cout << "Known dangerous-tunnel moves rejected: " << stats.knownPitTunnelAvoidances << '\n';
    std::cout << "Pit deaths: " << stats.pitDeaths << '\n';
    std::cout << "Mutual-Pit draws: " << stats.mutualPitDraws << '\n';

    std::cout << "\nBASILISK TELEMETRY\n";
    std::cout << "True-encounter arrows: " << stats.basiliskEncounters << '\n';
    std::cout << "Evades: " << stats.basiliskEvades << '\n';
    std::cout << "First/second/third encounter kills: " << stats.firstKills << '/' << stats.secondKills << '/' << stats.thirdKills << '\n';
    std::cout << "Matches reaching second/third encounter: " << stats.secondEncounterMatches << '/' << stats.thirdEncounterMatches << '\n';

    std::cout << "\nOBJECTIVE TELEMETRY\n";
    std::cout << "Bodies created/found: " << stats.bodiesCreated << '/' << stats.bodiesFound << '\n';
    std::cout << "Sigils acquired: " << stats.sigilsAcquired << '\n';
    std::cout << "Objective-driven searches: " << stats.objectiveSearches << '\n';
    std::cout << "Extractions activated: " << stats.extractionsActivated << '\n';
    std::cout << "Extraction path moves: " << stats.extractionPathMoves << '\n';
    std::cout << "Escape available / players escaped: " << stats.escapeAvailable << '/' << stats.escaped << '\n';

    std::cout << "\nRESOURCE / BOT DECISION TELEMETRY\n";
    std::cout << "Loose arrows spawned: " << stats.looseArrowSpawns << '\n';
    std::cout << "Arrows found: " << stats.arrowsFound << '\n';
    std::cout << "Arrows fired: " << stats.arrowsFired << '\n';
    std::cout << "Average arrows remaining/hunter: " << avgArrows << '\n';
    std::cout << "Searches: " << stats.searches << '\n';
    std::cout << "Unexplored moves: " << stats.unexploredMoves << '\n';
    std::cout << "Known-route moves: " << stats.knownMoves << '\n';

    return 0;
}
