#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/systems/MapDiscoverySystem.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"

using namespace basilisk;

namespace {

MatchState makeSnapshotWorld() {
    MatchState state;
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FogOfWar;
    state.rules.maxHealth = 100;
    state.rules.maxArrows = 5;
    state.rules.maxInventoryItems = 3;

    for (CaveId cave = 1; cave <= 6; ++cave) {
        state.world.addCave(cave);
    }
    state.world.connect(1, 2);
    state.world.connect(1, 3);
    state.world.connect(2, 4);
    state.world.connect(3, 5);
    state.world.connect(4, 6);
    state.world.connect(5, 6);

    PlayerState a;
    a.id = 1;
    a.cave = 1;
    a.health = 60;
    a.arrows = 2;
    const bool added = a.inventory.add(ItemInstance{ItemType::HealingDraught}, 3);
    assert(added);

    PlayerState b;
    b.id = 2;
    b.cave = 2;
    b.health = 100;
    b.arrows = 4;

    state.players = {a, b};
    state.basilisk.cave = 6;
    state.pits.push_back(PitState{3, true});

    JackalState jackal;
    jackal.cave = 5;
    state.jackals.push_back(jackal);

    return state;
}

bool hasAction(const PlayerRoundSnapshot& snapshot, ActionType type) {
    return std::any_of(
        snapshot.availableActions.begin(),
        snapshot.availableActions.end(),
        [type](const AvailableAction& action) { return action.type == type; });
}

bool hasObservation(const PlayerRoundSnapshot& snapshot, ObservationType type) {
    return std::any_of(
        snapshot.observations.begin(),
        snapshot.observations.end(),
        [type](const PlayerObservation& observation) { return observation.type == type; });
}

const DiscoveredCaveView& caveView(const PlayerMapView& view, CaveId cave) {
    const auto it = std::find_if(view.caves.begin(), view.caves.end(),
        [cave](const DiscoveredCaveView& entry) { return entry.cave == cave; });
    assert(it != view.caves.end());
    return *it;
}

void snapshotContainsOnlyPlayerFacingCoreState() {
    const auto state = makeSnapshotWorld();
    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});

    assert(snapshot.player == 1);
    assert(snapshot.currentCave == 1);
    assert(snapshot.health == 60);
    assert(snapshot.maxHealth == 100);
    assert(snapshot.arrows == 2);
    assert(snapshot.maxArrows == 5);
    assert(snapshot.inventory.capacity == 3);
    assert(snapshot.inventory.items.size() == 1);
    assert(snapshot.inventory.items[0] == ItemType::HealingDraught);

    // Fog-of-war snapshot begins with only the viewer's current cave. The
    // authoritative rival/Basilisk/Pit/Jackal cave IDs are not part of this
    // snapshot model at all.
    assert(snapshot.map.caves.size() == 1);
    assert(snapshot.map.caves.front().cave == 1);
    assert(snapshot.map.caves.front().exits.size() == 2);
    for (const auto& exit : snapshot.map.caves.front().exits) {
        assert(!exit.destination.has_value());
    }
}

void snapshotDerivesLegalActionsFromVisibleKnowledge() {
    const auto state = makeSnapshotWorld();
    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});

    int moveCount = 0;
    int shootCount = 0;
    int healCount = 0;
    for (const auto& action : snapshot.availableActions) {
        if (action.type == ActionType::Move) {
            ++moveCount;
            assert(action.targetTunnel.has_value());
            assert(!action.targetCave.has_value());
        }
        if (action.type == ActionType::Shoot) {
            ++shootCount;
            assert(action.targetTunnel.has_value());
            assert(!action.targetCave.has_value());
        }
        if (action.type == ActionType::UseItem) {
            ++healCount;
            assert(action.targetItem == ItemType::HealingDraught);
        }
    }

    assert(moveCount == 2);
    assert(shootCount == 2);
    assert(healCount == 1);
    assert(hasAction(snapshot, ActionType::Search));
}

void opaqueActionsAreScopedToCurrentCave() {
    auto state = makeSnapshotWorld();
    state.players[0].discovery.knownCaves.insert(CaveId{4});

    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    const auto& current = caveView(snapshot.map, CaveId{1});
    const auto& historical = caveView(snapshot.map, CaveId{4});

    // TunnelId is local to its source cave. Both views legitimately contain
    // an unresolved Tunnel 1, but actions are generated only from currentCave.
    assert(current.exits[0].id == TunnelId{1});
    assert(!current.exits[0].destination.has_value());
    assert(historical.exits[0].id == TunnelId{1});
    assert(!historical.exits[0].destination.has_value());
    assert(snapshot.map.currentCave == CaveId{1});

    int opaqueMoveCount = 0;
    for (const auto& action : snapshot.availableActions) {
        if (action.type != ActionType::Move ||
            action.targetTunnel != TunnelId{1}) continue;
        ++opaqueMoveCount;
    }
    assert(opaqueMoveCount == 1);
}

