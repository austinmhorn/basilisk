#include <algorithm>
#include <cassert>

#include "basilisk/Action.hpp"
#include "basilisk/Observation.hpp"
#include "basilisk/items/Item.hpp"
#include "basilisk/systems/ObservationSystem.hpp"
#include "basilisk/systems/TurnResolver.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

int main() {
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
    return 0;
}
