#include "basilisk/systems/ItemSystem.hpp"

#include <algorithm>
#include <optional>
#include <queue>
#include <unordered_set>

#include "basilisk/systems/MapDiscoverySystem.hpp"

namespace basilisk {
namespace {

constexpr std::uint64_t kSurveySeedSalt = 0xC6BC279692B5CC83ULL;

std::uint64_t surveySeed(const MatchState& state, PlayerId player) {
    return static_cast<std::uint64_t>(state.matchSeed) ^
        (static_cast<std::uint64_t>(state.mapSeed) << 1U) ^
        (static_cast<std::uint64_t>(state.round) * kSurveySeedSalt) ^
        (static_cast<std::uint64_t>(player) * 0x9E3779B97F4A7C15ULL);
}

void emitItemUsed(PlayerState& player,
                  ItemType item,
                  std::vector<GameEvent>& events) {
    events.push_back(GameEvent{
        GameEventType::ItemUsed,
        player.id,
        std::nullopt,
        player.cave,
        1,
        std::nullopt,
        item
    });
}

std::optional<int> distanceBetween(const WorldGraph& world, CaveId start, CaveId target) {
    if (!world.contains(start) || !world.contains(target)) return std::nullopt;
    if (start == target) return 0;

    std::queue<std::pair<CaveId, int>> frontier;
    std::unordered_set<CaveId> seen;
    frontier.push({start, 0});
    seen.insert(start);

    while (!frontier.empty()) {
        const auto [current, distance] = frontier.front();
        frontier.pop();
        for (const CaveId next : world.cave(current).connections) {
            if (!seen.insert(next).second) continue;
            if (next == target) return distance + 1;
            frontier.push({next, distance + 1});
        }
    }
    return std::nullopt;
}

} // namespace

std::vector<GameEvent> ItemSystem::use(
    PlayerState& player,
    ItemType item,
    const Rules& rules) {

    std::vector<GameEvent> events;
    if (!player.inventory.contains(item)) return events;

    switch (item) {
        case ItemType::HealingDraught: {
            if (player.health >= rules.maxHealth) return events;
            const int before = player.health;
            player.health = std::min(rules.maxHealth, player.health + rules.healingAmount);
            const int restored = player.health - before;
            if (!player.inventory.removeOne(item)) return events;
            emitItemUsed(player, item, events);
            events.push_back(GameEvent{
                GameEventType::PlayerHealed,
                player.id,
                player.id,
                player.cave,
                restored,
                std::nullopt,
                item
            });
            break;
        }

        case ItemType::OldMinersMap:
            if (!player.inventory.removeOne(item)) return events;
            player.pitMapRevealRounds = std::max(
                player.pitMapRevealRounds,
                rules.oldMinersMapRevealRounds);
            emitItemUsed(player, item, events);
            break;

        case ItemType::JackalRepellent:
            if (!player.inventory.removeOne(item)) return events;
            player.jackalRepellentRounds = std::max(
                player.jackalRepellentRounds,
                rules.jackalRepellentRounds);
            emitItemUsed(player, item, events);
            break;

        case ItemType::SurveyFragment:
        case ItemType::BloodBait:
        case ItemType::OldHuntersMap:
            // World-aware overload handles these.
            break;
    }

    return events;
}

std::vector<GameEvent> ItemSystem::use(
    MatchState& state,
    PlayerState& player,
    const PlayerAction& action) {

    std::vector<GameEvent> events;
    if (!action.targetItem.has_value() ||
        !player.inventory.contains(*action.targetItem)) {
        return events;
    }

    const ItemType item = *action.targetItem;
    if (item != ItemType::SurveyFragment &&
        item != ItemType::BloodBait &&
        item != ItemType::OldHuntersMap) {
        return use(player, item, state.rules);
    }

    if (item == ItemType::SurveyFragment) {
        if (action.targetCave.has_value() || action.targetTunnel.has_value() ||
            state.rules.surveyFragmentRevealMin <= 0 ||
            state.rules.surveyFragmentRevealMax <
                state.rules.surveyFragmentRevealMin ||
            !MapDiscoverySystem::hasSurveyFrontier(state, player)) return events;
        RandomGenerator random{surveySeed(state, player.id)};
        const int requested = random.range(
            state.rules.surveyFragmentRevealMin,
            state.rules.surveyFragmentRevealMax);
        if (MapDiscoverySystem::surveyFrontier(
                state, player, static_cast<std::size_t>(requested), random,
                events) == 0) return {};
        if (!player.inventory.removeOne(item)) return {};
        emitItemUsed(player, item, events);
        return events;
    }

    if (item == ItemType::OldHuntersMap) {
        if (!state.basilisk.alive) return events;
        const auto distance = distanceBetween(state.world, player.cave, state.basilisk.cave);
        if (!distance.has_value()) return events;
        if (!player.inventory.removeOne(item)) return events;
        events.push_back(GameEvent{
            GameEventType::OldHuntersMapRead,
            player.id,
            std::nullopt,
            player.cave,
            *distance,
            std::nullopt,
            item
        });
        return events;
    }

    if (!player.inventory.removeOne(item)) return events;
    state.basiliskBaitCave = player.cave;
    state.basiliskBaitRounds = state.rules.bloodBaitRounds;
    emitItemUsed(player, item, events);
    events.push_back(GameEvent{
        GameEventType::BasiliskBaitPlaced,
        player.id,
        std::nullopt,
        player.cave,
        state.rules.bloodBaitRounds,
        std::nullopt,
        item
    });
    return events;
}

} // namespace basilisk
