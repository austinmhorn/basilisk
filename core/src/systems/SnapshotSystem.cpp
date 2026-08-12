#include "basilisk/systems/SnapshotSystem.hpp"

#include <algorithm>
#include <stdexcept>

#include "basilisk/Extraction.hpp"
#include "basilisk/systems/MapDiscoverySystem.hpp"
#include "basilisk/systems/ObservationSystem.hpp"

namespace basilisk {
namespace {

PlayerState& playerById(MatchState& state, PlayerId id) {
    const auto it = std::find_if(
        state.players.begin(),
        state.players.end(),
        [id](const PlayerState& player) { return player.id == id; });

    if (it == state.players.end()) {
        throw std::invalid_argument("Snapshot requested for an unknown player.");
    }
    return *it;
}

bool extractionVisibleTo(
    const MatchState& state,
    const PlayerState& player) {

    if (!state.extraction.active ||
        !state.extraction.cave.has_value() ||
        state.extraction.sigilHolder != player.id) {
        return false;
    }

    const CaveId extraction = *state.extraction.cave;
    switch (state.extraction.revealPolicy) {
        case ExtractionRevealPolicy::RevealImmediately:
            return true;
        case ExtractionRevealPolicy::DiscoverThroughExploration:
            return player.discovery.knownCaves.contains(extraction);
        case ExtractionRevealPolicy::ProximityOnly:
            return player.cave == extraction ||
                   state.world.areConnected(player.cave, extraction);
        case ExtractionRevealPolicy::Hidden:
            return false;
    }

    return false;
}

void appendTravelActions(
    const PlayerMapView& map,
    ActionType type,
    std::vector<AvailableAction>& actions) {

    const auto currentIt = std::find_if(
        map.caves.begin(),
        map.caves.end(),
        [&](const DiscoveredCaveView& cave) { return cave.cave == map.currentCave; });

    if (currentIt == map.caves.end()) return;

    for (const auto& exit : currentIt->exits) {
        AvailableAction action;
        action.type = type;
        if (exit.destination.has_value()) {
            action.targetCave = *exit.destination;
        } else {
            action.targetTunnel = exit.id;
        }
        actions.push_back(action);
    }
}

std::vector<AvailableAction> buildAvailableActions(
    const MatchState& state,
    const PlayerState& player,
    const PlayerMapView& map) {

    std::vector<AvailableAction> actions;
    if (!player.alive || state.result.status != MatchStatus::Active) {
        return actions;
    }

    appendTravelActions(map, ActionType::Move, actions);

    AvailableAction search;
    search.type = ActionType::Search;
    actions.push_back(search);

    if (player.arrows > 0) {
        appendTravelActions(map, ActionType::Shoot, actions);
    }

    // HealingDraught is currently the only implemented usable inventory item.
    if (player.health < state.rules.maxHealth &&
        player.inventory.contains(ItemType::HealingDraught)) {
        AvailableAction heal;
        heal.type = ActionType::UseItem;
        heal.targetItem = ItemType::HealingDraught;
        actions.push_back(heal);
    }

    if (player.heldSigilFrom.has_value() &&
        state.extraction.active &&
        state.extraction.sigilHolder == player.id &&
        state.extraction.cave == player.cave) {
        AvailableAction escape;
        escape.type = ActionType::Contextual;
        escape.contextualAction = ContextualActionType::Escape;
        actions.push_back(escape);
    }

    return actions;
}

} // namespace

PlayerRoundSnapshot SnapshotSystem::buildForPlayer(
    const MatchState& state,
    PlayerId viewer,
    const std::vector<GameEvent>& events) {

    // Discovery initialization is logically player-visible state. Work on a
    // copy so snapshot generation remains a const/read-only operation for the
    // authoritative MatchState.
    MatchState visibleState = state;
    auto& player = playerById(visibleState, viewer);
    MapDiscoverySystem::initializePlayer(visibleState, player);

    PlayerRoundSnapshot snapshot;
    snapshot.player = player.id;
    snapshot.round = visibleState.round;
    snapshot.health = player.health;
    snapshot.maxHealth = visibleState.rules.maxHealth;
    snapshot.arrows = player.arrows;
    snapshot.maxArrows = visibleState.rules.maxArrows;
    snapshot.alive = player.alive;
    snapshot.currentCave = player.cave;
    snapshot.map = MapDiscoverySystem::buildView(visibleState, player);

    snapshot.inventory.capacity = visibleState.rules.maxInventoryItems;
    snapshot.inventory.items.reserve(player.inventory.items.size());
    for (const auto& item : player.inventory.items) {
        snapshot.inventory.items.push_back(item.type);
    }

    snapshot.hasHunterSigil = player.heldSigilFrom.has_value();
    if (extractionVisibleTo(visibleState, player)) {
        snapshot.extractionCave = visibleState.extraction.cave;
    }

    snapshot.availableActions = buildAvailableActions(
        visibleState,
        player,
        snapshot.map);
    snapshot.observations = ObservationSystem::buildForPlayer(
        visibleState,
        viewer,
        events);

    snapshot.matchStatus = visibleState.result.status;
    snapshot.matchOutcome = visibleState.result.outcome;
    snapshot.winner = visibleState.result.winner;

    return snapshot;
}

} // namespace basilisk
