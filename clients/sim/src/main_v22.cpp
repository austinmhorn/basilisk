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
    int pitWarnings{0};
    int searches{0};
    int retreats{0};
};

struct BotMemory {
    std::unordered_map<CaveId, CaveMemory> caves;
    std::optional<CaveId> lastCave;
    CaveId currentCave{0};
    bool rivalDead{false};
    std::size_t lastDiscoveredCount{0};
    std::uint64_t drought{0};
};

struct MatchTelemetry {
    bool firstDeathSeen{false};
    RoundNumber firstDeathRound{0};
    std::optional<PlayerId> survivorAfterFirstDeath;
    std::unordered_map<PlayerId, std::size_t> cavesAtRivalDeath;
    std::unordered_set<PlayerId> bodyCaveEnteredWithoutSearch;
    bool hadPitDeath{false};
    bool hadNonPitDeath{false};
    std::unordered_set<PlayerId> pitDeadPlayers;
    std::unordered_map<PlayerId, std::uint64_t> arrowsCollected;
    std::unordered_map<PlayerId, std::uint64_t> shotsFired;
};

struct SimulationStats {
    std::uint64_t matches{0}, completed{0}, stalled{0};
    std::uint64_t basiliskWins{0}, simultaneousBasiliskDraws{0}, extractionWins{0}, draws{0};

    std::uint64_t pitDeaths{0}, pitDeathsAfterWarning{0}, pvpDeaths{0}, pvpHits{0};
    std::uint64_t mutualPitDraws{0}, pitPlusOtherDraws{0};
    std::uint64_t matchesWithSingleSurvivorAfterFirstDeath{0};
    std::uint64_t totalRoundsSurvivedAfterRivalDeath{0};
    std::uint64_t totalCavesExploredAfterRivalDeath{0};
    std::uint64_t bodyCaveEnteredWithoutSearch{0};

    std::uint64_t arrowsFired{0}, blindShots{0}, searches{0}, arrowsFound{0}, looseArrowSpawns{0};
    std::uint64_t itemsFound{0}, heals{0};
    std::uint64_t player1ArrowsFound{0}, player2ArrowsFound{0};
    std::uint64_t player1Shots{0}, player2Shots{0};

    std::uint64_t basiliskEncounters{0}, basiliskEvades{0};
    std::uint64_t basiliskFirstEncounterKills{0}, basiliskSecondEncounterKills{0}, basiliskThirdEncounterKills{0};
    std::uint64_t matchesReachedSecondEncounter{0}, matchesReachedThirdEncounter{0};
    std::uint64_t restlessAssignments{0}, lurkerAssignments{0}, skittishAssignments{0}, territorialAssignments{0}, enragedAssignments{0};

    std::uint64_t bodiesCreated{0}, bodiesFound{0}, sigilsAcquired{0}, extractionsActivated{0}, escapeAvailableEvents{0}, playerEscapes{0};
    std::uint64_t objectiveSearches{0}, postDeathFrontierMoves{0}, extractionPathMoves{0};

    std::uint64_t jackalRobberies{0}, jackalScares{0}, jackalKnockouts{0}, jackalStuns{0};
    std::uint64_t pitWarningPlayerRounds{0}, basiliskWarningPlayerRounds{0}, rivalWarningPlayerRounds{0}, jackalWarningPlayerRounds{0};
    std::uint64_t pitWarningsAvoidedWithKnownRoute{0}, pitWarningsWithForcedUnknownRisk{0}, repeatedPitRiskMoves{0};
    std::uint64_t unexploredMoves{0}, knownMoves{0}, frontierSeekingMoves{0};
    std::uint64_t roundsWithoutDiscovery{0}, longestExplorationDrought{0}, maxVisitsSingleCave{0};

    std::uint64_t totalRounds{0}, totalCavesDiscovered{0}, totalFinalArrows{0};
    std::vector<std::uint64_t> roundSamples;
};

bool hasObservation(const PlayerRoundSnapshot& snapshot, ObservationType type) {
    return std::any_of(snapshot.observations.begin(), snapshot.observations.end(),
        [type](const PlayerObservation& o) { return o.type == type; });
}

