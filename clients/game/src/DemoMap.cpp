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

PlayerRoundSnapshot makeDemoMapSnapshot(DemoSnapshotStage stage) {
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
    snapshot.temporarilyRevealedPitCaves = {CaveId{39}};
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
            }},
        DiscoveredCaveView{
            CaveId{16},
            {
                knownExit(TunnelId{1}, CaveId{7}),
                knownExit(TunnelId{8}, CaveId{28}),
                unknownExit(TunnelId{11}),
                knownExit(TunnelId{12}, CaveId{21}),
            }},
        DiscoveredCaveView{
            CaveId{21},
            {
                knownExit(TunnelId{1}, CaveId{16}),
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

    switch (stage) {
        case DemoSnapshotStage::NormalStart:
            break;
        case DemoSnapshotStage::RecoverableSigil: {
            snapshot.recoverableRivalSigilAvailable = true;
            PlayerObservation observation;
            observation.type = ObservationType::RivalDied;
            observation.viewer = snapshot.player;
            snapshot.observations.push_back(observation);
            break;
        }
        case DemoSnapshotStage::SecuredSigilHiddenExtraction: {
            snapshot.hasHunterSigil = true;
            PlayerObservation observation;
            observation.type = ObservationType::SigilAcquired;
            observation.viewer = snapshot.player;
            snapshot.observations.push_back(observation);
            break;
        }
        case DemoSnapshotStage::SecuredSigilVisibleExtraction: {
            snapshot.hasHunterSigil = true;
            snapshot.extractionCave = CaveId{34};
            PlayerObservation observation;
            observation.type = ObservationType::ExtractionRevealed;
            observation.viewer = snapshot.player;
            observation.cave = snapshot.extractionCave;
            snapshot.observations.push_back(observation);
            break;
        }
        case DemoSnapshotStage::NextRound: {
            snapshot.round = RoundNumber{13};
            snapshot.hasHunterSigil = true;
            snapshot.extractionCave = CaveId{34};
            PlayerObservation nearby;
            nearby.type = ObservationType::BasiliskNearby;
            nearby.viewer = snapshot.player;
            snapshot.observations.push_back(nearby);
            PlayerObservation arrow;
            arrow.type = ObservationType::ArrowFound;
            arrow.viewer = snapshot.player;
            snapshot.observations.push_back(arrow);
            break;
        }
    }

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
    shoot.targetCave = CaveId{12};
    snapshot.availableActions.push_back(shoot);

    AvailableAction useItem;
    useItem.type = ActionType::UseItem;
    useItem.targetItem = ItemType::SurveyFragment;
    snapshot.availableActions.push_back(useItem);

    AvailableAction useHealing;
    useHealing.type = ActionType::UseItem;
    useHealing.targetItem = ItemType::HealingDraught;
    snapshot.availableActions.push_back(useHealing);

    AvailableAction escape;
    escape.type = ActionType::Contextual;
    escape.contextualAction = ContextualActionType::Escape;
    snapshot.availableActions.push_back(escape);
    return snapshot;
}

PlayerRoundSnapshot makeDemoDefeatedSnapshot(bool matchCompleted) {
    PlayerRoundSnapshot snapshot = makeDemoMapSnapshot();
    snapshot.health = 0;
    snapshot.alive = false;
    snapshot.availableActions.clear();
    PlayerObservation death;
    death.type = ObservationType::YouDied;
    death.viewer = snapshot.player;
    snapshot.observations = {death};
    if (matchCompleted) {
        snapshot.matchStatus = MatchStatus::Completed;
        snapshot.matchOutcome = MatchOutcome::Draw;
        snapshot.winner.reset();
    }
    return snapshot;
}

PlayerRoundSnapshot makeDemoSurvivorSnapshot(bool matchCompleted) {
    PlayerRoundSnapshot snapshot = makeDemoMapSnapshot(DemoSnapshotStage::NextRound);
    snapshot.player = PlayerId{2};
    snapshot.health = 55;
    snapshot.arrows = 2;
    snapshot.currentCave = CaveId{21};
    snapshot.map.currentCave = snapshot.currentCave;
    for (PlayerObservation& observation : snapshot.observations) {
        observation.viewer = snapshot.player;
    }
    if (matchCompleted) {
        snapshot.matchStatus = MatchStatus::Completed;
        snapshot.matchOutcome = MatchOutcome::BasiliskKilled;
        snapshot.winner = snapshot.player;
        snapshot.availableActions.clear();
        PlayerObservation victory;
        victory.type = ObservationType::BasiliskKilled;
        victory.viewer = snapshot.player;
        snapshot.observations = {victory};
    }
    return snapshot;
}

} // namespace basilisk::game::demo
