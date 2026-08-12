#include "basilisk/systems/ItemSystem.hpp"

#include <algorithm>

#include "basilisk/systems/MapDiscoverySystem.hpp"

namespace basilisk {
namespace {

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
    if (item != ItemType::SurveyFragment && item != ItemType::BloodBait) {
        return use(player, item, state.rules);
    }

    if (item == ItemType::SurveyFragment) {
        if (!action.targetTunnel.has_value()) return events;
        if (!MapDiscoverySystem::revealTunnelDestination(
                state, player, player.cave, *action.targetTunnel, events)) {
            return events;
        }
        if (!player.inventory.removeOne(item)) return {};
        emitItemUsed(player, item, events);
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
