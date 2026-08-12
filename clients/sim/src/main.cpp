#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
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
    std::uint64_t pvpDeaths{0};
    std::uint64_t arrowsFired{0};
    std::uint64_t searches{0};
    std::uint64_t jackalEncounters{0};
    std::uint64_t totalRounds{0};
    std::uint64_t totalCavesDiscovered{0};
};

bool hasObservation(const PlayerRoundSnapshot& snapshot, ObservationType type) {
    return std::any_of(snapshot.observations.begin(), snapshot.observations.end(),
        [type](const PlayerObservation& observation) { return observation.type == type; });
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

    const bool threatNearby =
        hasObservation(snapshot, ObservationType::BasiliskNearby) ||
        hasObservation(snapshot, ObservationType::BasiliskNearbySubtle) ||
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

void accumulateEvents(const std::vector<GameEvent>& events, SimulationStats& stats) {
    for (const auto& event : events) {
        switch (event.type) {
            case GameEventType::ArrowFired: ++stats.arrowsFired; break;
            case GameEventType::SearchCompleted: ++stats.searches; break;
            case GameEventType::PitTriggered: ++stats.pitDeaths; break;
            case GameEventType::JackalRobbedArrow:
            case GameEventType::JackalScaredPlayer:
            case GameEventType::JackalKnockedOutPlayer:
                ++stats.jackalEncounters;
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
        for (const PlayerId player : living) {
            const auto snapshot = SnapshotSystem::buildForPlayer(state, player, previousEvents);
            if (const auto action = chooseBotAction(snapshot, matchSeed); action.has_value()) {
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
        accumulateEvents(previousEvents, stats);
    }

    ++stats.matches;
    stats.totalRounds += std::min<std::uint64_t>(state.round, maxRounds);
    for (const auto& player : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
        stats.totalCavesDiscovered += snapshot.map.caves.size();
    }
    accumulateResult(state, stats);
}

void printPercent(const char* label, std::uint64_t value, std::uint64_t total) {
    const double pct = total == 0 ? 0.0 : (100.0 * static_cast<double>(value) / static_cast<double>(total));
    std::cout << label << ": " << value << " (" << pct << "%)\n";
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

    std::cout << "\nAverage rounds: " << avgRounds << '\n';
    std::cout << "Average caves discovered/hunter: " << avgCavesPerHunter << '\n';
    std::cout << "Arrows fired: " << stats.arrowsFired << '\n';
    std::cout << "Searches: " << stats.searches << '\n';
    std::cout << "Pit deaths: " << stats.pitDeaths << '\n';
    std::cout << "PvP deaths: " << stats.pvpDeaths << '\n';
    std::cout << "Jackal encounters: " << stats.jackalEncounters << '\n';

    return 0;
}
