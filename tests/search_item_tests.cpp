#include <cassert>
#include <iostream>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Player.hpp"
#include "basilisk/Random.hpp"
#include "basilisk/Rules.hpp"
#include "basilisk/items/Item.hpp"
#include "basilisk/systems/ItemSystem.hpp"
#include "basilisk/systems/RoundController.hpp"
#include "basilisk/systems/SearchSystem.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/systems/TurnResolver.hpp"

using namespace basilisk;

namespace {

bool hasEvent(const std::vector<GameEvent>& events, GameEventType type) {
    for (const auto& event : events) if (event.type == type) return true;
    return false;
}

Rules guaranteedHealingSearchRules() {
    Rules rules;
    rules.searchNothingWeight = 0;
    rules.searchHealingWeight = 1;
    rules.searchJackalRepellentWeight = 0;
    rules.searchOldMinersMapWeight = 0;
    rules.searchSurveyFragmentWeight = 0;
    rules.searchBloodBaitWeight = 0;
    rules.searchExoticNumerator = 1;
    rules.searchExoticDenominator = 1;
    return rules;
}

void weightedSearchAwardsAtMostOneOrdinaryItem() {
    PlayerState player{1, 7, 100, 4, true};
    const Rules rules = guaranteedHealingSearchRules();
    RandomGenerator rng{12345};
    const auto events = SearchSystem::search(player, rules, rng);

    assert(player.searchedCaves.contains(7));
    assert(player.arrows == 4);
    assert(player.inventory.items.size() == 1);
    assert(player.inventory.contains(ItemType::HealingDraught));
    assert(hasEvent(events, GameEventType::SearchCompleted));
    assert(hasEvent(events, GameEventType::ItemFound));
    assert(hasEvent(events, GameEventType::ExoticCallingCardFound));
}

void staticSearchCannotBeFarmed() {
    PlayerState player{1, 7, 100, 4, true};
    const Rules rules = guaranteedHealingSearchRules();
    RandomGenerator rng{12345};
    static_cast<void>(SearchSystem::search(player, rules, rng));
    const auto itemsAfterFirst = player.inventory.items.size();
    const auto second = SearchSystem::search(player, rules, rng);
    assert(hasEvent(second, GameEventType::CaveAlreadySearched));
    assert(!hasEvent(second, GameEventType::ItemFound));
    assert(!hasEvent(second, GameEventType::ExoticCallingCardFound));
    assert(player.inventory.items.size() == itemsAfterFirst);
}

void fullInventoryRejectsWeightedItem() {
    PlayerState player{1, 3, 100, 3, true};
    player.inventory.items = {
        ItemInstance{ItemType::OldMinersMap},
        ItemInstance{ItemType::SurveyFragment},
        ItemInstance{ItemType::JackalRepellent}
    };
    Rules rules = guaranteedHealingSearchRules();
    rules.searchExoticNumerator = 0;
    RandomGenerator rng{444};
    const auto events = SearchSystem::search(player, rules, rng);
    assert(player.inventory.items.size() == 3);
    assert(hasEvent(events, GameEventType::InventoryFull));
    assert(!hasEvent(events, GameEventType::ItemFound));
}

void healingDraughtRestoresFortyAndIsConsumed() {
    PlayerState player{1, 1, 20, 3, true};
    assert(player.inventory.add(ItemInstance{ItemType::HealingDraught}, 3));
    Rules rules;
    const auto events = ItemSystem::use(player, ItemType::HealingDraught, rules);
    assert(player.health == 60);
    assert(!player.inventory.contains(ItemType::HealingDraught));
    assert(hasEvent(events, GameEventType::PlayerHealed));
}

void oldMinersMapTemporarilyRevealsActivePit() {
    MatchState state;
    state.world.addCave(1); state.world.addCave(2); state.world.connect(1, 2);
    state.players = {PlayerState{1, 1, 100, 3, true}};
    state.pits.push_back(PitState{2, true});
    assert(state.players[0].inventory.add(ItemInstance{ItemType::OldMinersMap}, 3));
    TurnResolver resolver;
    static_cast<void>(resolver.resolve(state, {
        PlayerAction{1, ActionType::UseItem, std::nullopt, ItemType::OldMinersMap}
    }));
    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    assert(snapshot.temporarilyRevealedPitCaves.size() == 1);
    assert(snapshot.temporarilyRevealedPitCaves[0] == CaveId{2});
}

void surveyFragmentRevealsChosenUnknownTunnel() {
    MatchState state;
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FogOfWar;
    state.world.addCave(1); state.world.addCave(2); state.world.addCave(3);
    state.world.connect(1, 2); state.world.connect(1, 3);
    state.players = {PlayerState{1, 1, 100, 3, true}};
    assert(state.players[0].inventory.add(ItemInstance{ItemType::SurveyFragment}, 3));

    auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    const auto it = std::find_if(snapshot.availableActions.begin(), snapshot.availableActions.end(),
        [](const AvailableAction& a) {
            return a.type == ActionType::UseItem && a.targetItem == ItemType::SurveyFragment &&
                   a.targetTunnel.has_value();
        });
    assert(it != snapshot.availableActions.end());

    RoundController controller;
    PlayerAction action;
    action.player = 1;
    action.type = ActionType::UseItem;
    action.targetItem = ItemType::SurveyFragment;
    action.targetTunnel = it->targetTunnel;
    const auto events = controller.resolve(state, {action});

    assert(!state.players[0].inventory.contains(ItemType::SurveyFragment));
    assert(hasEvent(events, GameEventType::TunnelDestinationRevealed));
    snapshot = SnapshotSystem::buildForPlayer(state, 1, events);
    const auto current = std::find_if(snapshot.map.caves.begin(), snapshot.map.caves.end(),
        [](const DiscoveredCaveView& cave) { return cave.cave == 1; });
    assert(current != snapshot.map.caves.end());
    assert(std::any_of(current->exits.begin(), current->exits.end(),
        [](const TunnelView& tunnel) { return tunnel.destination.has_value(); }));
}

void bloodBaitCanPullBasiliskOneStepCloser() {
    MatchState state;
    state.matchSeed = 123;
    state.mapSeed = 456;
    state.rules.bloodBaitAttractionNumerator = 1;
    state.rules.bloodBaitAttractionDenominator = 1;
    state.world.addCave(1); state.world.addCave(2); state.world.addCave(3);
    state.world.connect(1, 2); state.world.connect(2, 3);
    state.basilisk.cave = 1;
    state.players = {PlayerState{1, 3, 100, 3, true}};
    assert(state.players[0].inventory.add(ItemInstance{ItemType::BloodBait}, 3));

    RoundController controller;
    PlayerAction action;
    action.player = 1;
    action.type = ActionType::UseItem;
    action.targetItem = ItemType::BloodBait;
    const auto events = controller.resolve(state, {action});

    assert(state.basilisk.cave == CaveId{2});
    assert(!state.players[0].inventory.contains(ItemType::BloodBait));
    assert(hasEvent(events, GameEventType::BasiliskBaitPlaced));
    assert(hasEvent(events, GameEventType::BasiliskBaitInfluencedMove));
}

void turnResolverUsesWeightedSearchSystem() {
    MatchState state;
    state.matchSeed = 101;
    state.mapSeed = 202;
    state.rules = guaranteedHealingSearchRules();
    state.world.addCave(1);
    state.players = {PlayerState{1, 1, 100, 4, true}};
    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt, std::nullopt}
    });
    assert(state.players[0].searchedCaves.contains(1));
    assert(state.players[0].inventory.items.size() == 1);
    assert(hasEvent(events, GameEventType::ExoticCallingCardFound));
}

} // namespace

int main() {
    weightedSearchAwardsAtMostOneOrdinaryItem();
    staticSearchCannotBeFarmed();
    fullInventoryRejectsWeightedItem();
    healingDraughtRestoresFortyAndIsConsumed();
    oldMinersMapTemporarilyRevealsActivePit();
    surveyFragmentRevealsChosenUnknownTunnel();
    bloodBaitCanPullBasiliskOneStepCloser();
    turnResolverUsesWeightedSearchSystem();
    std::cout << "Search and item tests passed.\n";
    return 0;
}
