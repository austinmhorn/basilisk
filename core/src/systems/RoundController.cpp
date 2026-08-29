#include "basilisk/systems/RoundController.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

#include "basilisk/Random.hpp"
#include "basilisk/systems/BasiliskBaitSystem.hpp"
#include "basilisk/systems/ItemSystem.hpp"
#include "basilisk/systems/LooseArrowSystem.hpp"
#include "basilisk/systems/MapDiscoverySystem.hpp"
#include "basilisk/systems/PitInvestigationSystem.hpp"
#include "basilisk/systems/SearchSystem.hpp"
#include "basilisk/systems/SigilPlacementSystem.hpp"
#include "basilisk/systems/TurnResolver.hpp"

namespace basilisk {
namespace {

std::uint64_t looseArrowSeed(const MatchState& state) {
    return static_cast<std::uint64_t>(state.matchSeed) ^
        (static_cast<std::uint64_t>(state.mapSeed) << 1U) ^
        (static_cast<std::uint64_t>(state.round) * 0xD6E8FEB86659FD93ULL);
}

std::uint64_t baitSeed(const MatchState& state) {
    return static_cast<std::uint64_t>(state.matchSeed) ^
        (static_cast<std::uint64_t>(state.mapSeed) << 3U) ^
        (static_cast<std::uint64_t>(state.round) * 0xA24BAED4963EE407ULL);
}

void tickTemporaryEffects(MatchState& state) {
    for (auto& player : state.players) {
        if (player.pitMapRevealRounds > 0) --player.pitMapRevealRounds;
        if (player.jackalRepellentRounds > 0) --player.jackalRepellentRounds;
    }
}

PlayerState* findPlayer(MatchState& state, PlayerId id) {
    const auto it = std::find_if(state.players.begin(), state.players.end(),
        [id](const PlayerState& player) { return player.id == id; });
    return it == state.players.end() ? nullptr : &*it;
}

bool isWorldUtility(const PlayerAction& action) {
    return action.type == ActionType::UseItem && action.targetItem.has_value() &&
        (*action.targetItem == ItemType::SurveyFragment ||
         *action.targetItem == ItemType::BloodBait);
}

} // namespace

std::vector<GameEvent> RoundController::resolveStationaryAction(
    MatchState& state, const PlayerAction& action) const {
    auto* player = findPlayer(state, action.player);
    if (!player || !player->alive) return {};
    if (action.type == ActionType::UseItem)
        return ItemSystem::use(state, *player, action);
    if (action.type != ActionType::Search) return {};

    // This phase intentionally precedes the remainder of the round. Its
    // deterministic stream is isolated so resumption cannot reroll the search.
    RandomGenerator rng{
        static_cast<std::uint64_t>(state.matchSeed) ^
        (static_cast<std::uint64_t>(state.round) * 0x9E3779B97F4A7C15ULL) ^
        static_cast<std::uint64_t>(player->id)};
    std::vector<GameEvent> events;
    auto investigationEvents = PitInvestigationSystem::resolve(state, {action});
    events.insert(events.end(), investigationEvents.begin(), investigationEvents.end());
    recoverSigilAtCurrentCave(state, *player, events);
    state.mostRecentSearchCave = player->cave;
    auto searchEvents = SearchSystem::search(*player, state.rules, rng);
    events.insert(events.end(), searchEvents.begin(), searchEvents.end());
    return events;
}

std::vector<GameEvent> RoundController::resolve(
    MatchState& state,
    const std::vector<PlayerAction>& actions) const {

    tickTemporaryEffects(state);

    for (auto& player : state.players) {
        MapDiscoverySystem::initializePlayer(state, player);
    }

    std::unordered_map<PlayerId, CaveId> trackedCaves;
    for (const auto& player : state.players) trackedCaves[player.id] = player.cave;

    std::vector<PlayerAction> prepared = actions;
    for (auto& action : prepared) {
        if (action.type != ActionType::Move && action.type != ActionType::Shoot) continue;
        auto* player = findPlayer(state, action.player);
        if (!player) continue;
        const auto destination = MapDiscoverySystem::resolveMoveDestination(state, *player, action);
        if (!destination.has_value()) {
            action.targetCave.reset();
            action.targetTunnel.reset();
            continue;
        }
        action.targetCave = *destination;
        action.targetTunnel.reset();
    }

    auto investigationEvents = PitInvestigationSystem::resolve(state, prepared);

    // Survey Fragment and Blood Bait need authoritative world state. Resolve
    // them here; TurnResolver's player-only ItemSystem path intentionally
    // treats these two as no-ops, preventing duplicate consumption.
    std::vector<GameEvent> utilityEvents;
    for (const auto& action : prepared) {
        if (!isWorldUtility(action)) continue;
        auto* player = findPlayer(state, action.player);
        if (!player || !player->alive) continue;
        auto itemEvents = ItemSystem::use(state, *player, action);
        utilityEvents.insert(utilityEvents.end(), itemEvents.begin(), itemEvents.end());
    }

    TurnResolver resolver;
    const auto resolvedEvents = resolver.resolve(state, prepared);

    std::vector<GameEvent> events;
    events.reserve(investigationEvents.size() + utilityEvents.size() + resolvedEvents.size() * 2 + 8);
    events.insert(events.end(), investigationEvents.begin(), investigationEvents.end());
    events.insert(events.end(), utilityEvents.begin(), utilityEvents.end());

    for (const auto& event : resolvedEvents) {
        events.push_back(event);

        if (event.type == GameEventType::PlayerMoved && event.actor.has_value() && event.cave.has_value()) {
            auto* player = findPlayer(state, *event.actor);
            if (!player) continue;
            const CaveId from = trackedCaves[*event.actor];
            const CaveId to = *event.cave;
            MapDiscoverySystem::discoverTraversal(*player, from, to, events);
            trackedCaves[*event.actor] = to;
            continue;
        }

        if (event.type == GameEventType::JackalScaredPlayer && event.targetPlayer.has_value() && event.cave.has_value()) {
            auto* player = findPlayer(state, *event.targetPlayer);
            if (!player) continue;
            const CaveId from = trackedCaves[*event.targetPlayer];
            const CaveId to = *event.cave;
            MapDiscoverySystem::discoverTraversal(*player, from, to, events);
            trackedCaves[*event.targetPlayer] = to;
            continue;
        }

        if (event.type == GameEventType::JackalKnockedOutPlayer && event.targetPlayer.has_value() && event.cave.has_value()) {
            auto* player = findPlayer(state, *event.targetPlayer);
            if (!player) continue;
            MapDiscoverySystem::discoverCave(*player, *event.cave, events);
            trackedCaves[*event.targetPlayer] = *event.cave;
        }
    }

    // If normal Basilisk behavior did not already move it this round, active
    // Blood Bait gets one independent chance to pull it a legal step closer.
    RandomGenerator baitRng{baitSeed(state)};
    std::vector<GameEvent> baitEvents;
    BasiliskBaitSystem::resolve(state, baitRng, events, baitEvents);
    events.insert(events.end(), baitEvents.begin(), baitEvents.end());

    LooseArrowSystem::collectForPlayers(state, events);
    RandomGenerator arrowRng{looseArrowSeed(state)};
    LooseArrowSystem::spawnForRound(state, arrowRng, events);

    return events;
}

} // namespace basilisk
