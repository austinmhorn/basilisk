#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
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

struct SimulationStats {
    std::uint64_t matches{0};
    std::uint64_t completed{0};
    std::uint64_t stalled{0};
    std::uint64_t basiliskWins{0};
    std::uint64_t simultaneousBasiliskDraws{0};
    std::uint64_t extractionWins{0};
    std::uint64_t draws{0};

    std::uint64_t pitDeaths{0};
    std::uint64_t pitDeathsAfterWarning{0};
    std::uint64_t pvpDeaths{0};
    std::uint64_t pvpHits{0};

    std::uint64_t arrowsFired{0};
    std::uint64_t blindShots{0};
    std::uint64_t searches{0};
    std::uint64_t arrowsFound{0};
    std::uint64_t itemsFound{0};
    std::uint64_t heals{0};

    std::uint64_t basiliskEncounters{0};
    std::uint64_t basiliskEvades{0};
    std::uint64_t basiliskFirstEncounterKills{0};
    std::uint64_t basiliskSecondEncounterKills{0};
    std::uint64_t basiliskThirdEncounterKills{0};
    std::uint64_t restlessAssignments{0};
    std::uint64_t lurkerAssignments{0};
    std::uint64_t skittishAssignments{0};
    std::uint64_t territorialAssignments{0};
    std::uint64_t enragedAssignments{0};

    std::uint64_t bodiesCreated{0};
    std::uint64_t bodiesFound{0};
    std::uint64_t sigilsAcquired{0};
    std::uint64_t extractionsActivated{0};
    std::uint64_t escapeAvailableEvents{0};
    std::uint64_t playerEscapes{0};

    std::uint64_t jackalRobberies{0};
    std::uint64_t jackalScares{0};
    std::uint64_t jackalKnockouts{0};
    std::uint64_t jackalStuns{0};

    std::uint64_t pitWarningPlayerRounds{0};
    std::uint64_t basiliskWarningPlayerRounds{0};
    std::uint64_t rivalWarningPlayerRounds{0};
    std::uint64_t jackalWarningPlayerRounds{0};

    std::uint64_t totalRounds{0};
    std::uint64_t totalCavesDiscovered{0};
    std::uint64_t totalFinalArrows{0};
    std::vector<std::uint64_t> roundSamples;
};

bool hasObservation(const PlayerRoundSnapshot& snapshot, ObservationType type) {
    return std::any_of(snapshot.observations.begin(), snapshot.observations.end(),
        [type](const PlayerObservation& observation) { return observation.type == type; });
}

bool hasBasiliskClue(const PlayerRoundSnapshot& snapshot) {
    return hasObservation(snapshot, ObservationType::BasiliskNearby) ||
           hasObservation(snapshot, ObservationType::BasiliskNearbySubtle) ||
           hasObservation(snapshot, ObservationType::RestlessBasiliskNoise) ||
           hasObservation(snapshot, ObservationType::EnragedLastKnownCave);
}

PlayerAction materialize(PlayerId player, const AvailableAction& available) {
    PlayerAction action;
    action.player = player;
    action.type = available.type;
    action.targetCave = available.targetCave;
    action.targetTunnel = available.targetTunnel;
    action.targetItem = available.targetItem;
    action.contextualAction = available.contextualAction;
    return action;
}

std::vector<const AvailableAction*> actionsOfType(
    const PlayerRoundSnapshot& snapshot,
    ActionType type) {

    std::vector<const AvailableAction*> out;
    for (const auto& action : snapshot.availableActions) {
        if (action.type == type) out.push_back(&action);
    }
    return out;
}

const AvailableAction* deterministicPick(
    const std::vector<const AvailableAction*>& choices,
    std::uint64_t salt) {

    if (choices.empty()) return nullptr;
    return choices[static_cast<std::size_t>(salt % choices.size())];
}

