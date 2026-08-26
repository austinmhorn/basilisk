#include <algorithm>
#include <array>
#include <cassert>
#include <set>
#include <vector>

#include "DebugMapProvider.hpp"
#include "DebugInventoryMenu.hpp"
#include "LocalGameSessionAdapter.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;
using namespace basilisk::game;
using namespace basilisk::game::debug;

namespace {

std::set<PhysicalTunnel> physicalTunnels(const MatchState& state) {
    std::set<PhysicalTunnel> tunnels;
    for (const CaveId source : state.world.caveIds()) {
        for (const CaveId destination : state.world.cave(source).connections) {
            const auto [first, second] = std::minmax(source, destination);
            tunnels.insert(PhysicalTunnel{first, second});
        }
    }
    return tunnels;
}

std::size_t playerMapSignature(const PlayerMapView& map) {
    std::size_t signature = static_cast<std::size_t>(map.currentCave);
    for (const DiscoveredCaveView& cave : map.caves) {
        signature = signature * 131U + static_cast<std::size_t>(cave.cave);
        for (const TunnelView& exit : cave.exits) {
            signature = signature * 131U + static_cast<std::size_t>(exit.id);
            signature = signature * 131U + static_cast<std::size_t>(
                exit.destination.value_or(CaveId{}));
            signature = signature * 2U + static_cast<std::size_t>(
                exit.strongColdDraft);
        }
    }
    return signature;
}

void revealContainsCompletePhysicalTopology() {
    constexpr MapSeed mapSeed{1};
    constexpr MatchSeed matchSeed{424242};
    const MatchState authoritative = MapGenerator::generate(mapSeed, matchSeed);
    auto debugSession = LocalGameSessionAdapter::createDebug(mapSeed, matchSeed);
    assert(debugSession.session != nullptr);
    assert(debugSession.mapProvider != nullptr);

    const DebugMapTruth& truth = debugSession.mapProvider->mapTruth();
    assert(truth.fullBounds.populated);
    assert(truth.cavePositions.size() == authoritative.world.size());
    assert(std::set<PhysicalTunnel>(
               truth.tunnels.begin(), truth.tunnels.end()) ==
           physicalTunnels(authoritative));
}

void togglingRevealDoesNotMutatePlayerState() {
    auto debugSession = LocalGameSessionAdapter::createDebug(
        MapSeed{1}, MatchSeed{424242});
    assert(debugSession.session != nullptr);
    const PlayerRoundSnapshot* snapshot =
        debugSession.session->displayedSnapshot();
    assert(snapshot != nullptr);
    const RoundNumber round = snapshot->round;
    const CaveId currentCave = snapshot->currentCave;
    const std::size_t mapSignature = playerMapSignature(snapshot->map);

    DebugMapRevealState reveal;
    assert(!reveal.revealed());
    reveal.toggle();
    assert(reveal.revealed());
    reveal.toggle();
    assert(!reveal.revealed());

    snapshot = debugSession.session->displayedSnapshot();
    assert(snapshot != nullptr);
    assert(snapshot->round == round);
    assert(snapshot->currentCave == currentCave);
    assert(playerMapSignature(snapshot->map) == mapSignature);
}

void fixedHiddenEndpointsMatchDebugDestinationCoordinates() {
    constexpr MapSeed mapSeed{1};
    constexpr MatchSeed matchSeed{424242};
    const MatchState authoritative = MapGenerator::generate(mapSeed, matchSeed);
    auto debugSession = LocalGameSessionAdapter::createDebug(mapSeed, matchSeed);
    assert(debugSession.session != nullptr);
    assert(debugSession.mapProvider != nullptr);
    const PlayerFixedMapGeometry* geometry =
        debugSession.session->displayedMapGeometry();
    assert(geometry != nullptr);
    assert(!geometry->unknownExitEndpoints.empty());

    const DebugMapTruth& truth = debugSession.mapProvider->mapTruth();

    for (const auto& [exit, endpoint] : geometry->unknownExitEndpoints) {
        assert(authoritative.world.contains(exit.source));
        const auto& connections =
            authoritative.world.cave(exit.source).connections;
        assert(exit.tunnel > 0 && exit.tunnel <= connections.size());
        const CaveId destination =
            connections[static_cast<std::size_t>(exit.tunnel - 1)];
        assert(truth.cavePositions.at(destination) == endpoint);
    }
}

void gameplayTruthReflectsAuthoritativeState() {
    constexpr MapSeed mapSeed{1};
    constexpr MatchSeed matchSeed{424242};
    const MatchState authoritative = MapGenerator::generate(mapSeed, matchSeed);
    auto debugSession = LocalGameSessionAdapter::createDebug(mapSeed, matchSeed);
    assert(debugSession.session != nullptr);
    assert(debugSession.mapProvider != nullptr);

    const DebugGameplayTruth truth =
        debugSession.mapProvider->gameplayTruth();
    assert(truth.basiliskCave == authoritative.basilisk.cave);
    assert(truth.basiliskAlive == authoritative.basilisk.alive);
    assert(truth.basiliskBehavior == authoritative.basilisk.behavior);
    assert(truth.basiliskLastCave == authoritative.basilisk.lastCave);
    assert(truth.basiliskEncounterCount ==
           authoritative.basilisk.trueEncounters);
    assert(truth.basiliskRoundsSinceMove ==
           authoritative.basilisk.roundsSinceMove);

    std::vector<CaveId> expectedPits;
    for (const PitState& pit : authoritative.pits) {
        expectedPits.push_back(pit.cave);
    }
    std::vector<CaveId> expectedJackals;
    for (const JackalState& jackal : authoritative.jackals) {
        expectedJackals.push_back(jackal.cave);
    }
    assert(truth.pitCaves == expectedPits);
    assert(truth.jackalCaves == expectedJackals);
    assert(truth.territorialSearchTarget ==
           authoritative.mostRecentSearchCave);
}

void gameplayTruthTracksTheRunningSession() {
    auto debugSession = LocalGameSessionAdapter::createDebug(
        MapSeed{1}, MatchSeed{424242});
    assert(debugSession.session != nullptr);
    assert(debugSession.mapProvider != nullptr);
    const PlayerRoundSnapshot* before =
        debugSession.session->displayedSnapshot();
    assert(before != nullptr);
    const auto search = std::find_if(
        before->availableActions.begin(),
        before->availableActions.end(),
        [](const AvailableAction& action) {
            return action.type == ActionType::Search;
        });
    assert(search != before->availableActions.end());
    assert(!debugSession.mapProvider->gameplayTruth()
                .territorialSearchTarget.has_value());
    const CaveId searchedCave = before->currentCave;
    assert(debugSession.session->submitAndLock(*search));
    assert(debugSession.mapProvider->gameplayTruth()
               .territorialSearchTarget == searchedCave);
}

void mapAndGameplayRevealStatesAreIndependent() {
    DebugMapRevealState mapReveal;
    DebugMapRevealState gameplayReveal;
    mapReveal.toggle();
    assert(mapReveal.revealed());
    assert(!gameplayReveal.revealed());
    gameplayReveal.toggle();
    assert(mapReveal.revealed());
    assert(gameplayReveal.revealed());
    mapReveal.toggle();
    assert(!mapReveal.revealed());
    assert(gameplayReveal.revealed());
}

void behaviorControlCyclesLiveStateAndResetsMovementClock() {
    auto debugSession = LocalGameSessionAdapter::createDebug(
        MapSeed{1}, MatchSeed{424242});
    assert(debugSession.session != nullptr);
    assert(debugSession.mapProvider != nullptr);

    const PlayerRoundSnapshot* snapshot =
        debugSession.session->displayedSnapshot();
    assert(snapshot != nullptr);
    const auto search = std::find_if(
        snapshot->availableActions.begin(),
        snapshot->availableActions.end(),
        [](const AvailableAction& action) {
            return action.type == ActionType::Search;
        });
    assert(search != snapshot->availableActions.end());
    assert(debugSession.session->submitAndLock(*search));

    const DebugGameplayTruth baseline =
        debugSession.mapProvider->gameplayTruth();
    assert(baseline.basiliskBehavior == BasiliskBehavior::Normal);
    assert(baseline.basiliskRoundsSinceMove > 0);
    const PlayerRoundSnapshot* afterSearch =
        debugSession.session->displayedSnapshot();
    assert(afterSearch != nullptr);
    const RoundNumber unchangedRound = afterSearch->round;

    constexpr std::array expected{
        BasiliskBehavior::Restless,
        BasiliskBehavior::Lurker,
        BasiliskBehavior::Skittish,
        BasiliskBehavior::Territorial,
        BasiliskBehavior::Enraged,
        BasiliskBehavior::Normal,
    };
    for (const BasiliskBehavior behavior : expected) {
        assert(debugSession.mapProvider->cycleBasiliskBehavior());
        const DebugGameplayTruth truth =
            debugSession.mapProvider->gameplayTruth();
        assert(truth.basiliskBehavior == behavior);
        assert(truth.basiliskRoundsSinceMove == 0);
        assert(truth.basiliskCave == baseline.basiliskCave);
        assert(truth.basiliskAlive == baseline.basiliskAlive);
        assert(truth.basiliskLastCave == baseline.basiliskLastCave);
        assert(truth.basiliskEncounterCount ==
               baseline.basiliskEncounterCount);
        assert(truth.pitCaves == baseline.pitCaves);
        assert(truth.jackalCaves == baseline.jackalCaves);
        assert(truth.territorialSearchTarget ==
               baseline.territorialSearchTarget);
        assert(debugSession.session->displayedSnapshot()->round ==
               unchangedRound);
    }
}

void debugInventoryUsesCapacityAndPublishesNormalActions() {
    auto debugSession = LocalGameSessionAdapter::createDebug(
        MapSeed{1}, MatchSeed{424242});
    assert(debugSession.session != nullptr);
    assert(debugSession.mapProvider != nullptr);

    assert(debugSession.mapProvider->grantItem(ItemType::SurveyFragment));
    const PlayerRoundSnapshot* snapshot =
        debugSession.session->displayedSnapshot();
    assert(snapshot != nullptr);
    assert(std::find(
        snapshot->inventory.items.begin(),
        snapshot->inventory.items.end(),
        ItemType::SurveyFragment) != snapshot->inventory.items.end());
    assert(std::any_of(
        snapshot->availableActions.begin(),
        snapshot->availableActions.end(),
        [](const AvailableAction& action) {
            return action.type == ActionType::UseItem &&
                action.targetItem == ItemType::SurveyFragment &&
                !action.targetCave.has_value() &&
                !action.targetTunnel.has_value();
        }));

    while (snapshot->inventory.items.size() < snapshot->inventory.capacity) {
        assert(debugSession.mapProvider->grantItem(ItemType::HealingDraught));
        snapshot = debugSession.session->displayedSnapshot();
        assert(snapshot != nullptr);
    }
    assert(!debugSession.mapProvider->grantItem(ItemType::BloodBait));
    assert(debugSession.session->displayedSnapshot()->inventory.items.size() ==
           snapshot->inventory.capacity);
}

void debugInventoryMenuCyclesWithoutAffectingBehaviorControl() {
    DebugInventoryMenuState menu;
    assert(!menu.active());
    menu.toggle();
    assert(menu.active());
    assert(menu.selectedItem() == ItemType::HealingDraught);
    menu.moveSelection(-1);
    assert(menu.selectedItem() == ItemType::OldHuntersMap);
    menu.moveSelection(1);
    assert(menu.selectedItem() == ItemType::HealingDraught);
    menu.close();
    assert(!menu.active());
}

} // namespace

int main() {
    revealContainsCompletePhysicalTopology();
    togglingRevealDoesNotMutatePlayerState();
    fixedHiddenEndpointsMatchDebugDestinationCoordinates();
    gameplayTruthReflectsAuthoritativeState();
    gameplayTruthTracksTheRunningSession();
    mapAndGameplayRevealStatesAreIndependent();
    behaviorControlCyclesLiveStateAndResetsMovementClock();
    debugInventoryUsesCapacityAndPublishesNormalActions();
    debugInventoryMenuCyclesWithoutAffectingBehaviorControl();
    return 0;
}
