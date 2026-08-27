#include <algorithm>
#include <array>
#include <cassert>
#include <set>
#include <vector>

#include "DebugMapProvider.hpp"
#include "DebugInventoryMenu.hpp"
#include "DebugKillMenu.hpp"
#include "LocalGameSessionAdapter.hpp"
#include "LocalAiGameSessionAdapter.hpp"
#include "LocalSandboxSessionAdapter.hpp"
#include "SandboxPresentation.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/world/MapGenerator.hpp"
#include "basilisk/systems/SigilPlacementSystem.hpp"

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

void localAiSessionUsesSameLiveDebugState() {
    auto local = LocalAiGameSessionAdapter::create(
        MapSeed{1}, MatchSeed{424242}, client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Balanced, client::ai::AiSeed{91});
    assert(local.session != nullptr);
    assert(local.driver != nullptr);
    assert(local.mapProvider != nullptr);

    const MatchState generated = MapGenerator::generate(
        MapSeed{1}, MatchSeed{424242});
    assert(local.mapProvider->mapTruth().cavePositions.size() ==
           generated.world.size());
    assert(local.mapProvider->gameplayTruth().basiliskCave ==
           generated.basilisk.cave);
    const auto authoritativeAi = std::find_if(
        generated.players.begin(), generated.players.end(),
        [](const PlayerState& player) { return player.id != PlayerId{1}; });
    assert(authoritativeAi != generated.players.end());
    const DebugGameplayTruth initialTruth = local.mapProvider->gameplayTruth();
    assert(initialTruth.hunters.size() == 1);
    assert(initialTruth.hunters.front().label == "AI");
    assert(initialTruth.hunters.front().player == authoritativeAi->id);
    assert(initialTruth.hunters.front().cave == authoritativeAi->cave);
    assert(initialTruth.hunters.front().health ==
           local.session->snapshotFor(authoritativeAi->id)->health);
    assert(initialTruth.hunters.front().arrows ==
           local.session->snapshotFor(authoritativeAi->id)->arrows);
    assert(initialTruth.aiDecisionTrace.size() == 4);
    assert(initialTruth.aiDecisionTrace[0].starts_with("AI LAST"));
    assert(initialTruth.aiDecisionTrace[1].starts_with("BASILISK"));
    assert(initialTruth.aiDecisionTrace[2].starts_with("SIGIL"));
    assert(initialTruth.aiDecisionTrace[3].starts_with("TOP"));
    const CaveId initialAiCave = initialTruth.hunters.front().cave;

    const PlayerRoundSnapshot* before = local.session->displayedSnapshot();
    assert(before != nullptr);
    const RoundNumber round = before->round;
    assert(local.mapProvider->grantItem(ItemType::SurveyFragment));
    const PlayerRoundSnapshot* granted = local.session->displayedSnapshot();
    assert(granted != nullptr && granted->round == round);
    assert(std::ranges::find(
        granted->inventory.items, ItemType::SurveyFragment) !=
        granted->inventory.items.end());

    assert(local.mapProvider->cycleBasiliskBehavior());
    assert(local.mapProvider->gameplayTruth().basiliskBehavior ==
           BasiliskBehavior::Restless);

    const auto search = std::ranges::find_if(
        granted->availableActions, [](const AvailableAction& action) {
            return action.type == ActionType::Search;
        });
    assert(search != granted->availableActions.end());
    assert(local.session->submitAndLock(*search));
    local.driver->advance(901);
    assert(local.session->displayedSnapshot()->round == round + 1);
    const DebugGameplayTruth movedTruth = local.mapProvider->gameplayTruth();
    assert(movedTruth.hunters.size() == 1);
    assert(movedTruth.hunters.front().cave != initialAiCave);
}