bool hasBasiliskClue(const PlayerRoundSnapshot& snapshot) {
    return hasObservation(snapshot, ObservationType::BasiliskNearby) ||
           hasObservation(snapshot, ObservationType::BasiliskNearbySubtle) ||
           hasObservation(snapshot, ObservationType::RestlessBasiliskNoise) ||
           hasObservation(snapshot, ObservationType::EnragedLastKnownCave);
}

PlayerAction materialize(PlayerId player, const AvailableAction& available) {
    PlayerAction a;
    a.player = player;
    a.type = available.type;
    a.targetCave = available.targetCave;
    a.targetTunnel = available.targetTunnel;
    a.targetItem = available.targetItem;
    a.contextualAction = available.contextualAction;
    return a;
}

std::vector<const AvailableAction*> actionsOfType(const PlayerRoundSnapshot& s, ActionType type) {
    std::vector<const AvailableAction*> out;
    for (const auto& a : s.availableActions) if (a.type == type) out.push_back(&a);
    return out;
}

const AvailableAction* deterministicPick(const std::vector<const AvailableAction*>& choices, std::uint64_t salt) {
    if (choices.empty()) return nullptr;
    return choices[static_cast<std::size_t>(salt % choices.size())];
}

const AvailableAction* searchAction(const PlayerRoundSnapshot& s) {
    for (const auto& a : s.availableActions) if (a.type == ActionType::Search) return &a;
    return nullptr;
}

const DiscoveredCaveView* caveView(const PlayerRoundSnapshot& s, CaveId cave) {
    const auto it = std::find_if(s.map.caves.begin(), s.map.caves.end(), [cave](const auto& c) { return c.cave == cave; });
    return it == s.map.caves.end() ? nullptr : &*it;
}

bool hasUnknownExit(const PlayerRoundSnapshot& s, CaveId cave) {
    const auto* view = caveView(s, cave);
    return view && std::any_of(view->exits.begin(), view->exits.end(),
        [](const TunnelView& e) { return !e.destination.has_value(); });
}

std::optional<CaveId> nearestFrontierStep(const PlayerRoundSnapshot& s) {
    if (hasUnknownExit(s, s.currentCave)) return s.currentCave;
    std::queue<CaveId> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_set<CaveId> seen;
    q.push(s.currentCave);
    seen.insert(s.currentCave);

    while (!q.empty()) {
        const CaveId cur = q.front(); q.pop();
        const auto* view = caveView(s, cur);
        if (!view) continue;
        for (const auto& exit : view->exits) {
            if (!exit.destination.has_value()) continue;
            const CaveId next = *exit.destination;
            if (!seen.insert(next).second) continue;
            parent[next] = cur;
            if (hasUnknownExit(s, next)) {
                CaveId step = next;
                while (parent.contains(step) && parent.at(step) != s.currentCave) step = parent.at(step);
                return step;
            }
            q.push(next);
        }
    }
    return std::nullopt;
}

