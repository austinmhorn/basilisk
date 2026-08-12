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
#include "basilisk/systems/SearchSystem.hpp"
#include "basilisk/systems/TurnResolver.hpp"

using namespace basilisk;

namespace {

bool hasEvent(const std::vector<GameEvent>& events, GameEventType type) {
    for (const auto& event : events) {
        if (event.type == type) {
            return true;
        }
    }
    return false;
}

Rules guaranteedSearchRules() {
    Rules rules;
    rules.searchArrowNumerator = 1;
    rules.searchArrowDenominator = 1;
    rules.searchHealingNumerator = 1;
    rules.searchHealingDenominator = 1;
    rules.searchExoticNumerator = 1;
    rules.searchExoticDenominator = 1;
    return rules;
}

void firstSearchCanAwardConfiguredLoot() {
    PlayerState player{1, 7, 100, 4, true};
    const Rules rules = guaranteedSearchRules();
    RandomGenerator rng{12345};

    const auto events = SearchSystem::search(player, rules, rng);

    assert(player.searchedCaves.contains(7));
    assert(player.arrows == 5);
    assert(player.inventory.items.size() == 1);
    assert(player.inventory.contains(ItemType::HealingDraught));
    assert(hasEvent(events, GameEventType::SearchCompleted));
    assert(hasEvent(events, GameEventType::ArrowFound));
    assert(hasEvent(events, GameEventType::ItemFound));
    assert(hasEvent(events, GameEventType::ExoticCallingCardFound));
}

void staticSearchCannotBeFarmed() {
    PlayerState player{1, 7, 100, 4, true};
    const Rules rules = guaranteedSearchRules();
    RandomGenerator rng{12345};

    const auto first = SearchSystem::search(player, rules, rng);
    assert(hasEvent(first, GameEventType::SearchCompleted));

    const int arrowsAfterFirst = player.arrows;
    const auto itemsAfterFirst = player.inventory.items.size();

    const auto second = SearchSystem::search(player, rules, rng);
    assert(hasEvent(second, GameEventType::CaveAlreadySearched));
    assert(!hasEvent(second, GameEventType::ArrowFound));
    assert(!hasEvent(second, GameEventType::ItemFound));
    assert(!hasEvent(second, GameEventType::ExoticCallingCardFound));
    assert(player.arrows == arrowsAfterFirst);
    assert(player.inventory.items.size() == itemsAfterFirst);
}

void arrowsNeverExceedCapacity() {
    PlayerState player{1, 4, 100, 5, true};
    Rules rules;
    rules.searchArrowNumerator = 1;
    rules.searchArrowDenominator = 1;
    RandomGenerator rng{987};

    const auto events = SearchSystem::search(player, rules, rng);

    assert(player.arrows == 5);
    assert(!hasEvent(events, GameEventType::ArrowFound));
}

void fullInventoryRejectsFoundItem() {
    PlayerState player{1, 3, 100, 3, true};
    player.inventory.items = {
        ItemInstance{ItemType::OldMinersMap},
        ItemInstance{ItemType::SurveyFragment},
        ItemInstance{ItemType::JackalRepellent}
    };

    Rules rules;
    rules.searchHealingNumerator = 1;
    rules.searchHealingDenominator = 1;
    RandomGenerator rng{444};

    const auto events = SearchSystem::search(player, rules, rng);

    assert(player.inventory.items.size() == 3);
    assert(hasEvent(events, GameEventType::InventoryFull));
    assert(!hasEvent(events, GameEventType::ItemFound));
}

void healingDraughtRestoresFortyAndIsConsumed() {
    PlayerState player{1, 1, 20, 3, true};
    const bool added = player.inventory.add(ItemInstance{ItemType::HealingDraught}, 3);
    assert(added);

    Rules rules;
    const auto events = ItemSystem::use(player, ItemType::HealingDraught, rules);

    assert(player.health == 60);
    assert(!player.inventory.contains(ItemType::HealingDraught));
    assert(hasEvent(events, GameEventType::ItemUsed));
    assert(hasEvent(events, GameEventType::PlayerHealed));
}

void healingCapsAtMaximumHealth() {
    PlayerState player{1, 1, 80, 3, true};
    const bool added = player.inventory.add(ItemInstance{ItemType::HealingDraught}, 3);
    assert(added);

    Rules rules;
    const auto events = ItemSystem::use(player, ItemType::HealingDraught, rules);

    assert(player.health == 100);
    assert(hasEvent(events, GameEventType::PlayerHealed));
}

void turnResolverUsesSearchSystem() {
    MatchState state;
    state.matchSeed = 101;
    state.mapSeed = 202;
    state.rules = guaranteedSearchRules();
    state.world.addCave(1);
    state.players = {PlayerState{1, 1, 100, 4, true}};

    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt, std::nullopt}
    });

    assert(state.players[0].searchedCaves.contains(1));
    assert(state.players[0].arrows == 5);
    assert(state.players[0].inventory.contains(ItemType::HealingDraught));
    assert(hasEvent(events, GameEventType::ExoticCallingCardFound));
}

void turnResolverUsesHealingItem() {
    MatchState state;
    state.matchSeed = 303;
    state.mapSeed = 404;
    state.world.addCave(1);
    state.players = {PlayerState{1, 1, 20, 3, true}};
    const bool added = state.players[0].inventory.add(
        ItemInstance{ItemType::HealingDraught},
        state.rules.maxInventoryItems);
    assert(added);

    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::UseItem, std::nullopt, ItemType::HealingDraught}
    });

    assert(state.players[0].health == 60);
    assert(!state.players[0].inventory.contains(ItemType::HealingDraught));
    assert(hasEvent(events, GameEventType::PlayerHealed));
}

} // namespace

int main() {
    firstSearchCanAwardConfiguredLoot();
    staticSearchCannotBeFarmed();
    arrowsNeverExceedCapacity();
    fullInventoryRejectsFoundItem();
    healingDraughtRestoresFortyAndIsConsumed();
    healingCapsAtMaximumHealth();
    turnResolverUsesSearchSystem();
    turnResolverUsesHealingItem();

    std::cout << "Search and item tests passed.\n";
    return 0;
}