void debugKillUsesAuthoritativeEliminationAndSigilPlacement() {
    DebugKillMenuState menu;
    assert(!menu.active());
    menu.toggle();
    assert(menu.active() && menu.selectedTarget() == DebugKillTarget::Host);
    menu.moveSelection(1);
    assert(menu.selectedTarget() == DebugKillTarget::Ai);
    menu.close();

    auto killAi = LocalAiGameSessionAdapter::create(
        MapSeed{1}, MatchSeed{424242}, client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Balanced, client::ai::AiSeed{91});
    const PlayerId human = killAi.session->viewContext().localPlayer;
    const auto aiSlot = std::ranges::find_if(killAi.session->matchMetadata().players,
        [&](const PublicPlayerSlot& slot) { return slot.player != human; });
    assert(aiSlot != killAi.session->matchMetadata().players.end());
    assert(killAi.mapProvider->killControlAvailable());
    assert(killAi.mapProvider->killPlayer(DebugKillTarget::Ai));
    const PlayerRoundSnapshot* ai = killAi.session->snapshotFor(aiSlot->player);
    const PlayerRoundSnapshot* host = killAi.session->snapshotFor(human);
    assert(ai != nullptr && !ai->alive && ai->health == 0);
    assert(host != nullptr && host->recoverableRivalSigilAvailable);
    assert(host->matchStatus == MatchStatus::Active);
    assert(!killAi.mapProvider->killPlayer(DebugKillTarget::Ai));

    auto killHost = LocalAiGameSessionAdapter::create(
        MapSeed{2}, MatchSeed{424242}, client::ai::AiDifficulty::Medium,
        client::ai::AiBehavior::Balanced, client::ai::AiSeed{92});
    const PlayerId hostId = killHost.session->viewContext().localPlayer;
    assert(killHost.mapProvider->killPlayer(DebugKillTarget::Host));
    assert(!killHost.session->snapshotFor(hostId)->alive);
    assert(killHost.session->viewContext().mode == client::ClientViewMode::Defeated);
}

void livingAiTruthNeverDisappearsAcrossRounds() {
    auto local = LocalAiGameSessionAdapter::create(
        MapSeed{4}, MatchSeed{424242}, client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Opportunist, client::ai::AiSeed{117});
    assert(local.session != nullptr && local.driver != nullptr &&
           local.mapProvider != nullptr);
    const PlayerId human = local.session->viewContext().localPlayer;
    const auto aiSlot = std::ranges::find_if(
        local.session->matchMetadata().players,
        [&](const PublicPlayerSlot& slot) { return slot.player != human; });
    assert(aiSlot != local.session->matchMetadata().players.end());
    const PlayerId ai = aiSlot->player;

    int verifiedRounds = 0;
    for (; verifiedRounds < 8; ++verifiedRounds) {
        const PlayerRoundSnapshot* aiSnapshot = local.session->snapshotFor(ai);
        assert(aiSnapshot != nullptr);
        const DebugGameplayTruth before = local.mapProvider->gameplayTruth();
        const auto aiMarkers = std::ranges::count_if(before.hunters,
            [&](const DebugGameplayTruth::Hunter& hunter) {
                return hunter.player == ai;
            });
        if (!aiSnapshot->alive) break;
        assert(aiMarkers == 1);
        const auto marker = std::ranges::find_if(before.hunters,
            [&](const DebugGameplayTruth::Hunter& hunter) {
                return hunter.player == ai;
            });
        assert(marker->cave == aiSnapshot->currentCave);

        const PlayerRoundSnapshot* humanSnapshot = local.session->snapshotFor(human);
        assert(humanSnapshot != nullptr && humanSnapshot->alive);
        const RoundNumber priorRound = humanSnapshot->round;
        const auto action = std::ranges::find_if(humanSnapshot->availableActions,
            [](const AvailableAction& candidate) {
                return candidate.type == ActionType::Search;
            });
        assert(action != humanSnapshot->availableActions.end());
        assert(local.session->submitAndLock(*action));
        local.driver->advance(901);
        if (local.session->activeClash().has_value()) {
            assert(local.session->submitClashResponse(
                local.session->activeClash()->challengeWord));
        }
        local.driver->advance(901);

        aiSnapshot = local.session->snapshotFor(ai);
        assert(aiSnapshot != nullptr);
        assert(local.session->snapshotFor(human)->round > priorRound);
        const DebugGameplayTruth after = local.mapProvider->gameplayTruth();
        if (aiSnapshot->alive) {
            const auto afterMarkers = std::ranges::count_if(after.hunters,
                [&](const DebugGameplayTruth::Hunter& hunter) {
                    return hunter.player == ai;
                });
            assert(afterMarkers == 1);
            const auto afterMarker = std::ranges::find_if(after.hunters,
                [&](const DebugGameplayTruth::Hunter& hunter) {
                    return hunter.player == ai;
                });
            assert(afterMarker->cave == aiSnapshot->currentCave);
        }
    }
    assert(verifiedRounds >= 4);
}