std::optional<CaveId> nextStepToTarget(const PlayerRoundSnapshot& s, CaveId target) {
    if (s.currentCave == target) return target;
    std::queue<CaveId> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_set<CaveId> seen;
    q.push(s.currentCave);
    seen.insert(s.currentCave);

    while (!q.empty()) {
        const CaveId cur = q.front(); q.pop();
        const auto* view = caveView(s, cur);
        if (!view) continue;
        for (const auto& exit : view->exits) {
            if (!exit.destination.has_value()) continue;
            const CaveId next = *exit.destination;
            if (!seen.insert(next).second) continue;
            parent[next] = cur;
            if (next == target) {
                CaveId step = next;
                while (parent.contains(step) && parent.at(step) != s.currentCave) step = parent.at(step);
                return step;
            }
            q.push(next);
        }
    }
    return std::nullopt;
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

void updateMemory(const PlayerRoundSnapshot& s, BotMemory& m, SimulationStats& stats) {
    if (m.currentCave != s.currentCave) {
        if (m.currentCave != 0) m.lastCave = m.currentCave;
        m.currentCave = s.currentCave;
    }
    auto& cave = m.caves[s.currentCave];
    ++cave.visits;
    stats.maxVisitsSingleCave = std::max<std::uint64_t>(stats.maxVisitsSingleCave, cave.visits);
    if (hasObservation(s, ObservationType::PitNearby)) ++cave.pitWarnings;
    if (hasObservation(s, ObservationType::RivalDied)) m.rivalDead = true;

    if (s.map.caves.size() > m.lastDiscoveredCount) {
        m.lastDiscoveredCount = s.map.caves.size();
        m.drought = 0;
    } else {
        ++m.drought;
        ++stats.roundsWithoutDiscovery;
        stats.longestExplorationDrought = std::max(stats.longestExplorationDrought, m.drought);
    }
}

std::optional<PlayerAction> chooseBotActionV22(
    const PlayerRoundSnapshot& s, BotMemory& m, MatchSeed matchSeed, SimulationStats& stats) {
    if (!s.alive || s.availableActions.empty()) return std::nullopt;
    updateMemory(s, m, stats);

    const std::uint64_t salt = static_cast<std::uint64_t>(matchSeed) ^
        (static_cast<std::uint64_t>(s.round) * 0x9E3779B97F4A7C15ULL) ^
        (static_cast<std::uint64_t>(s.player) * 0xBF58476D1CE4E5B9ULL);

    for (const auto& a : s.availableActions) {
        if (a.type == ActionType::Contextual && a.contextualAction == ContextualActionType::Escape)
            return materialize(s.player, a);
    }

    if (s.health <= 60) {
        for (const auto& a : s.availableActions)
            if (a.type == ActionType::UseItem && a.targetItem == ItemType::HealingDraught)
                return materialize(s.player, a);
    }

    if (s.hasHunterSigil && s.extractionCave.has_value()) {
        if (const auto step = nextStepToTarget(s, *s.extractionCave); step.has_value() && *step != s.currentCave) {
            if (const auto* move = moveTo(s, *step)) {
                ++stats.extractionPathMoves;
                ++stats.knownMoves;
                return materialize(s.player, *move);
            }
        }
    }

    // After the rival dies, finding the Sigil becomes the primary objective.
    // The bot searches each newly reached cave before pursuing combat clues.
    auto& cave = m.caves[s.currentCave];
    if (m.rivalDead && !s.hasHunterSigil && cave.searches == 0) {
        if (const auto* search = searchAction(s)) {
            ++cave.searches;
            ++stats.objectiveSearches;
            return materialize(s.player, *search);
        }
    }

    // Basilisk combat still matters, but before rival death it has higher priority.
    // After rival death, only the strongest direct clue (Enraged last-known cave)
    // interrupts objective exploration.
    if (hasObservation(s, ObservationType::EnragedLastKnownCave) && s.arrows > 0) {
        for (const auto& o : s.observations) {
            if (o.type == ObservationType::EnragedLastKnownCave && o.cave.has_value()) {
                if (const auto* shot = shootTo(s, *o.cave)) return materialize(s.player, *shot);
            }
        }
    }

    if (!m.rivalDead && (hasBasiliskClue(s) || hasObservation(s, ObservationType::RivalNearby)) && s.arrows > 0) {
        const auto shots = actionsOfType(s, ActionType::Shoot);
        if (const auto* shot = deterministicPick(shots, salt)) return materialize(s.player, *shot);
    }

    const bool pitWarning = hasObservation(s, ObservationType::PitNearby);
    const auto moves = actionsOfType(s, ActionType::Move);
    std::vector<const AvailableAction*> known;
    std::vector<const AvailableAction*> unknown;
    for (const auto* move : moves) {
        if (move->targetCave.has_value()) known.push_back(move);
        else unknown.push_back(move);
    }

    if (pitWarning && !unknown.empty()) {
        // Objective mode is somewhat more willing to accept risk because a corpse/Sigil
        // may be beyond the warned frontier, but it still never knows which tunnel is bad.
        const int warningThreshold = m.rivalDead ? 2 : 3;
        const int retreatThreshold = m.rivalDead ? 1 : 2;
        const std::uint64_t droughtThreshold = m.rivalDead ? 5 : 8;
        const bool repeated = cave.pitWarnings >= warningThreshold ||
                              cave.retreats >= retreatThreshold ||
                              m.drought >= droughtThreshold;

        if (!repeated && !known.empty()) {
            std::vector<const AvailableAction*> preferred;
            for (const auto* move : known)
                if (!m.lastCave.has_value() || move->targetCave != m.lastCave) preferred.push_back(move);
            const auto* choice = deterministicPick(preferred.empty() ? known : preferred, salt >> 3U);
            if (choice) {
                ++cave.retreats;
                ++stats.pitWarningsAvoidedWithKnownRoute;
                ++stats.knownMoves;
                return materialize(s.player, *choice);
            }
        }

        if (!repeated && cave.searches == 0) {
            if (const auto* search = searchAction(s)) {
                ++cave.searches;
                if (m.rivalDead) ++stats.objectiveSearches;
                return materialize(s.player, *search);
            }
        }

        if (const auto* risk = deterministicPick(unknown, salt >> 9U)) {
            ++stats.pitWarningsWithForcedUnknownRisk;
            if (repeated) ++stats.repeatedPitRiskMoves;
            ++stats.unexploredMoves;
            if (m.rivalDead) ++stats.postDeathFrontierMoves;
            return materialize(s.player, *risk);
        }
    }

    const bool shouldSearch = cave.searches == 0 &&
        (m.rivalDead || cave.visits == 1 || (s.arrows <= 1 && s.round % 3 == 0));
    if (shouldSearch) {
        if (const auto* search = searchAction(s)) {
            ++cave.searches;
            if (m.rivalDead) ++stats.objectiveSearches;
            return materialize(s.player, *search);
        }
    }

    if (!unknown.empty()) {
        if (const auto* move = deterministicPick(unknown, salt >> 5U)) {
            ++stats.unexploredMoves;
            if (m.rivalDead) ++stats.postDeathFrontierMoves;
            return materialize(s.player, *move);
        }
    }

    if (const auto frontierStep = nearestFrontierStep(s); frontierStep.has_value() && *frontierStep != s.currentCave) {
        if (const auto* move = moveTo(s, *frontierStep)) {
            ++stats.frontierSeekingMoves;
            if (m.rivalDead) ++stats.postDeathFrontierMoves;
            ++stats.knownMoves;
            return materialize(s.player, *move);
        }
    }

    // If objective mode exhausts all known frontiers, normal Basilisk hunting resumes.
    if (m.rivalDead && hasBasiliskClue(s) && s.arrows > 0) {
        const auto shots = actionsOfType(s, ActionType::Shoot);
        if (const auto* shot = deterministicPick(shots, salt >> 7U)) return materialize(s.player, *shot);
    }

    if (!known.empty()) {
        std::vector<const AvailableAction*> preferred;
        for (const auto* move : known)
            if (!m.lastCave.has_value() || move->targetCave != m.lastCave) preferred.push_back(move);
        if (const auto* move = deterministicPick(preferred.empty() ? known : preferred, salt >> 11U)) {
            ++stats.knownMoves;
            return materialize(s.player, *move);
        }
    }

    if (const auto* search = searchAction(s)) return materialize(s.player, *search);
    return materialize(s.player, s.availableActions.front());
}

void snapshotTelemetry(const PlayerRoundSnapshot& s, SimulationStats& stats,
                       std::unordered_set<PlayerId>& pitWarned) {
    if (hasObservation(s, ObservationType::PitNearby)) {
        ++stats.pitWarningPlayerRounds;
        pitWarned.insert(s.player);
    }
    if (hasBasiliskClue(s)) ++stats.basiliskWarningPlayerRounds;
    if (hasObservation(s, ObservationType::RivalNearby)) ++stats.rivalWarningPlayerRounds;
    if (hasObservation(s, ObservationType::JackalNearby)) ++stats.jackalWarningPlayerRounds;
}

bool playerSearchedThisRound(const std::vector<PlayerAction>& selected, PlayerId player) {
    return std::any_of(selected.begin(), selected.end(), [player](const PlayerAction& action) {
        return action.player == player && action.type == ActionType::Search;
    });
}

void inspectObjectiveMisses(const MatchState& state,
                            const std::vector<PlayerAction>& selected,
                            MatchTelemetry& matchTelemetry) {
    for (const auto& player : state.players) {
        if (!player.alive || playerSearchedThisRound(selected, player)) continue;
        for (const auto& body : state.bodies) {
            if (!body.sigilAvailable || body.owner == player.id) continue;
            if (body.cave == player.cave || body.sigilCave == player.cave) {
                matchTelemetry.bodyCaveEnteredWithoutSearch.insert(player.id);
            }
        }
    }
}

void eventTelemetry(const std::vector<GameEvent>& events,
                    const std::unordered_set<PlayerId>& pitWarned,
                    SimulationStats& stats,
                    MatchTelemetry& matchTelemetry,
                    const MatchState& state) {
    for (const auto& e : events) {
        switch (e.type) {
            case GameEventType::ArrowFired:
                ++stats.arrowsFired;
                if (e.actor.has_value()) ++matchTelemetry.shotsFired[*e.actor];
                break;
            case GameEventType::ArrowHitPlayer: ++stats.pvpHits; break;
            case GameEventType::ArrowReachedBasilisk: ++stats.basiliskEncounters; break;
            case GameEventType::SearchCompleted: ++stats.searches; break;
            case GameEventType::LooseArrowSpawned: ++stats.looseArrowSpawns; break;
            case GameEventType::ArrowFound:
                stats.arrowsFound += static_cast<std::uint64_t>(std::max(0, e.amount));
                if (e.actor.has_value()) matchTelemetry.arrowsCollected[*e.actor] += static_cast<std::uint64_t>(std::max(0, e.amount));
                break;
            case GameEventType::ItemFound: ++stats.itemsFound; break;
            case GameEventType::PlayerHealed: ++stats.heals; break;
            case GameEventType::PitTriggered:
                ++stats.pitDeaths;
                matchTelemetry.hadPitDeath = true;
                if (e.targetPlayer.has_value()) {
                    matchTelemetry.pitDeadPlayers.insert(*e.targetPlayer);
                    if (pitWarned.contains(*e.targetPlayer)) ++stats.pitDeathsAfterWarning;
                }
                break;
            case GameEventType::BodyCreated: ++stats.bodiesCreated; break;
            case GameEventType::BodyFound: ++stats.bodiesFound; break;
            case GameEventType::SigilAcquired: ++stats.sigilsAcquired; break;
            case GameEventType::ExtractionActivated: ++stats.extractionsActivated; break;
            case GameEventType::EscapeAvailable: ++stats.escapeAvailableEvents; break;
            case GameEventType::PlayerEscaped: ++stats.playerEscapes; break;
            case GameEventType::JackalRobbedArrow: ++stats.jackalRobberies; break;
            case GameEventType::JackalScaredPlayer: ++stats.jackalScares; break;
            case GameEventType::JackalKnockedOutPlayer: ++stats.jackalKnockouts; break;
            case GameEventType::JackalStunned: ++stats.jackalStuns; break;
            case GameEventType::BasiliskEvaded: ++stats.basiliskEvades; break;
            case GameEventType::BasiliskBehaviorChanged:
                if (e.basiliskBehavior.has_value()) switch (*e.basiliskBehavior) {
                    case BasiliskBehavior::Restless: ++stats.restlessAssignments; break;
                    case BasiliskBehavior::Lurker: ++stats.lurkerAssignments; break;
                    case BasiliskBehavior::Skittish: ++stats.skittishAssignments; break;
                    case BasiliskBehavior::Territorial: ++stats.territorialAssignments; break;
                    case BasiliskBehavior::Enraged: ++stats.enragedAssignments; break;
                    case BasiliskBehavior::Normal: break;
                }
                break;
            default: break;
        }
    }

    for (const auto& killed : events) {
        if (killed.type != GameEventType::PlayerKilled || !killed.targetPlayer.has_value()) continue;
        const PlayerId target = *killed.targetPlayer;
        const bool pit = matchTelemetry.pitDeadPlayers.contains(target);
        const bool timeout = std::any_of(events.begin(), events.end(), [&](const auto& e) {
            return (e.type == GameEventType::PlayerReserveExpired || e.type == GameEventType::PlayerDisconnectTimedOut) &&
                   e.targetPlayer == target;
        });
        if (!pit && !timeout) {
            ++stats.pvpDeaths;
            matchTelemetry.hadNonPitDeath = true;
        }
    }

    const bool killedBasilisk = std::any_of(events.begin(), events.end(),
        [](const auto& e) { return e.type == GameEventType::BasiliskKilled; });
    if (killedBasilisk) {
        if (state.basilisk.trueEncounters <= 1) ++stats.basiliskFirstEncounterKills;
        else if (state.basilisk.trueEncounters == 2) ++stats.basiliskSecondEncounterKills;
        else ++stats.basiliskThirdEncounterKills;
    }
}

void noteFirstDeath(const MatchState& state, MatchTelemetry& mt) {
    if (mt.firstDeathSeen) return;
    std::vector<PlayerId> living;
    for (const auto& player : state.players) if (player.alive) living.push_back(player.id);
    if (living.size() != 1) return;

    mt.firstDeathSeen = true;
    mt.firstDeathRound = state.round;
    mt.survivorAfterFirstDeath = living.front();
    ++mt.cavesAtRivalDeath[living.front()]; // marker; overwritten by caller with actual count
}

void resultTelemetry(const MatchState& state, SimulationStats& stats, const MatchTelemetry& mt) {
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
            if (mt.pitDeadPlayers.size() >= 2) ++stats.mutualPitDraws;
            else if (mt.hadPitDeath && mt.hadNonPitDeath) ++stats.pitPlusOtherDraws;
            break;
        case MatchOutcome::None: break;
    }
}

