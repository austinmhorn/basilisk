#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/Observation.hpp"
#include "basilisk/systems/RoundController.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

std::string itemName(ItemType item) {
    switch (item) {
        case ItemType::HealingDraught: return "Healing Draught";
        case ItemType::OldMinersMap: return "Old Miner's Map";
        case ItemType::SurveyFragment: return "Survey Fragment";
        case ItemType::JackalRepellent: return "Jackal Repellent";
        case ItemType::BloodBait: return "Blood Bait";
    }
    return "Unknown Item";
}

std::string observationText(const PlayerObservation& observation) {
    switch (observation.type) {
        case ObservationType::RivalNearby: return "You hear another hunter moving somewhere nearby.";
        case ObservationType::PitNearby: return "A cold draft rises from one of the nearby tunnels.";
        case ObservationType::JackalNearby: return "You hear a jackal prowling nearby.";
        case ObservationType::BasiliskNearby: return "A terrible presence feels close.";
        case ObservationType::BasiliskNearbySubtle: return "The cavern goes strangely quiet.";
        case ObservationType::RestlessBasiliskNoise: return "A heavy scraping sound echoes through distant tunnels.";
        case ObservationType::EnragedLastKnownCave:
            return observation.cave.has_value()
                ? "The enraged Basilisk was last seen in Cave " + std::to_string(*observation.cave) + "."
                : "The enraged Basilisk left a recent trail.";
        case ObservationType::PitInvestigationSucceeded:
            return observation.tunnel.has_value()
                ? "The cold draft is strongest from Tunnel " + std::to_string(*observation.tunnel) + "."
                : "You identify the direction of the cold draft.";
        case ObservationType::PitInvestigationInconclusive:
            return "The air shifts unpredictably. You can't tell which tunnel the draft is coming from.";
        case ObservationType::ArrowHitYou: return "An arrow strikes you.";
        case ObservationType::YouWereDamaged: return "You take " + std::to_string(observation.amount) + " damage.";
        case ObservationType::YouKilledRival: return "The rival hunter falls.";
        case ObservationType::YouDied: return "You have died.";
        case ObservationType::FellIntoPit: return "You stepped into a hidden Pit and fell to your death.";
        case ObservationType::RivalDied:
            return "The rival hunter has died somewhere in the caverns. The hunt continues; their Sigil can still be recovered.";
        case ObservationType::RivalDisconnected:
            return "The rival hunter disconnected. Their place in the hunt is being held briefly.";
        case ObservationType::RivalReconnected:
            return "The rival hunter reconnected and has returned to the hunt.";
        case ObservationType::RivalReserveExpired:
            return "The rival hunter exhausted their decision reserve and is out of the hunt.";
        case ObservationType::RivalDisconnectTimedOut:
            return "The rival hunter did not return before the reconnect grace expired and is out of the hunt.";
        case ObservationType::ItemFound:
            return observation.itemType.has_value()
                ? "You found: " + itemName(*observation.itemType) + "."
                : "You found an item.";
        case ObservationType::ArrowFound: return "You found " + std::to_string(observation.amount) + " arrow(s).";
        case ObservationType::ExoticCallingCardFound: return "EXOTIC DISCOVERY: a Calling Card reward has been found.";
        case ObservationType::SigilAcquired: return "You recovered the fallen hunter's Sigil.";
        case ObservationType::ExtractionRevealed:
            return observation.cave.has_value()
                ? "Extraction has activated at Cave " + std::to_string(*observation.cave) + "."
                : "Extraction has activated.";
        case ObservationType::EscapeAvailable: return "You can escape from this cave.";
        case ObservationType::BasiliskEvaded: return "Your arrow found the Basilisk, but it evaded the killing blow.";
        case ObservationType::BasiliskBehaviorChanged: return "Something about the Basilisk's behavior has changed.";
        case ObservationType::BasiliskKilled: return "The Basilisk is dead.";
        case ObservationType::MatchDrawn: return "The hunt ends in a draw.";
    }
    return "Something happened.";
}

std::string actionText(const AvailableAction& action) {
    auto targetText = [&]() {
        if (action.targetCave.has_value()) return std::string{"Cave "} + std::to_string(*action.targetCave);
        if (action.targetTunnel.has_value()) {
            return std::string{"Tunnel "} + std::to_string(*action.targetTunnel) + " (unknown destination)";
        }
        return std::string{};
    };

    switch (action.type) {
        case ActionType::Move: return "MOVE -> " + targetText();
        case ActionType::Search: return "SEARCH current cave";
        case ActionType::Shoot: return "SHOOT -> " + targetText();
        case ActionType::UseItem:
            return action.targetItem.has_value() ? "USE -> " + itemName(*action.targetItem) : "USE ITEM";
        case ActionType::Contextual:
            if (action.contextualAction == ContextualActionType::Escape) return "ESCAPE";
            return "CONTEXTUAL ACTION";
    }
    return "UNKNOWN ACTION";
}