void hunterTruthAlwaysReadsCurrentAuthoritativeCave() {
    MatchState state = MapGenerator::generate(MapSeed{2}, MatchSeed{424242});
    assert(state.players.size() >= 2);
    const PlayerId ai = state.players[1].id;
    const std::array labels{DebugHunterLabel{ai, "BASILISK AI"}};
    DebugGameplayTruth truth = buildDebugGameplayTruth(state, labels);
    assert(truth.hunters.size() == 1);
    assert(truth.hunters.front().cave == state.players[1].cave);
    const CaveId destination = state.world.caveIds().back();
    state.players[1].cave = destination;
    truth = buildDebugGameplayTruth(state, labels);
    assert(truth.hunters.front().cave == destination);
}

void sigilTruthTracksMapCarrierAndUnavailableStates() {
    MatchState state = MapGenerator::generate(MapSeed{3}, MatchSeed{424242});
    assert(state.players.size() >= 2);
    const PlayerId owner = state.players[1].id;
    const PlayerId carrier = state.players[0].id;
    const CaveId bodyCave = state.players[1].cave;
    const CaveId ejectedCave = state.world.caveIds().back();
    state.bodies.push_back(
        BodyState{owner, bodyCave, true, ejectedCave});

    DebugGameplayTruth truth = buildDebugGameplayTruth(state);
    assert(truth.sigils.size() == 1);
    assert(truth.sigils.front().state ==
           DebugGameplayTruth::SigilState::OnMap);
    assert(truth.sigils.front().cave == ejectedCave);
    assert(!truth.sigils.front().carrier.has_value());

    state.bodies.front().sigilAvailable = false;
    state.players.front().heldSigilFrom = owner;
    truth = buildDebugGameplayTruth(state);
    assert(truth.sigils.size() == 1);
    assert(truth.sigils.front().state ==
           DebugGameplayTruth::SigilState::Carried);
    assert(truth.sigils.front().carrier == carrier);
    assert(truth.sigils.front().cave == state.players.front().cave);

    state.result.status = MatchStatus::Completed;
    state.result.outcome = MatchOutcome::EscapedWithSigil;
    state.result.winner = carrier;
    truth = buildDebugGameplayTruth(state);
    assert(truth.sigils.empty());

    state.result = MatchResult{};
    state.players.front().heldSigilFrom.reset();
    truth = buildDebugGameplayTruth(state);
    assert(truth.sigils.empty());
}

void sigilTruthReportsAuthoritativeRelocation() {
    MatchState state;
    for (CaveId cave = 1; cave <= 4; ++cave) state.world.addCave(cave);
    state.world.connect(1, 2);
    state.world.connect(1, 3);
    state.world.connect(2, 4);
    state.basilisk.cave = 1;
    state.pits.push_back(PitState{2, true});
    state.players.push_back(PlayerState{PlayerId{1}, CaveId{4}});
    state.players.push_back(PlayerState{PlayerId{2}, CaveId{1}, 0, 3, false});
    std::vector<GameEvent> events;
    placeSigilsForDeath(state, state.players[1], CaveId{1}, events);
    assert(state.bodies.front().sigilCave == CaveId{4});
    const DebugGameplayTruth truth = buildDebugGameplayTruth(state);
    assert(truth.sigils.size() == 1);
    assert(truth.sigils.front().state == DebugGameplayTruth::SigilState::OnMap);
    assert(truth.sigils.front().cave == *state.bodies.front().sigilCave);
}

void sandboxDebugTruthLabelsEveryLivingHunter() {
    auto sandbox = LocalSandboxSessionAdapter::create(4, MapSeed{9004},
        MatchSeed{42000}, client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Balanced, client::ai::AiSeed{77});
    assert(sandbox.session != nullptr && sandbox.driver != nullptr);
    assert(sandbox.mapProvider != nullptr);
    const DebugGameplayTruth truth = sandbox.mapProvider->gameplayTruth();
    assert(truth.hunters.size() == 4);
    assert(truth.hunters.front().label == "HOST");
    for (std::size_t index = 1; index < truth.hunters.size(); ++index) {
        assert(truth.hunters[index].label ==
            "AI " + std::to_string(truth.hunters[index].player));
        const PlayerRoundSnapshot* snapshot =
            sandbox.session->snapshotFor(truth.hunters[index].player);
        assert(snapshot != nullptr);
        assert(snapshot->currentCave == truth.hunters[index].cave);
    }
    const PlayerId firstAi = truth.hunters[1].player;
    assert(sandbox.mapProvider->killPlayer(DebugKillTarget::Ai));
    const DebugGameplayTruth after = sandbox.mapProvider->gameplayTruth();
    assert(std::none_of(after.hunters.begin(), after.hunters.end(),
        [firstAi](const DebugGameplayTruth::Hunter& hunter) {
            return hunter.player == firstAi;
        }));
}

