#include "basilisk/systems/ItemSystem.hpp"

#include <algorithm>

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

    if (!player.inventory.contains(item)) {
        return events;
    }

    switch (item) {
        case ItemType::HealingDraught: {
            if (player.health >= rules.maxHealth) {
                return events;
            }

            const int before = player.health;
            player.health = std::min(rules.maxHealth, player.health + rules.healingAmount);
            const int restored = player.health - before;

            if (!player.inventory.removeOne(item)) {
                return events;
            }

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
            // Reserved for later content passes.
            break;
    }

    return events;
}

} // namespace basilisk