PlayerAction materializeAction(PlayerId player, const AvailableAction& available) {
    PlayerAction action;
    action.player = player;
    action.type = available.type;
    action.targetCave = available.targetCave;
    action.targetTunnel = available.targetTunnel;
    action.targetItem = available.targetItem;
    action.contextualAction = available.contextualAction;
    return action;
}

const DiscoveredCaveView* currentCaveView(const PlayerRoundSnapshot& snapshot) {
    const auto it = std::find_if(snapshot.map.caves.begin(), snapshot.map.caves.end(),
        [&](const DiscoveredCaveView& cave) { return cave.cave == snapshot.currentCave; });
    return it == snapshot.map.caves.end() ? nullptr : &*it;
}

void printCurrentCave(const PlayerRoundSnapshot& snapshot) {
    std::cout << "\nCURRENT CAVE: " << snapshot.currentCave << '\n';
    std::cout << "Exits:\n";
    const auto* cave = currentCaveView(snapshot);
    if (cave == nullptr || cave->exits.empty()) {
        std::cout << "  none\n";
        return;
    }
    for (const auto& exit : cave->exits) {
        std::cout << "  Tunnel " << exit.id << " -> ";
        if (exit.destination.has_value()) std::cout << "Cave " << *exit.destination;
        else std::cout << "UNKNOWN";
        if (exit.strongColdDraft) std::cout << "  [STRONG COLD DRAFT]";
        std::cout << '\n';
    }
}

void printExplorationHistory(const PlayerRoundSnapshot& snapshot) {
    std::cout << "\nExploration history (" << snapshot.map.caves.size() << " cave(s) discovered):\n";
    bool printed = false;
    for (const auto& cave : snapshot.map.caves) {
        if (cave.cave == snapshot.currentCave) continue;
        printed = true;
        std::cout << "  Cave " << cave.cave << ": ";
        for (std::size_t i = 0; i < cave.exits.size(); ++i) {
            const auto& exit = cave.exits[i];
            if (exit.destination.has_value()) std::cout << *exit.destination;
            else std::cout << "?";
            if (exit.strongColdDraft) std::cout << "!";
            if (i + 1 < cave.exits.size()) std::cout << " | ";
        }
        std::cout << '\n';
    }
    if (!printed) std::cout << "  No previously explored caves yet.\n";
}

void printSnapshot(const PlayerRoundSnapshot& snapshot) {
    std::cout << "\n============================================================\n";
    std::cout << "PLAYER " << snapshot.player << " | ROUND " << snapshot.round;
    if (!snapshot.alive) std::cout << " | DEAD";
    std::cout << '\n';
    std::cout << "HP " << snapshot.health << '/' << snapshot.maxHealth
              << " | Arrows " << snapshot.arrows << '/' << snapshot.maxArrows
              << " | Inventory " << snapshot.inventory.items.size() << '/' << snapshot.inventory.capacity << '\n';

    if (!snapshot.inventory.items.empty()) {
        std::cout << "Items: ";
        for (std::size_t i = 0; i < snapshot.inventory.items.size(); ++i) {
            std::cout << itemName(snapshot.inventory.items[i]);
            if (i + 1 < snapshot.inventory.items.size()) std::cout << ", ";
        }
        std::cout << '\n';
    }

    if (snapshot.hasHunterSigil) std::cout << "Hunter's Sigil: ACQUIRED\n";
    if (snapshot.extractionCave.has_value()) std::cout << "Extraction: Cave " << *snapshot.extractionCave << '\n';

    if (!snapshot.observations.empty()) {
        std::cout << "\nROUND REPORT:\n";
        for (const auto& observation : snapshot.observations) {
            std::cout << "  - " << observationText(observation) << '\n';
        }
    }

    printCurrentCave(snapshot);
    printExplorationHistory(snapshot);
}

std::optional<PlayerAction> promptAction(const PlayerRoundSnapshot& snapshot) {
    if (!snapshot.alive || snapshot.availableActions.empty()) return std::nullopt;
    std::cout << "\nAVAILABLE ACTIONS:\n";
    for (std::size_t i = 0; i < snapshot.availableActions.size(); ++i) {
        std::cout << "  " << (i + 1) << ") " << actionText(snapshot.availableActions[i]) << '\n';
    }
    while (true) {
        std::cout << "Choose action [1-" << snapshot.availableActions.size() << "]: ";
        std::size_t choice = 0;
        if (std::cin >> choice && choice >= 1 && choice <= snapshot.availableActions.size()) {
            return materializeAction(snapshot.player, snapshot.availableActions[choice - 1]);
        }
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Invalid choice.\n";
    }
}