void sandboxDebugMenusTargetEveryParticipantAndSpectatingCycles() {
    auto sandbox = LocalSandboxSessionAdapter::create(5, MapSeed{9005},
        MatchSeed{42001}, client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Random, client::ai::AiSeed{78});
    assert(sandbox.session != nullptr && sandbox.mapProvider != nullptr);
    const auto participants = sandbox.mapProvider->participants();
    assert(participants.size() == 5);
    assert(participants.front().label == "HOST");
    for (std::size_t index = 1; index < participants.size(); ++index)
        assert(participants[index].label == "AI " + std::to_string(index + 1));

    DebugInventoryMenuState inventory;
    inventory.toggle(participants, true);
    assert(inventory.active() && inventory.selectingParticipant());
    for (std::size_t index = 0; index < participants.size(); ++index) {
        if (index != 0) inventory.moveSelection(1);
        assert(inventory.selectedPlayer() == participants[index].player);
    }
    inventory.close();
    for (const auto& participant : participants) {
        assert(sandbox.mapProvider->grantItem(
            participant.player, ItemType::HealingDraught));
        const auto* snapshot = sandbox.session->snapshotFor(participant.player);
        assert(snapshot != nullptr && std::ranges::find(snapshot->inventory.items,
            ItemType::HealingDraught) != snapshot->inventory.items.end());
    }

    const PlayerId host = participants.front().player;
    assert(sandbox.mapProvider->killPlayer(host));
    assert(!sandbox.session->snapshotFor(host)->alive);
    const auto stripAfterDeath = sandboxParticipantPresentation(*sandbox.session);
    assert(stripAfterDeath.size() == participants.size());
    assert(!stripAfterDeath.front().alive && stripAfterDeath.front().local);
    assert(sandbox.session->viewContext().mode == client::ClientViewMode::Defeated);
    assert(sandbox.session->watchRemainingHunter());
    std::set<PlayerId> watched;
    for (std::size_t index = 1; index < participants.size(); ++index) {
        const PlayerId viewed = sandbox.session->viewContext().viewedPlayer;
        watched.insert(viewed);
        assert(sandbox.session->displayedSnapshot() ==
            sandbox.session->snapshotFor(viewed));
        assert(sandbox.session->cycleSpectatedPlayer(1));
    }
    assert(watched.size() == participants.size() - 1);

    const PlayerId killedViewed = sandbox.session->viewContext().viewedPlayer;
    assert(sandbox.mapProvider->killPlayer(killedViewed));
    assert(!sandbox.session->snapshotFor(killedViewed)->alive);
    assert(sandbox.session->viewContext().viewedPlayer != killedViewed);
    assert(sandbox.session->displayedSnapshot()->alive);

    DebugKillMenuState kill;
    kill.toggle(sandbox.mapProvider->participants());
    assert(kill.active() && kill.selectedPlayer() == host);
    assert(!kill.participants().front().alive);
    for (const auto& participant : participants) {
        if (!sandbox.session->snapshotFor(participant.player)->alive) continue;
        assert(sandbox.mapProvider->killPlayer(participant.player));
        assert(!sandbox.session->snapshotFor(participant.player)->alive);
    }
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
    localAiSessionUsesSameLiveDebugState();
    debugKillUsesAuthoritativeEliminationAndSigilPlacement();
    livingAiTruthNeverDisappearsAcrossRounds();
    hunterTruthAlwaysReadsCurrentAuthoritativeCave();
    sigilTruthTracksMapCarrierAndUnavailableStates();
    sigilTruthReportsAuthoritativeRelocation();
    sandboxDebugTruthLabelsEveryLivingHunter();
    sandboxDebugMenusTargetEveryParticipantAndSpectatingCycles();
    return 0;
}