void sharedDiscoveredDestinationIsReachableFromEitherCave() {
    MatchState state;
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FogOfWar;
    for (const CaveId cave : {CaveId{2}, CaveId{4}, CaveId{13}, CaveId{24}})
        state.world.addCave(cave);
    state.world.connect(13, 4);
    state.world.connect(4, 2);
    state.world.connect(4, 24);
    state.world.connect(2, 24);
    state.players = {PlayerState{1, 4, 100, 3, true}};
    state.basilisk.cave = 13;

    auto& player = state.players.front();
    std::vector<GameEvent> events;
    MapDiscoverySystem::initializePlayer(state, player);
    MapDiscoverySystem::discoverTraversal(player, 4, 24, events);
    MapDiscoverySystem::discoverTraversal(player, 4, 2, events);

    // The player learned Cave 24 through Cave 4, not by traversing 2 -> 24.
    assert(!MapDiscoverySystem::knowsConnection(player, 2, 24));

    player.cave = 2;
    const auto fromTwo = SnapshotSystem::buildForPlayer(state, 1, {});
    assert(std::any_of(
        fromTwo.availableActions.begin(),
        fromTwo.availableActions.end(),
        [](const AvailableAction& action) {
            return action.type == ActionType::Move &&
                   action.targetCave == CaveId{24};
        }));

    player.cave = 4;
    const auto fromFour = SnapshotSystem::buildForPlayer(state, 1, {});
    assert(std::any_of(
        fromFour.availableActions.begin(),
        fromFour.availableActions.end(),
        [](const AvailableAction& action) {
            return action.type == ActionType::Move &&
                   action.targetCave == CaveId{24};
        }));
}

void snapshotUsesFilteredObservationsInsteadOfHiddenState() {
    const auto state = makeSnapshotWorld();
    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});

    assert(hasObservation(snapshot, ObservationType::RivalNearby));
    assert(hasObservation(snapshot, ObservationType::PitNearby));

    for (const auto& observation : snapshot.observations) {
        if (observation.type == ObservationType::RivalNearby ||
            observation.type == ObservationType::PitNearby ||
            observation.type == ObservationType::JackalNearby ||
            observation.type == ObservationType::BasiliskNearby ||
            observation.type == ObservationType::BasiliskNearbySubtle) {
            assert(!observation.cave.has_value());
        }
    }
}

void extractionIsVisibleOnlyToEligibleSigilHolder() {
    auto state = makeSnapshotWorld();
    state.players[0].heldSigilFrom = PlayerId{2};
    state.extraction.active = true;
    state.extraction.cave = CaveId{4};
    state.extraction.sigilHolder = PlayerId{1};
    state.extraction.revealPolicy = ExtractionRevealPolicy::RevealImmediately;

    const auto a = SnapshotSystem::buildForPlayer(state, 1, {});
    const auto b = SnapshotSystem::buildForPlayer(state, 2, {});

    assert(a.hasHunterSigil);
    assert(a.extractionCave == CaveId{4});
    assert(!b.hasHunterSigil);
    assert(!b.extractionCave.has_value());
}

void escapeActionAppearsOnlyAtActiveExtraction() {
    auto state = makeSnapshotWorld();
    state.players[0].heldSigilFrom = PlayerId{2};
    state.extraction.active = true;
    state.extraction.cave = CaveId{1};
    state.extraction.sigilHolder = PlayerId{1};

    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});

    bool foundEscape = false;
    for (const auto& action : snapshot.availableActions) {
        if (action.type == ActionType::Contextual &&
            action.contextualAction == ContextualActionType::Escape) {
            foundEscape = true;
        }
    }
    assert(foundEscape);
}

void completedMatchOffersNoFurtherActions() {
    auto state = makeSnapshotWorld();
    state.result.status = MatchStatus::Completed;
    state.result.outcome = MatchOutcome::BasiliskKilled;
    state.result.winner = PlayerId{1};

    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    assert(snapshot.availableActions.empty());
    assert(snapshot.matchStatus == MatchStatus::Completed);
    assert(snapshot.matchOutcome == MatchOutcome::BasiliskKilled);
    assert(snapshot.winner == PlayerId{1});
}

void fullHealthDoesNotAdvertiseHealingAction() {
    auto state = makeSnapshotWorld();
    state.players[0].health = state.rules.maxHealth;

    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    for (const auto& action : snapshot.availableActions) {
        assert(!(action.type == ActionType::UseItem &&
                 action.targetItem == ItemType::HealingDraught));
    }
}

} // namespace

int main() {
    snapshotContainsOnlyPlayerFacingCoreState();
    snapshotDerivesLegalActionsFromVisibleKnowledge();
    opaqueActionsAreScopedToCurrentCave();
    sharedDiscoveredDestinationIsReachableFromEitherCave();
    snapshotUsesFilteredObservationsInsteadOfHiddenState();
    extractionIsVisibleOnlyToEligibleSigilHolder();
    escapeActionAppearsOnlyAtActiveExtraction();
    completedMatchOffersNoFurtherActions();
    fullHealthDoesNotAdvertiseHealingAction();

    std::cout << "Basilisk snapshot tests passed.\n";
    return 0;
}
