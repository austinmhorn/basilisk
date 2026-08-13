#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "WebDebugState.hpp"
#include "basilisk/Action.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/systems/RoundController.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

std::string itemName(ItemType item) {
    return cli_debug::itemName(item);
}

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
        case ActionType::UseItem:
            return action.targetItem.has_value() ? "Use " + itemName(*action.targetItem) : "Use item";
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

std::optional<PlayerAction> prompt(const PlayerRoundSnapshot& snapshot) {
    if (!snapshot.alive || snapshot.availableActions.empty()) return std::nullopt;

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

bool alive(const MatchState& state, PlayerId playerId) {
    const auto it = std::find_if(state.players.begin(), state.players.end(),
        [playerId](const PlayerState& p) { return p.id == playerId; });
    return it != state.players.end() && it->alive;
}

void handoff(PlayerId next) {
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cout << "\nPass control to Hunter " << next << ". Press ENTER when ready.";
    std::cin.get();
    std::cout << std::string(10, '\n');
}

std::string outcome(const PlayerRoundSnapshot& snapshot) {
    switch (snapshot.matchOutcome) {
        case MatchOutcome::BasiliskKilled:
            return snapshot.winner.has_value()
                ? "Hunter " + std::to_string(*snapshot.winner) + " killed the Basilisk."
                : "The Basilisk was killed.";
        case MatchOutcome::SimultaneousBasiliskKill:
            return "Both hunters killed the Basilisk simultaneously. Draw.";
        case MatchOutcome::EscapedWithSigil:
            return snapshot.winner.has_value()
                ? "Hunter " + std::to_string(*snapshot.winner) + " escaped with the rival Sigil."
                : "A hunter escaped with the rival Sigil.";
        case MatchOutcome::Draw: return "No hunter survived. Draw.";
        case MatchOutcome::None: return "Match ended without a recorded result.";
    }
    return "Unknown result.";
}

} // namespace

int main(int argc, char** argv) {
    MapSeed mapSeed = 20260812;
    MatchSeed matchSeed = 424242;
    if (argc > 1) mapSeed = static_cast<MapSeed>(std::stoull(argv[1]));
    if (argc > 2) matchSeed = static_cast<MatchSeed>(std::stoull(argv[2]));

    auto state = MapGenerator::generate(mapSeed, matchSeed);
    RoundController controller;
    std::vector<GameEvent> events;

    std::cout << "BASILISK VISUAL DEBUG CLIENT\n"
              << "Map seed: " << mapSeed << " | Match seed: " << matchSeed << "\n"
              << "Serve the repo root with: python3 -m http.server 8765\n"
              << "Open: http://localhost:8765/clients/web-debug/index.html\n";

    while (state.result.status == MatchStatus::Active) {
        std::vector<PlayerAction> actions;
        for (PlayerId playerId : {PlayerId{1}, PlayerId{2}}) {
            const auto snapshot = SnapshotSystem::buildForPlayer(state, playerId, events);
            if (!snapshot.alive) continue;

            cli_debug::writeWebDebugState(snapshot);
            if (const auto action = prompt(snapshot); action.has_value()) actions.push_back(*action);

            if (playerId == 1 && alive(state, 2)) handoff(2);
        }

        if (actions.empty()) break;
        events = controller.resolve(state, actions);
    }

    const auto finalSnapshot = SnapshotSystem::buildForPlayer(state, 1, events);
    cli_debug::writeWebDebugState(finalSnapshot);
    std::cout << "\nHUNT COMPLETE: " << outcome(finalSnapshot) << '\n';
    return 0;
}