void runOne(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds, SimulationStats& stats) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::vector<GameEvent> previousEvents;
    std::unordered_map<PlayerId, BotMemory> memories;
    MatchTelemetry mt;
    bool countedSecondEncounter = false;
    bool countedThirdEncounter = false;

    while (state.result.status == MatchStatus::Active && state.round <= maxRounds) {
        std::vector<PlayerId> living;
        for (const auto& p : state.players) if (p.alive) living.push_back(p.id);
        if (living.empty()) break;

        std::vector<PlayerAction> selected;
        std::unordered_set<PlayerId> pitWarned;
        std::unordered_map<PlayerId, std::size_t> discoveredBefore;

        for (const PlayerId player : living) {
            const auto snapshot = SnapshotSystem::buildForPlayer(state, player, previousEvents);
            discoveredBefore[player] = snapshot.map.caves.size();
            snapshotTelemetry(snapshot, stats, pitWarned);
            if (const auto action = chooseBotActionV22(snapshot, memories[player], matchSeed, stats)) {
                if (action->type == ActionType::Shoot && !hasBasiliskClue(snapshot) &&
                    !hasObservation(snapshot, ObservationType::RivalNearby) &&
                    !hasObservation(snapshot, ObservationType::JackalNearby)) ++stats.blindShots;
                selected.push_back(*action);
            }
        }
        if (selected.empty()) break;

        inspectObjectiveMisses(state, selected, mt);

        for (const auto& a : selected) if (!coordinator.submitAction(a)) break;
        for (const auto& a : selected) if (!coordinator.lockAction(a.player)) break;
        previousEvents = coordinator.lastEvents();
        eventTelemetry(previousEvents, pitWarned, stats, mt, state);

        if (!countedSecondEncounter && state.basilisk.trueEncounters >= 2) {
            ++stats.matchesReachedSecondEncounter;
            countedSecondEncounter = true;
        }
        if (!countedThirdEncounter && state.basilisk.trueEncounters >= 3) {
            ++stats.matchesReachedThirdEncounter;
            countedThirdEncounter = true;
        }

        if (!mt.firstDeathSeen) {
            std::vector<PlayerId> survivors;
            for (const auto& p : state.players) if (p.alive) survivors.push_back(p.id);
            if (survivors.size() == 1) {
                mt.firstDeathSeen = true;
                mt.firstDeathRound = state.round;
                mt.survivorAfterFirstDeath = survivors.front();
                const auto survivorSnapshot = SnapshotSystem::buildForPlayer(state, survivors.front(), previousEvents);
                mt.cavesAtRivalDeath[survivors.front()] = survivorSnapshot.map.caves.size();
                ++stats.matchesWithSingleSurvivorAfterFirstDeath;
            }
        }
    }

    ++stats.matches;
    const auto recordedRounds = std::min<std::uint64_t>(state.round, maxRounds);
    stats.totalRounds += recordedRounds;
    stats.roundSamples.push_back(recordedRounds);

    for (const auto& p : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, p.id, previousEvents);
        stats.totalCavesDiscovered += snapshot.map.caves.size();
        stats.totalFinalArrows += static_cast<std::uint64_t>(std::max(0, p.arrows));
    }

    if (mt.survivorAfterFirstDeath.has_value()) {
        const PlayerId survivor = *mt.survivorAfterFirstDeath;
        stats.totalRoundsSurvivedAfterRivalDeath += recordedRounds > mt.firstDeathRound
            ? recordedRounds - mt.firstDeathRound : 0;
        const auto finalSnapshot = SnapshotSystem::buildForPlayer(state, survivor, previousEvents);
        const auto initial = mt.cavesAtRivalDeath.contains(survivor) ? mt.cavesAtRivalDeath.at(survivor) : 0;
        if (finalSnapshot.map.caves.size() > initial)
            stats.totalCavesExploredAfterRivalDeath += finalSnapshot.map.caves.size() - initial;
    }

    stats.bodyCaveEnteredWithoutSearch += mt.bodyCaveEnteredWithoutSearch.size();
    stats.player1ArrowsFound += mt.arrowsCollected[1];
    stats.player2ArrowsFound += mt.arrowsCollected[2];
    stats.player1Shots += mt.shotsFired[1];
    stats.player2Shots += mt.shotsFired[2];
    resultTelemetry(state, stats, mt);
}

