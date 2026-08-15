#include <algorithm>
#include <cassert>

#include "basilisk/Action.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Observation.hpp"
#include "basilisk/items/Item.hpp"
#include "basilisk/systems/ObservationSystem.hpp"
#include "basilisk/systems/RoundController.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/systems/TurnResolver.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

bool snapshotContainsCave(
    const PlayerRoundSnapshot& snapshot,
    CaveId cave) {

    return std::any_of(
        snapshot.map.caves.begin(),
        snapshot.map.caves.end(),
        [cave](const DiscoveredCaveView& view) {
            return view.cave == cave;
        });
}

void oldHuntersMapReportsOnlyDistance() {
    auto state = MapGenerator::generate(20260813, 313131);
    auto& player = state.players.front();
    assert(player.inventory.add(ItemInstance{ItemType::OldHuntersMap}, state.rules.maxInventoryItems));

    PlayerAction action;
    action.player = player.id;
    action.type = ActionType::UseItem;
    action.targetItem = ItemType::OldHuntersMap;

    TurnResolver resolver;
    const auto events = resolver.resolve(state, {action});
    const auto read = std::find_if(events.begin(), events.end(), [](const GameEvent& event) {
        return event.type == GameEventType::OldHuntersMapRead;
    });
    assert(read != events.end());
    assert(read->actor == player.id);
    assert(read->amount >= 0);
    assert(!player.inventory.contains(ItemType::OldHuntersMap));

    const auto observations = ObservationSystem::buildForPlayer(state, player.id, events);
    const auto clue = std::find_if(observations.begin(), observations.end(), [](const PlayerObservation& observation) {
        return observation.type == ObservationType::OldHuntersMapDistance;
    });
    assert(clue != observations.end());
    assert(clue->amount == read->amount);
}

void seedOnePitInvestigationAndHuntersMapDoNotRevealCaveSeven() {
    auto state = MapGenerator::generate(MapSeed{1}, MatchSeed{424242});
    auto& player = state.players.front();
    player.cave = CaveId{6};
    player.inventory.items.clear();
    player.searchedCaves.clear();
    player.discovery.knownCaves = {CaveId{6}};
    player.discovery.knownConnections.clear();
    player.knownPitTunnels.clear();
    player.pitMapRevealRounds = 0;
    state.jackals.clear();
    state.looseArrows.clear();

    const auto pit = std::find_if(
        state.pits.begin(), state.pits.end(), [](const PitState& candidate) {
            return candidate.active && candidate.cave == CaveId{7};
        });
    assert(pit != state.pits.end());
    const auto& exits = state.world.cave(CaveId{6}).connections;
    const auto pitExit = std::find(exits.begin(), exits.end(), CaveId{7});
    assert(pitExit != exits.end());
    const TunnelId pitTunnel = static_cast<TunnelId>(
        std::distance(exits.begin(), pitExit) + 1);

    state.rules.pitInvestigationNumerator = 1;
    state.rules.pitInvestigationDenominator = 1;
    state.rules.searchNothingWeight = 0;
    state.rules.searchHealingWeight = 0;
    state.rules.searchJackalRepellentWeight = 0;
    state.rules.searchOldMinersMapWeight = 0;
    state.rules.searchSurveyFragmentWeight = 0;
    state.rules.searchBloodBaitWeight = 0;
    state.rules.searchOldHuntersMapWeight = 1;
    state.rules.searchExoticNumerator = 0;
    state.rules.searchExoticDenominator = 1;

    RoundController controller;
    PlayerAction search;
    search.player = player.id;
    search.type = ActionType::Search;
    const std::vector<GameEvent> searchEvents = controller.resolve(state, {search});

    assert(player.knownPitTunnels.at(CaveId{6}) == pitTunnel);
    assert(player.inventory.contains(ItemType::OldHuntersMap));
    assert(!player.discovery.knownCaves.contains(CaveId{7}));
    assert(player.pitMapRevealRounds == 0);

    const PlayerRoundSnapshot afterSearch = SnapshotSystem::buildForPlayer(
        state, player.id, searchEvents);
    assert(!snapshotContainsCave(afterSearch, CaveId{7}));
    assert(afterSearch.temporarilyRevealedPitCaves.empty());
    const auto caveSix = std::find_if(
        afterSearch.map.caves.begin(),
        afterSearch.map.caves.end(),
        [](const DiscoveredCaveView& view) { return view.cave == CaveId{6}; });
    assert(caveSix != afterSearch.map.caves.end());
    const auto warnedExit = std::find_if(
        caveSix->exits.begin(), caveSix->exits.end(),
        [pitTunnel](const TunnelView& exit) { return exit.id == pitTunnel; });
    assert(warnedExit != caveSix->exits.end());
    assert(warnedExit->strongColdDraft);
    assert(!warnedExit->destination.has_value());

    const auto useMap = std::find_if(
        afterSearch.availableActions.begin(),
        afterSearch.availableActions.end(),
        [](const AvailableAction& action) {
            return action.type == ActionType::UseItem &&
                action.targetItem == ItemType::OldHuntersMap;
        });
    assert(useMap != afterSearch.availableActions.end());
    PlayerAction use;
    use.player = player.id;
    use.type = useMap->type;
    use.targetCave = useMap->targetCave;
    use.targetTunnel = useMap->targetTunnel;
    use.targetItem = useMap->targetItem;
    use.contextualAction = useMap->contextualAction;
    const std::vector<GameEvent> useEvents = controller.resolve(state, {use});

    const PlayerRoundSnapshot afterUse = SnapshotSystem::buildForPlayer(
        state, player.id, useEvents);
    const auto distance = std::find_if(
        afterUse.observations.begin(), afterUse.observations.end(),
        [](const PlayerObservation& observation) {
            return observation.type == ObservationType::OldHuntersMapDistance;
        });
    assert(distance != afterUse.observations.end());
    assert(distance->amount >= 0);
    assert(!snapshotContainsCave(afterUse, CaveId{7}));
    assert(afterUse.temporarilyRevealedPitCaves.empty());
    assert(!player.discovery.knownCaves.contains(CaveId{7}));
    assert(player.pitMapRevealRounds == 0);
}

} // namespace

int main() {
    oldHuntersMapReportsOnlyDistance();
    seedOnePitInvestigationAndHuntersMapDoNotRevealCaveSeven();
    return 0;
}
