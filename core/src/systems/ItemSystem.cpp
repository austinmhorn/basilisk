#include "basilisk/systems/ItemSystem.hpp"

#include <algorithm>

namespace basilisk {

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

            events.push_back(GameEvent{
                GameEventType::ItemUsed,
                player.id,
                std::nullopt,
                player.cave,
                1,
                std::nullopt,
                item
            });

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
        case ItemType::SurveyFragment:
        case ItemType::JackalRepellent:
        case ItemType::BloodBait:
            // These items are modeled now so inventory/search content can be
            // data-driven, but their gameplay effects are intentionally
            // deferred until the associated map/AI systems are implemented.
            break;
    }

    return events;
}

} // namespace basilisk