void printPercent(const char* label, std::uint64_t value, std::uint64_t total) {
    const double pct = total == 0 ? 0.0 : 100.0 * static_cast<double>(value) / static_cast<double>(total);
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

    SimulationStats stats;
    for (std::uint64_t i = 0; i < matches; ++i)
        runOne(firstMapSeed + static_cast<MapSeed>(i), firstMatchSeed + static_cast<MatchSeed>(i), maxRounds, stats);

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V2.2)\n";
    std::cout << "Matches: " << stats.matches << " | max rounds/match: " << maxRounds << "\n\n";

    std::cout << "OUTCOMES\n";
    printPercent("Completed", stats.completed, stats.matches);
    printPercent("Stalled at round cap", stats.stalled, stats.matches);
    printPercent("Basilisk kills", stats.basiliskWins, stats.matches);
    printPercent("Simultaneous Basilisk draws", stats.simultaneousBasiliskDraws, stats.matches);
    printPercent("Extraction wins", stats.extractionWins, stats.matches);
    printPercent("Other draws", stats.draws, stats.matches);

    const double avgRounds = stats.matches ? static_cast<double>(stats.totalRounds) / stats.matches : 0.0;
    const double avgCaves = stats.matches ? static_cast<double>(stats.totalCavesDiscovered) / (stats.matches * 2.0) : 0.0;
    const double avgArrows = stats.matches ? static_cast<double>(stats.totalFinalArrows) / (stats.matches * 2.0) : 0.0;
    const double avgPostDeathRounds = stats.matchesWithSingleSurvivorAfterFirstDeath
        ? static_cast<double>(stats.totalRoundsSurvivedAfterRivalDeath) / stats.matchesWithSingleSurvivorAfterFirstDeath : 0.0;
    const double avgPostDeathCaves = stats.matchesWithSingleSurvivorAfterFirstDeath
        ? static_cast<double>(stats.totalCavesExploredAfterRivalDeath) / stats.matchesWithSingleSurvivorAfterFirstDeath : 0.0;

    std::cout << "\nMATCH LENGTH / EXPLORATION\n";
    std::cout << "Average rounds: " << avgRounds << '\n';
    std::cout << "Median rounds: " << percentile(stats.roundSamples, .50) << '\n';
    std::cout << "P90 rounds: " << percentile(stats.roundSamples, .90) << '\n';
    std::cout << "P95 rounds: " << percentile(stats.roundSamples, .95) << '\n';
    std::cout << "Minimum rounds: " << percentile(stats.roundSamples, 0.0) << '\n';
    std::cout << "Maximum rounds: " << percentile(stats.roundSamples, 1.0) << '\n';
    std::cout << "Average caves discovered/hunter: " << avgCaves << '\n';
    std::cout << "Frontier-seeking moves: " << stats.frontierSeekingMoves << '\n';
    std::cout << "Rounds without new discovery: " << stats.roundsWithoutDiscovery << '\n';
    std::cout << "Longest exploration drought: " << stats.longestExplorationDrought << '\n';
    std::cout << "Maximum visits to one cave: " << stats.maxVisitsSingleCave << '\n';

    std::cout << "\nDEATH / DRAW TELEMETRY\n";
    std::cout << "Pit deaths: " << stats.pitDeaths << '\n';
    std::cout << "Pit deaths after warning that round: " << stats.pitDeathsAfterWarning << '\n';
    std::cout << "Mutual-Pit draws: " << stats.mutualPitDraws << '\n';
    std::cout << "Pit + other-death draws: " << stats.pitPlusOtherDraws << '\n';
    std::cout << "Matches with one survivor after first death: " << stats.matchesWithSingleSurvivorAfterFirstDeath << '\n';
    std::cout << "Average rounds survived after rival death: " << avgPostDeathRounds << '\n';
    std::cout << "Average new caves explored after rival death: " << avgPostDeathCaves << '\n';
    std::cout << "Corpse/Sigil caves entered without searching: " << stats.bodyCaveEnteredWithoutSearch << '\n';
    std::cout << "PvP hits: " << stats.pvpHits << '\n';
    std::cout << "PvP deaths: " << stats.pvpDeaths << '\n';

    std::cout << "\nPIT DECISION TELEMETRY\n";
    std::cout << "Pit warning player-rounds: " << stats.pitWarningPlayerRounds << '\n';
    std::cout << "Pit warnings avoided via known route: " << stats.pitWarningsAvoidedWithKnownRoute << '\n';
    std::cout << "Pit warnings with unknown risk: " << stats.pitWarningsWithForcedUnknownRisk << '\n';
    std::cout << "Repeated-warning risk moves: " << stats.repeatedPitRiskMoves << '\n';

    std::cout << "\nBASILISK TELEMETRY\n";
    std::cout << "True-encounter arrows: " << stats.basiliskEncounters << '\n';
    std::cout << "Evades: " << stats.basiliskEvades << '\n';
    std::cout << "First/second/third encounter kills: "
              << stats.basiliskFirstEncounterKills << '/' << stats.basiliskSecondEncounterKills << '/'
              << stats.basiliskThirdEncounterKills << '\n';
    std::cout << "Matches reaching second encounter: " << stats.matchesReachedSecondEncounter << '\n';
    std::cout << "Matches reaching third encounter: " << stats.matchesReachedThirdEncounter << '\n';
    std::cout << "Restless/Lurker/Skittish/Territorial/Enraged assignments: "
              << stats.restlessAssignments << '/' << stats.lurkerAssignments << '/' << stats.skittishAssignments << '/'
              << stats.territorialAssignments << '/' << stats.enragedAssignments << '\n';
    std::cout << "Basilisk warning player-rounds: " << stats.basiliskWarningPlayerRounds << '\n';

    std::cout << "\nOBJECTIVE TELEMETRY\n";
    std::cout << "Bodies created: " << stats.bodiesCreated << '\n';
    std::cout << "Bodies found: " << stats.bodiesFound << '\n';
    std::cout << "Sigils acquired: " << stats.sigilsAcquired << '\n';
    std::cout << "Objective-driven searches: " << stats.objectiveSearches << '\n';
    std::cout << "Post-death frontier moves: " << stats.postDeathFrontierMoves << '\n';
    std::cout << "Extractions activated: " << stats.extractionsActivated << '\n';
    std::cout << "Extraction path moves: " << stats.extractionPathMoves << '\n';
    std::cout << "Escape-available events: " << stats.escapeAvailableEvents << '\n';
    std::cout << "Players escaped: " << stats.playerEscapes << '\n';

    std::cout << "\nJACKAL TELEMETRY\n";
    std::cout << "Robberies/scares/knockouts/stuns: " << stats.jackalRobberies << '/' << stats.jackalScares << '/'
              << stats.jackalKnockouts << '/' << stats.jackalStuns << '\n';
    std::cout << "Jackal warning player-rounds: " << stats.jackalWarningPlayerRounds << '\n';

    std::cout << "\nRESOURCE / BOT DECISION TELEMETRY\n";
    std::cout << "Loose arrows spawned: " << stats.looseArrowSpawns << '\n';
    std::cout << "Arrows found: " << stats.arrowsFound << " (P1 " << stats.player1ArrowsFound
              << " / P2 " << stats.player2ArrowsFound << ")\n";
    std::cout << "Arrows fired: " << stats.arrowsFired << " (P1 " << stats.player1Shots
              << " / P2 " << stats.player2Shots << ")\n";
    std::cout << "Blind shots: " << stats.blindShots << '\n';
    std::cout << "Average arrows remaining/hunter: " << avgArrows << '\n';
    std::cout << "Searches: " << stats.searches << '\n';
    std::cout << "Items found: " << stats.itemsFound << '\n';
    std::cout << "Heals used: " << stats.heals << '\n';
    std::cout << "Unexplored moves: " << stats.unexploredMoves << '\n';
    std::cout << "Known-route moves: " << stats.knownMoves << '\n';

    return 0;
}