std::optional<PlayerAction> chooseBotAction(
    const PlayerRoundSnapshot& snapshot,
    MatchSeed matchSeed) {

    if (!snapshot.alive || snapshot.availableActions.empty()) return std::nullopt;

    const std::uint64_t salt = static_cast<std::uint64_t>(matchSeed) ^
        (static_cast<std::uint64_t>(snapshot.round) * 0x9E3779B97F4A7C15ULL) ^
        (static_cast<std::uint64_t>(snapshot.player) * 0xBF58476D1CE4E5B9ULL);

    for (const auto& action : snapshot.availableActions) {
        if (action.type == ActionType::Contextual &&
            action.contextualAction == ContextualActionType::Escape) {
            return materialize(snapshot.player, action);
        }
    }

    if (snapshot.health <= 60) {
        for (const auto& action : snapshot.availableActions) {
            if (action.type == ActionType::UseItem &&
                action.targetItem == ItemType::HealingDraught) {
                return materialize(snapshot.player, action);
            }
        }
    }

    const bool threatNearby = hasBasiliskClue(snapshot) ||
        hasObservation(snapshot, ObservationType::RivalNearby);

    if (threatNearby && snapshot.arrows > 0) {
        const auto shoots = actionsOfType(snapshot, ActionType::Shoot);
        if (const auto* choice = deterministicPick(shoots, salt); choice != nullptr) {
            return materialize(snapshot.player, *choice);
        }
    }

    if ((snapshot.round + snapshot.player) % 4 == 0) {
        for (const auto& action : snapshot.availableActions) {
            if (action.type == ActionType::Search) {
                return materialize(snapshot.player, action);
            }
        }
    }

    const auto moves = actionsOfType(snapshot, ActionType::Move);
    std::vector<const AvailableAction*> unexplored;
    for (const auto* move : moves) {
        if (move->targetTunnel.has_value() && !move->targetCave.has_value()) {
            unexplored.push_back(move);
        }
    }

    if (const auto* choice = deterministicPick(
            unexplored.empty() ? moves : unexplored,
            salt >> 7U); choice != nullptr) {
        return materialize(snapshot.player, *choice);
    }

    for (const auto& action : snapshot.availableActions) {
        if (action.type == ActionType::Search) {
            return materialize(snapshot.player, action);
        }
    }

    return materialize(snapshot.player, snapshot.availableActions.front());
}

void accumulateSnapshotTelemetry(
    const PlayerRoundSnapshot& snapshot,
    SimulationStats& stats,
    std::unordered_set<PlayerId>& pitWarnedThisRound) {

    if (hasObservation(snapshot, ObservationType::PitNearby)) {
        ++stats.pitWarningPlayerRounds;
        pitWarnedThisRound.insert(snapshot.player);
    }
    if (hasBasiliskClue(snapshot)) ++stats.basiliskWarningPlayerRounds;
    if (hasObservation(snapshot, ObservationType::RivalNearby)) ++stats.rivalWarningPlayerRounds;
    if (hasObservation(snapshot, ObservationType::JackalNearby)) ++stats.jackalWarningPlayerRounds;
}

void accumulateSelectedAction(
    const PlayerRoundSnapshot& snapshot,
    const PlayerAction& action,
    SimulationStats& stats) {

    if (action.type != ActionType::Shoot) return;
    const bool hadTargetClue = hasBasiliskClue(snapshot) ||
        hasObservation(snapshot, ObservationType::RivalNearby) ||
        hasObservation(snapshot, ObservationType::JackalNearby);
    if (!hadTargetClue) ++stats.blindShots;
}

