#include "DemoMap.hpp"

#include <optional>

namespace basilisk::game::demo {
namespace {

TunnelView knownExit(TunnelId tunnel, CaveId destination) {
    return TunnelView{tunnel, destination};
}

TunnelView unknownExit(TunnelId tunnel, bool strongColdDraft = false) {
    return TunnelView{tunnel, std::nullopt, strongColdDraft};
}

} // namespace

PlayerRoundSnapshot makeDemoMapSnapshot() {
    // This intentionally constructs only the public player-facing DTO. It is
    // not a generated match, simulation, or authoritative world definition.
    PlayerRoundSnapshot snapshot;
    snapshot.player = PlayerId{1};
    snapshot.round = RoundNumber{12};
    snapshot.health = 70;
    snapshot.maxHealth = 100;
    snapshot.arrows = 3;
    snapshot.maxArrows = 5;
    snapshot.alive = true;
    snapshot.inventory.items = {ItemType::HealingDraught, ItemType::SurveyFragment};
    snapshot.inventory.capacity = 3;
    snapshot.hasHunterSigil = true;
    snapshot.extractionCave = CaveId{34};
    snapshot.currentCave = CaveId{7};
    snapshot.map.currentCave = snapshot.currentCave;
    snapshot.map.caves = {
        DiscoveredCaveView{
            CaveId{7},
            {
                knownExit(TunnelId{2}, CaveId{12}),
                unknownExit(TunnelId{6}),
                knownExit(TunnelId{10}, CaveId{16}),
            }},
        DiscoveredCaveView{
            CaveId{12},
            {
                knownExit(TunnelId{1}, CaveId{7}),
                unknownExit(TunnelId{4}, true),
                knownExit(TunnelId{7}, CaveId{21}),
            }},
        DiscoveredCaveView{
            CaveId{16},
            {
                knownExit(TunnelId{1}, CaveId{7}),
                knownExit(TunnelId{8}, CaveId{28}),
                unknownExit(TunnelId{11}),
            }},
        DiscoveredCaveView{
            CaveId{21},
            {
                knownExit(TunnelId{1}, CaveId{12}),
                knownExit(TunnelId{5}, CaveId{28}),
                unknownExit(TunnelId{9}),
            }},
        DiscoveredCaveView{
            CaveId{28},
            {
                knownExit(TunnelId{1}, CaveId{16}),
                knownExit(TunnelId{2}, CaveId{21}),
                knownExit(TunnelId{6}, CaveId{34}),
            }},
        DiscoveredCaveView{
            CaveId{34},
            {
                knownExit(TunnelId{1}, CaveId{28}),
                unknownExit(TunnelId{5}),
            }},
    };

    PlayerObservation basiliskNearby;
    basiliskNearby.type = ObservationType::BasiliskNearby;
    basiliskNearby.viewer = snapshot.player;
    snapshot.observations.push_back(basiliskNearby);

    PlayerObservation pitDraft;
    pitDraft.type = ObservationType::PitInvestigationSucceeded;
    pitDraft.viewer = snapshot.player;
    pitDraft.tunnel = TunnelId{4};
    snapshot.observations.push_back(pitDraft);

    PlayerObservation itemFound;
    itemFound.type = ObservationType::ItemFound;
    itemFound.viewer = snapshot.player;
    itemFound.itemType = ItemType::SurveyFragment;
    snapshot.observations.push_back(itemFound);

    AvailableAction moveKnown;
    moveKnown.type = ActionType::Move;
    moveKnown.targetCave = CaveId{12};
    snapshot.availableActions.push_back(moveKnown);

    AvailableAction moveUnknown;
    moveUnknown.type = ActionType::Move;
    moveUnknown.targetTunnel = TunnelId{6};
    snapshot.availableActions.push_back(moveUnknown);

    snapshot.availableActions.push_back(AvailableAction{ActionType::Search});

    AvailableAction shoot;
    shoot.type = ActionType::Shoot;
    shoot.targetCave = CaveId{16};
    snapshot.availableActions.push_back(shoot);

    AvailableAction useItem;
    useItem.type = ActionType::UseItem;
    useItem.targetItem = ItemType::SurveyFragment;
    snapshot.availableActions.push_back(useItem);
    return snapshot;
}

} // namespace basilisk::game::demo
