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
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/systems/TurnResolver.hpp"
#include "basilisk/systems/WorldDangerSystem.hpp"

using namespace basilisk;

namespace {

bool hasEvent(const std::vector<GameEvent>& events, GameEventType type) {
    for (const auto& event : events) {
        if (event.type == type) return true;
    }
    return false;
}

Rules guaranteedSearchRules() {
    Rules rules;
    rules.searchArrowNumerator = 1;
    rules.searchArrowDenominator = 1;
    rules.searchHealingNumerator = 1;
    rules.searchHealingDenominator = 1;
    rules.searchOldMinersMapNumerator = 1;
    rules.searchOldMinersMapDenominator = 1;
    rules.searchJackalRepellentNumerator = 1;
    rules.searchJackalRepellentDenominator = 1;
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
    assert(player.inventory.items.size() == 3);
    assert(player.inventory.contains(ItemType::HealingDraught));
    assert(player.inventory.contains(ItemType::OldMinersMap));
    assert(player.inventory.contains(ItemType::JackalRepellent));
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
    rules.searchHealingNumerator = 0;
    rules.searchOldMinersMapNumerator = 0;
    rules.searchJackalRepellentNumerator = 0;
    rules.searchExoticNumerator = 0;
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
    rules.searchOldMinersMapNumerator = 0;
    rules.searchJackalRepellentNumerator = 0;
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
    assert(hasEvent(events, GameEventType::ItemUsed));
    assert(hasEvent(events, GameEventType::PlayerHealed));
}

void healingCapsAtMaximumHealth() {
    PlayerState player{1, 1, 80, 3, true};
    assert(player.inventory.add(ItemInstance{ItemType::HealingDraught}, 3));

    Rules rules;
    const auto events = ItemSystem::use(player, ItemType::HealingDraught, rules);

    assert(player.health == 100);
    assert(hasEvent(events, GameEventType::PlayerHealed));
}

void oldMinersMapTemporarilyRevealsActivePit() {
    MatchState state;
    state.world.addCave(1);
    state.world.addCave(2);
    state.world.connect(1, 2);
    state.players = {PlayerState{1, 1, 100, 3, true}};
    state.pits.push_back(PitState{2, true});
    assert(state.players[0].inventory.add(ItemInstance{ItemType::OldMinersMap}, 3));

    TurnResolver resolver;
    static_cast<void>(resolver.resolve(state, {
        PlayerAction{1, ActionType::UseItem, std::nullopt, ItemType::OldMinersMap}
    }));

    assert(state.players[0].pitMapRevealRounds == state.rules.oldMinersMapRevealRounds);
    assert(!state.players[0].inventory.contains(ItemType::OldMinersMap));
    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    assert(snapshot.temporarilyRevealedPitCaves.size() == 1);
    assert(snapshot.temporarilyRevealedPitCaves[0] == CaveId{2});
}

void jackalRepellentBlocksAttack() {
    MatchState state;
    state.matchSeed = 4444;
    state.world.addCave(1);
    state.players = {PlayerState{1, 1, 100, 3, true}};
    state.jackals.push_back(JackalState{1});
    assert(state.players[0].inventory.add(ItemInstance{ItemType::JackalRepellent}, 3));

    TurnResolver resolver;
    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::UseItem, std::nullopt, ItemType::JackalRepellent}
    });

    assert(state.players[0].jackalRepellentRounds == state.rules.jackalRepellentRounds);
    assert(state.players[0].arrows == 3);
    assert(hasEvent(events, GameEventType::ItemUsed));
    assert(hasEvent(events, GameEventType::JackalRepelled));
    assert(!hasEvent(events, GameEventType::JackalRobbedArrow));
    assert(!hasEvent(events, GameEventType::JackalScaredPlayer));
    assert(!hasEvent(events, GameEventType::JackalKnockedOutPlayer));
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
    assert(state.players[0].inventory.items.size() == 3);
    assert(hasEvent(events, GameEventType::ExoticCallingCardFound));
}

void turnResolverUsesHealingItem() {
    MatchState state;
    state.matchSeed = 303;
    state.mapSeed = 404;
    state.world.addCave(1);
    state.players = {PlayerState{1, 1, 20, 3, true}};
    assert(state.players[0].inventory.add(
        ItemInstance{ItemType::HealingDraught},
        state.rules.maxInventoryItems));

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
    oldMinersMapTemporarilyRevealsActivePit();
    jackalRepellentBlocksAttack();
    turnResolverUsesSearchSystem();
    turnResolverUsesHealingItem();

    std::cout << "Search and item tests passed.\n";
    return 0;
}