void accumulateEvents(
    const std::vector<GameEvent>& events,
    const std::unordered_set<PlayerId>& pitWarnedThisRound,
    SimulationStats& stats) {

    for (const auto& event : events) {
        switch (event.type) {
            case GameEventType::ArrowFired: ++stats.arrowsFired; break;
            case GameEventType::ArrowHitPlayer: ++stats.pvpHits; break;
            case GameEventType::ArrowReachedBasilisk: ++stats.basiliskEncounters; break;
            case GameEventType::SearchCompleted: ++stats.searches; break;
            case GameEventType::ArrowFound: stats.arrowsFound += static_cast<std::uint64_t>(std::max(0, event.amount)); break;
            case GameEventType::ItemFound: ++stats.itemsFound; break;
            case GameEventType::PlayerHealed: ++stats.heals; break;
            case GameEventType::PitTriggered:
                ++stats.pitDeaths;
                if (event.targetPlayer.has_value() && pitWarnedThisRound.contains(*event.targetPlayer)) {
                    ++stats.pitDeathsAfterWarning;
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
                if (event.basiliskBehavior.has_value()) {
                    switch (*event.basiliskBehavior) {
                        case BasiliskBehavior::Restless: ++stats.restlessAssignments; break;
                        case BasiliskBehavior::Lurker: ++stats.lurkerAssignments; break;
                        case BasiliskBehavior::Skittish: ++stats.skittishAssignments; break;
                        case BasiliskBehavior::Territorial: ++stats.territorialAssignments; break;
                        case BasiliskBehavior::Enraged: ++stats.enragedAssignments; break;
                        case BasiliskBehavior::Normal: break;
                    }
                }
                break;
            default: break;
        }
    }

    for (const auto& killed : events) {
        if (killed.type != GameEventType::PlayerKilled || !killed.targetPlayer.has_value()) continue;
        const PlayerId target = *killed.targetPlayer;
        const bool pitCause = std::any_of(events.begin(), events.end(), [&](const GameEvent& event) {
            return event.type == GameEventType::PitTriggered && event.targetPlayer == target;
        });
        const bool timeoutCause = std::any_of(events.begin(), events.end(), [&](const GameEvent& event) {
            return (event.type == GameEventType::PlayerReserveExpired ||
                    event.type == GameEventType::PlayerDisconnectTimedOut) &&
                   event.targetPlayer == target;
        });
        if (!pitCause && !timeoutCause) ++stats.pvpDeaths;
    }
}

void accumulateBasiliskKillStage(
    const MatchState& state,
    const std::vector<GameEvent>& events,
    SimulationStats& stats) {

    const bool killedThisRound = std::any_of(events.begin(), events.end(), [](const GameEvent& event) {
        return event.type == GameEventType::BasiliskKilled;
    });
    if (!killedThisRound) return;

    if (state.basilisk.trueEncounters <= 1) ++stats.basiliskFirstEncounterKills;
    else if (state.basilisk.trueEncounters == 2) ++stats.basiliskSecondEncounterKills;
    else ++stats.basiliskThirdEncounterKills;
}

void accumulateResult(const MatchState& state, SimulationStats& stats) {
    if (state.result.status != MatchStatus::Completed) {
        ++stats.stalled;
        return;
    }

    ++stats.completed;
    switch (state.result.outcome) {
        case MatchOutcome::BasiliskKilled: ++stats.basiliskWins; break;
        case MatchOutcome::SimultaneousBasiliskKill: ++stats.simultaneousBasiliskDraws; break;
        case MatchOutcome::EscapedWithSigil: ++stats.extractionWins; break;
        case MatchOutcome::Draw: ++stats.draws; break;
        case MatchOutcome::None: break;
    }
}

void runOne(
    MapSeed mapSeed,
    MatchSeed matchSeed,
    std::uint64_t maxRounds,
    SimulationStats& stats) {

    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::vector<GameEvent> previousEvents;

    while (state.result.status == MatchStatus::Active && state.round <= maxRounds) {
        std::vector<PlayerId> living;
        for (const auto& player : state.players) {
            if (player.alive) living.push_back(player.id);
        }
        if (living.empty()) break;

        std::vector<PlayerAction> selected;
        std::unordered_set<PlayerId> pitWarnedThisRound;

        for (const PlayerId player : living) {
            const auto snapshot = SnapshotSystem::buildForPlayer(state, player, previousEvents);
            accumulateSnapshotTelemetry(snapshot, stats, pitWarnedThisRound);
            if (const auto action = chooseBotAction(snapshot, matchSeed); action.has_value()) {
                accumulateSelectedAction(snapshot, *action, stats);
                selected.push_back(*action);
            }
        }
        if (selected.empty()) break;

        for (const auto& action : selected) {
            if (!coordinator.submitAction(action)) break;
        }
        for (const auto& action : selected) {
            if (!coordinator.lockAction(action.player)) break;
        }

        previousEvents = coordinator.lastEvents();
        accumulateEvents(previousEvents, pitWarnedThisRound, stats);
        accumulateBasiliskKillStage(state, previousEvents, stats);
    }

    ++stats.matches;
    const std::uint64_t recordedRounds = std::min<std::uint64_t>(state.round, maxRounds);
    stats.totalRounds += recordedRounds;
    stats.roundSamples.push_back(recordedRounds);

    for (const auto& player : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
        stats.totalCavesDiscovered += snapshot.map.caves.size();
        stats.totalFinalArrows += static_cast<std::uint64_t>(std::max(0, player.arrows));
    }
    accumulateResult(state, stats);
}

void printPercent(const char* label, std::uint64_t value, std::uint64_t total) {
    const double pct = total == 0 ? 0.0 : (100.0 * static_cast<double>(value) / static_cast<double>(total));
    std::cout << label << ": " << value << " (" << pct << "%)\n";
}

std::uint64_t percentile(std::vector<std::uint64_t> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        fraction * static_cast<double>(values.size() - 1));
    return values[index];
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t matches = 1000;
    std::uint64_t maxRounds = 250;
    MapSeed firstMapSeed = 100000;
    MatchSeed firstMatchSeed = 500000;

    if (argc > 1) matches = std::stoull(argv[1]);
    if (argc > 2) maxRounds = std::stoull(argv[2]);
    if (argc > 3) firstMapSeed = static_cast<MapSeed>(std::stoull(argv[3]));
    if (argc > 4) firstMatchSeed = static_cast<MatchSeed>(std::stoull(argv[4]));

    SimulationStats stats;
    for (std::uint64_t i = 0; i < matches; ++i) {
        runOne(
            firstMapSeed + static_cast<MapSeed>(i),
            firstMatchSeed + static_cast<MatchSeed>(i),
            maxRounds,
            stats);
    }

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT\n";
    std::cout << "Matches: " << stats.matches << " | max rounds/match: " << maxRounds << "\n\n";

    std::cout << "OUTCOMES\n";
    printPercent("Completed", stats.completed, stats.matches);
    printPercent("Stalled at round cap", stats.stalled, stats.matches);
    printPercent("Basilisk kills", stats.basiliskWins, stats.matches);
    printPercent("Simultaneous Basilisk draws", stats.simultaneousBasiliskDraws, stats.matches);
    printPercent("Extraction wins", stats.extractionWins, stats.matches);
    printPercent("Other draws", stats.draws, stats.matches);

    const double avgRounds = stats.matches == 0 ? 0.0 :
        static_cast<double>(stats.totalRounds) / static_cast<double>(stats.matches);
    const double avgCavesPerHunter = stats.matches == 0 ? 0.0 :
        static_cast<double>(stats.totalCavesDiscovered) / static_cast<double>(stats.matches * 2);
    const double avgFinalArrows = stats.matches == 0 ? 0.0 :
        static_cast<double>(stats.totalFinalArrows) / static_cast<double>(stats.matches * 2);

    std::cout << "\nMATCH LENGTH / EXPLORATION\n";
    std::cout << "Average rounds: " << avgRounds << '\n';
    std::cout << "Median rounds: " << percentile(stats.roundSamples, 0.50) << '\n';
    std::cout << "P90 rounds: " << percentile(stats.roundSamples, 0.90) << '\n';
    std::cout << "P95 rounds: " << percentile(stats.roundSamples, 0.95) << '\n';
    std::cout << "Minimum rounds: " << percentile(stats.roundSamples, 0.0) << '\n';
    std::cout << "Maximum rounds: " << percentile(stats.roundSamples, 1.0) << '\n';
    std::cout << "Average caves discovered/hunter: " << avgCavesPerHunter << '\n';

    std::cout << "\nDEATH / HAZARD TELEMETRY\n";
    std::cout << "Pit deaths: " << stats.pitDeaths << '\n';
    std::cout << "Pit warning player-rounds: " << stats.pitWarningPlayerRounds << '\n';
    std::cout << "Pit deaths after warning that round: " << stats.pitDeathsAfterWarning << '\n';
    std::cout << "PvP hits: " << stats.pvpHits << '\n';
    std::cout << "PvP deaths: " << stats.pvpDeaths << '\n';
    std::cout << "Rival warning player-rounds: " << stats.rivalWarningPlayerRounds << '\n';

    std::cout << "\nBASILISK TELEMETRY\n";
    std::cout << "True-encounter arrows: " << stats.basiliskEncounters << '\n';
    std::cout << "Evades: " << stats.basiliskEvades << '\n';
    std::cout << "First-encounter kills: " << stats.basiliskFirstEncounterKills << '\n';
    std::cout << "Second-encounter kills: " << stats.basiliskSecondEncounterKills << '\n';
    std::cout << "Third-encounter kills: " << stats.basiliskThirdEncounterKills << '\n';
    std::cout << "Restless assignments: " << stats.restlessAssignments << '\n';
    std::cout << "Lurker assignments: " << stats.lurkerAssignments << '\n';
    std::cout << "Skittish assignments: " << stats.skittishAssignments << '\n';
    std::cout << "Territorial assignments: " << stats.territorialAssignments << '\n';
    std::cout << "Enraged assignments: " << stats.enragedAssignments << '\n';
    std::cout << "Basilisk warning player-rounds: " << stats.basiliskWarningPlayerRounds << '\n';

    std::cout << "\nOBJECTIVE TELEMETRY\n";
    std::cout << "Bodies created: " << stats.bodiesCreated << '\n';
    std::cout << "Bodies found: " << stats.bodiesFound << '\n';
    std::cout << "Sigils acquired: " << stats.sigilsAcquired << '\n';
    std::cout << "Extractions activated: " << stats.extractionsActivated << '\n';
    std::cout << "Escape-available events: " << stats.escapeAvailableEvents << '\n';
    std::cout << "Players escaped: " << stats.playerEscapes << '\n';

    std::cout << "\nJACKAL TELEMETRY\n";
    std::cout << "Robberies: " << stats.jackalRobberies << '\n';
    std::cout << "Scares: " << stats.jackalScares << '\n';
    std::cout << "Knockouts: " << stats.jackalKnockouts << '\n';
    std::cout << "Stuns: " << stats.jackalStuns << '\n';
    std::cout << "Jackal warning player-rounds: " << stats.jackalWarningPlayerRounds << '\n';

    std::cout << "\nRESOURCE TELEMETRY\n";
    std::cout << "Arrows fired: " << stats.arrowsFired << '\n';
    std::cout << "Blind shots: " << stats.blindShots << '\n';
    std::cout << "Average arrows remaining/hunter: " << avgFinalArrows << '\n';
    std::cout << "Searches: " << stats.searches << '\n';
    std::cout << "Arrows found: " << stats.arrowsFound << '\n';
    std::cout << "Items found: " << stats.itemsFound << '\n';
    std::cout << "Heals used: " << stats.heals << '\n';

    return 0;
}