std::optional<PlayerAction> firstSearchAction(const PlayerRoundSnapshot& snapshot) {
    const auto it = std::find_if(snapshot.availableActions.begin(), snapshot.availableActions.end(),
        [](const AvailableAction& action) { return action.type == ActionType::Search; });
    if (it == snapshot.availableActions.end()) return std::nullopt;
    return materializeAction(snapshot.player, *it);
}

int runSmoke() {
    auto state = MapGenerator::generate(20260812, 424242);
    std::vector<GameEvent> events;
    const auto a = SnapshotSystem::buildForPlayer(state, 1, events);
    const auto b = SnapshotSystem::buildForPlayer(state, 2, events);
    const auto aSearch = firstSearchAction(a);
    const auto bSearch = firstSearchAction(b);
    if (!aSearch.has_value() || !bSearch.has_value()) return 1;
    RoundController controller;
    events = controller.resolve(state, {*aSearch, *bSearch});
    const auto afterA = SnapshotSystem::buildForPlayer(state, 1, events);
    const auto afterB = SnapshotSystem::buildForPlayer(state, 2, events);
    if (afterA.round != 2 || afterB.round != 2) return 2;
    if (afterA.currentCave == 0 || afterB.currentCave == 0) return 3;
    std::cout << "Basilisk CLI smoke passed.\n";
    return 0;
}

std::string outcomeText(const PlayerRoundSnapshot& snapshot) {
    switch (snapshot.matchOutcome) {
        case MatchOutcome::BasiliskKilled:
            return snapshot.winner.has_value()
                ? "Player " + std::to_string(*snapshot.winner) + " killed the Basilisk."
                : "The Basilisk was killed.";
        case MatchOutcome::SimultaneousBasiliskKill:
            return "Both hunters struck the Basilisk down in the same round. The match is a draw.";
        case MatchOutcome::EscapedWithSigil:
            return snapshot.winner.has_value()
                ? "Player " + std::to_string(*snapshot.winner) + " escaped the caverns with the rival Hunter's Sigil."
                : "A hunter escaped with the rival Sigil.";
        case MatchOutcome::Draw: return "Both hunters died. The hunt ends in a draw.";
        case MatchOutcome::None: return "No result.";
    }
    return "Unknown result.";
}

bool playerAlive(const MatchState& state, PlayerId id) {
    const auto it = std::find_if(state.players.begin(), state.players.end(),
        [id](const PlayerState& player) { return player.id == id; });
    return it != state.players.end() && it->alive;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string{argv[1]} == "--smoke") return runSmoke();

    MapSeed mapSeed = 20260812;
    MatchSeed matchSeed = 424242;
    if (argc > 1) mapSeed = static_cast<MapSeed>(std::stoull(argv[1]));
    if (argc > 2) matchSeed = static_cast<MatchSeed>(std::stoull(argv[2]));

    auto state = MapGenerator::generate(mapSeed, matchSeed);
    RoundController controller;
    std::vector<GameEvent> events;
    std::unordered_set<PlayerId> deathScreensShown;

    std::cout << "BEWARE THE BASILISK V2 - HEADLESS PROTOTYPE\n";
    std::cout << "Map seed: " << mapSeed << " | Match seed: " << matchSeed << "\n";
    std::cout << "Local hot-seat prototype: living hunters choose before the round resolves.\n";

    while (state.result.status == MatchStatus::Active) {
        std::vector<PlayerAction> actions;

        for (PlayerId playerId : {PlayerId{1}, PlayerId{2}}) {
            const auto snapshot = SnapshotSystem::buildForPlayer(state, playerId, events);

            if (!snapshot.alive) {
                if (!deathScreensShown.contains(playerId)) {
                    printSnapshot(snapshot);
                    std::cout << "\nPLAYER " << playerId << " IS OUT OF THE HUNT.\n";
                    deathScreensShown.insert(playerId);
                }
                continue;
            }

            printSnapshot(snapshot);
            if (const auto action = promptAction(snapshot); action.has_value()) actions.push_back(*action);

            if (playerId == 1 && playerAlive(state, 2)) {
                std::cout << "\n--- Pass control to Player 2 ---\n";
            }
        }

        if (actions.empty()) break;
        events = controller.resolve(state, actions);
    }

    const auto finalSnapshot = SnapshotSystem::buildForPlayer(state, 1, events);
    std::cout << "\n============================================================\n";
    std::cout << "MATCH COMPLETE\n";
    std::cout << outcomeText(finalSnapshot) << '\n';
    return 0;
}
