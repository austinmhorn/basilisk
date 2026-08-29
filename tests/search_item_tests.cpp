#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_set>
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
    rules.searchOldHuntersMapWeight = 0;
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

void healingDraughtRestoresFiftyAndIsConsumed() {
    PlayerState player{1, 1, 20, 3, true};
    assert(player.inventory.add(ItemInstance{ItemType::HealingDraught}, 3));
    Rules rules;
    const auto events = ItemSystem::use(player, ItemType::HealingDraught, rules);
    assert(player.health == 70);
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

MatchState surveyWorld(CaveId caveCount = 10) {
    MatchState state;
    state.matchSeed = MatchSeed{77123};
    state.mapSeed = MapSeed{99117};
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FogOfWar;
    for (CaveId cave = 1; cave <= caveCount; ++cave) state.world.addCave(cave);
    for (CaveId cave = 1; cave < caveCount; ++cave) {
        state.world.connect(cave, cave + 1);
    }
    state.basilisk.alive = false;
    state.players = {
        PlayerState{1, 1, 100, 3, true},
        PlayerState{2, caveCount, 100, 3, true},
    };
    assert(state.players[0].inventory.add(ItemInstance{ItemType::SurveyFragment}, 3));
    return state;
}

const AvailableAction* surveyAction(const PlayerRoundSnapshot& snapshot) {
    const auto found = std::find_if(
        snapshot.availableActions.begin(), snapshot.availableActions.end(),
        [](const AvailableAction& action) {
            return action.type == ActionType::UseItem &&
                action.targetItem == ItemType::SurveyFragment;
        });
    return found == snapshot.availableActions.end() ? nullptr : &*found;
}

void surveyFragmentRevealsConnectedUnexploredRegion() {
    MatchState state = surveyWorld();

    auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    assert(std::count_if(
        snapshot.availableActions.begin(), snapshot.availableActions.end(),
        [](const AvailableAction& action) {
            return action.type == ActionType::UseItem &&
                action.targetItem == ItemType::SurveyFragment;
        }) == 1);
    const AvailableAction* available = surveyAction(snapshot);
    assert(available != nullptr);
    assert(!available->targetCave.has_value());
    assert(!available->targetTunnel.has_value());

    RoundController controller;
    PlayerAction action;
    action.player = 1;
    action.type = ActionType::UseItem;
    action.targetItem = ItemType::SurveyFragment;
    const auto events = controller.resolve(state, {action});

    assert(!state.players[0].inventory.contains(ItemType::SurveyFragment));
    const auto& discovery = state.players[0].discovery;
    assert(discovery.surveyedCaves.size() >= 3);
    assert(discovery.surveyedCaves.size() <= 5);
    assert(discovery.knownCaves.size() == discovery.surveyedCaves.size() + 1);
    assert(!state.players[1].discovery.knownCaves.contains(CaveId{2}));

    std::unordered_set<CaveId> eventCaves;
    for (const GameEvent& event : events) {
        if (event.type == GameEventType::CaveDiscovered && event.cave.has_value()) {
            assert(eventCaves.insert(*event.cave).second);
        }
    }
    assert(eventCaves == discovery.surveyedCaves);

    // A linear fixture proves each reveal grew from the preceding known
    // frontier rather than selecting disconnected world caves.
    for (CaveId cave = 1; cave <= discovery.knownCaves.size(); ++cave) {
        assert(discovery.knownCaves.contains(cave));
    }

    snapshot = SnapshotSystem::buildForPlayer(state, 1, events);
    bool foundOpaqueSurveyExit = false;
    for (const DiscoveredCaveView& cave : snapshot.map.caves) {
        if (!discovery.surveyedCaves.contains(cave.cave)) {
            assert(!cave.surveyed);
            continue;
        }
        assert(cave.surveyed);
        assert(cave.exits.size() == state.world.cave(cave.cave).connections.size());
        for (const TunnelView& exit : cave.exits) {
            if (!exit.destination.has_value()) foundOpaqueSurveyExit = true;
        }
    }
    assert(foundOpaqueSurveyExit);
}

void surveyFragmentClampsAndIsDeterministic() {
    MatchState first = surveyWorld(CaveId{3});
    assert(first.players[0].inventory.add(
        ItemInstance{ItemType::SurveyFragment}, 3));
    first.rules.surveyFragmentRevealMin = 5;
    first.rules.surveyFragmentRevealMax = 5;
    MatchState second = first;

    RoundController controller;
    PlayerAction use;
    use.player = PlayerId{1};
    use.type = ActionType::UseItem;
    use.targetItem = ItemType::SurveyFragment;
    const auto firstEvents = controller.resolve(first, {use});
    const auto secondEvents = controller.resolve(second, {use});
    (void)firstEvents;
    (void)secondEvents;

    assert(first.players[0].discovery.surveyedCaves.size() == 2);
    assert(first.players[0].discovery.surveyedCaves ==
           second.players[0].discovery.surveyedCaves);
    assert(std::count_if(
        first.players[0].inventory.items.begin(),
        first.players[0].inventory.items.end(),
        [](const ItemInstance& item) {
            return item.type == ItemType::SurveyFragment;
        }) == 1);
}

void surveyFragmentRandomFrontierGrowthIsDeterministic() {
    MatchState first = surveyWorld(CaveId{8});
    first.world.connect(CaveId{1}, CaveId{3});
    first.world.connect(CaveId{1}, CaveId{4});
    first.rules.surveyFragmentRevealMin = 3;
    first.rules.surveyFragmentRevealMax = 3;
    MatchState second = first;

    PlayerAction use;
    use.player = PlayerId{1};
    use.type = ActionType::UseItem;
    use.targetItem = ItemType::SurveyFragment;
    RoundController controller;
    const auto firstEvents = controller.resolve(first, {use});
    const auto secondEvents = controller.resolve(second, {use});
    (void)firstEvents;
    (void)secondEvents;

    assert(first.players[0].discovery.surveyedCaves.size() == 3);
    assert(first.players[0].discovery.surveyedCaves ==
           second.players[0].discovery.surveyedCaves);
}

void surveyFragmentIsUnavailableWhenNothingCanBeRevealed() {
    MatchState state = surveyWorld(CaveId{3});
    for (const CaveId cave : state.world.caveIds()) {
        state.players[0].discovery.knownCaves.insert(cave);
    }
    const auto before = state.players[0].inventory.items.size();
    const auto snapshot = SnapshotSystem::buildForPlayer(state, 1, {});
    assert(surveyAction(snapshot) == nullptr);

    PlayerAction forged;
    forged.player = PlayerId{1};
    forged.type = ActionType::UseItem;
    forged.targetItem = ItemType::SurveyFragment;
    RoundController controller;
    const auto events = controller.resolve(state, {forged});
    assert(events.empty());
    assert(state.players[0].inventory.items.size() == before);
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
    healingDraughtRestoresFiftyAndIsConsumed();
    oldMinersMapTemporarilyRevealsActivePit();
    surveyFragmentRevealsConnectedUnexploredRegion();
    surveyFragmentClampsAndIsDeterministic();
    surveyFragmentRandomFrontierGrowthIsDeterministic();
    surveyFragmentIsUnavailableWhenNothingCanBeRevealed();
    bloodBaitCanPullBasiliskOneStepCloser();
    turnResolverUsesWeightedSearchSystem();
    std::cout << "Search and item tests passed.\n";
    return 0;
}
