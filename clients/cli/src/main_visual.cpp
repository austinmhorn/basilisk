#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "DebugTruthState.hpp"
#include "WebDebugState.hpp"
#include "basilisk/Action.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/systems/RoundController.hpp"
#include "basilisk/systems/SoloCoordinator.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

constexpr const char* kBrowserActionPath = "basilisk_debug_action.txt";

std::string itemName(ItemType item) { return cli_debug::itemName(item); }

std::string targetText(const AvailableAction& action) {
    if (action.targetCave.has_value()) return "Cave " + std::to_string(*action.targetCave);
    if (action.targetTunnel.has_value()) return "Tunnel " + std::to_string(*action.targetTunnel) + " (unknown destination)";
    return {};
}

std::string actionText(const AvailableAction& action) {
    switch (action.type) {
        case ActionType::Move: return "Move through " + targetText(action);
        case ActionType::Search: return "Search this cave";
        case ActionType::Shoot: return "Fire an arrow toward " + targetText(action);
        case ActionType::UseItem: return action.targetItem.has_value() ? "Use " + itemName(*action.targetItem) : "Use item";
        case ActionType::Contextual:
            if (action.contextualAction == ContextualActionType::Escape) return "Escape with the Hunter's Sigil";
            return "Contextual action";
    }
    return "Unknown action";
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

void printSnapshot(const PlayerRoundSnapshot& snapshot) {
    std::cout << "\nHunter " << snapshot.player << " · Round " << snapshot.round
              << " · Cave " << snapshot.currentCave
              << " · HP " << snapshot.health << '/' << snapshot.maxHealth
              << " · Arrows " << snapshot.arrows << '/' << snapshot.maxArrows << "\n";
    std::cout << "Browser map updated in basilisk_debug_state.json\n";
    if (!snapshot.observations.empty()) {
        std::cout << "\nROUND REPORT:\n";
        for (const auto& observation : snapshot.observations)
            std::cout << "  • " << cli_debug::observationText(observation) << '\n';
    }
    std::cout << '\n';
    for (std::size_t i = 0; i < snapshot.availableActions.size(); ++i)
        std::cout << "  " << i + 1 << ") " << actionText(snapshot.availableActions[i]) << '\n';
}

std::optional<PlayerAction> promptTerminal(const PlayerRoundSnapshot& snapshot) {
    if (!snapshot.alive || snapshot.availableActions.empty()) return std::nullopt;
    printSnapshot(snapshot);
    while (true) {
        std::cout << "\nAction [1-" << snapshot.availableActions.size() << "]: ";
        std::size_t choice = 0;
        if (std::cin >> choice && choice >= 1 && choice <= snapshot.availableActions.size())
            return materialize(snapshot.player, snapshot.availableActions[choice - 1]);
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Invalid choice.\n";
    }
}

std::optional<PlayerAction> promptBrowser(const PlayerRoundSnapshot& snapshot) {
    if (!snapshot.alive || snapshot.availableActions.empty()) return std::nullopt;
    printSnapshot(snapshot);
    std::cout << "\nWaiting for browser action...\n";
    while (true) {
        std::ifstream input(kBrowserActionPath);
        if (input) {
            PlayerId player{};
            RoundNumber round{};
            std::size_t choice = 0;
            if (input >> player >> round >> choice) {
                input.close();
                std::error_code ec;
                std::filesystem::remove(kBrowserActionPath, ec);
                if (player == snapshot.player && round == snapshot.round &&
                    choice >= 1 && choice <= snapshot.availableActions.size()) {
                    std::cout << "Browser selected " << choice << ") "
                              << actionText(snapshot.availableActions[choice - 1]) << '\n';
                    return materialize(snapshot.player, snapshot.availableActions[choice - 1]);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
    }
}

std::string outcome(const PlayerRoundSnapshot& snapshot) {
    const bool basiliskFoundPlayer = std::any_of(
        snapshot.observations.begin(),
        snapshot.observations.end(),
        [](const PlayerObservation& observation) {
            return observation.type == ObservationType::BasiliskFoundYou;
        });
    if (basiliskFoundPlayer) return "The Basilisk found you. You did not survive.";

    switch (snapshot.matchOutcome) {
        case MatchOutcome::BasiliskKilled:
            return snapshot.winner.has_value() ? "Hunter " + std::to_string(*snapshot.winner) + " killed the Basilisk." : "The Basilisk was killed.";
        case MatchOutcome::SimultaneousBasiliskKill: return "Both hunters killed the Basilisk simultaneously. Draw.";
        case MatchOutcome::EscapedWithSigil:
            return snapshot.winner.has_value() ? "Hunter " + std::to_string(*snapshot.winner) + " escaped with the rival Sigil." : "A hunter escaped with the rival Sigil.";
        case MatchOutcome::Draw: return "No hunter survived. Draw.";
        case MatchOutcome::None: return "Match ended without a recorded result.";
    }
    return "Unknown result.";
}

} // namespace

int main(int argc, char** argv) {
    MapSeed mapSeed = 20260812;
    MatchSeed matchSeed = 424242;
    bool browserActions = false;
    bool solo = false;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--browser-actions") browserActions = true;
        else if (arg == "--solo") solo = true;
        else if (positional == 0) { mapSeed = static_cast<MapSeed>(std::stoull(arg)); ++positional; }
        else if (positional == 1) { matchSeed = static_cast<MatchSeed>(std::stoull(arg)); ++positional; }
    }

    auto state = MapGenerator::generate(mapSeed, matchSeed);
    if (solo) {
        std::erase_if(state.players, [](const PlayerState& player) {
            return player.id != PlayerId{1};
        });
    }
    RoundController controller;
    SoloCoordinator soloCoordinator{state};
    std::vector<GameEvent> events;
    const std::vector<PlayerId> activePlayers = solo
        ? std::vector<PlayerId>{PlayerId{1}}
        : std::vector<PlayerId>{PlayerId{1}, PlayerId{2}};

    std::cout << "BASILISK VISUAL DEBUG CLIENT\n"
              << "Map seed: " << mapSeed << " | Match seed: " << matchSeed << "\n";
    if (solo) std::cout << "Solo debug mode enabled: only Hunter 1 is active.\n";
    if (browserActions) {
        std::cout << "Browser action mode enabled.\n"
                  << "Run: python3 clients/web-debug/server.py\n"
                  << "Open: http://localhost:8765/clients/web-debug/index.html\n";
    } else {
        std::cout << "Serve the repo root with: python3 -m http.server 8765\n"
                  << "Open: http://localhost:8765/clients/web-debug/index.html\n"
                  << "Tip: add --browser-actions and use clients/web-debug/server.py for point-and-click input.\n";
    }

    while (state.result.status == MatchStatus::Active) {
        std::vector<PlayerAction> actions;
        for (const PlayerId playerId : activePlayers) {
            const auto snapshot = SnapshotSystem::buildForPlayer(state, playerId, events);
            if (!snapshot.alive) continue;

            if (browserActions) {
                std::error_code ec;
                std::filesystem::remove(kBrowserActionPath, ec);
            }
            cli_debug::writeWebDebugState(snapshot);
            cli_debug::writeDebugTruthState(state, playerId, events);
            const auto action = browserActions ? promptBrowser(snapshot) : promptTerminal(snapshot);
            if (action.has_value()) actions.push_back(*action);
        }
        if (actions.empty()) break;
        if (solo) {
            if (!soloCoordinator.submitAction(actions.front())) break;
            events = soloCoordinator.lastEvents();
        } else {
            events = controller.resolve(state, actions);
        }
    }

    const auto finalSnapshot = SnapshotSystem::buildForPlayer(state, 1, events);
    cli_debug::writeWebDebugState(finalSnapshot);
    cli_debug::writeDebugTruthState(state, 1, events);
    std::cout << "\nHUNT COMPLETE: " << outcome(finalSnapshot) << '\n';
    return 0;
}
