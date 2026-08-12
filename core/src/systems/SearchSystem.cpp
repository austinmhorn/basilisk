#include "basilisk/systems/SearchSystem.hpp"

#include <algorithm>

namespace basilisk {
namespace {

void tryAddItem(PlayerState& player,
                ItemType item,
                const Rules& rules,
                std::vector<GameEvent>& events) {
    if (player.inventory.add(ItemInstance{item}, rules.maxInventoryItems)) {
        events.push_back(GameEvent{
            GameEventType::ItemFound,
            player.id,
            std::nullopt,
            player.cave,
            1,
            std::nullopt,
            item
        });
    } else {
        events.push_back(GameEvent{
            GameEventType::InventoryFull,
            player.id,
            std::nullopt,
            player.cave,
            0,
            std::nullopt,
            item
        });
    }
}

} // namespace

std::vector<GameEvent> SearchSystem::search(
    PlayerState& player,
    const Rules& rules,
    RandomGenerator& rng) {

    std::vector<GameEvent> events;

    if (player.searchedCaves.contains(player.cave)) {
        events.push_back(GameEvent{
            GameEventType::CaveAlreadySearched,
            player.id,
            std::nullopt,
            player.cave
        });
        return events;
    }

    player.searchedCaves.insert(player.cave);
    events.push_back(GameEvent{
        GameEventType::SearchCompleted,
        player.id,
        std::nullopt,
        player.cave
    });

    if (player.arrows < rules.maxArrows &&
        rng.chance(rules.searchArrowNumerator, rules.searchArrowDenominator)) {
        ++player.arrows;
        events.push_back(GameEvent{
            GameEventType::ArrowFound,
            player.id,
            std::nullopt,
            player.cave,
            1
        });
    }

    if (rng.chance(rules.searchHealingNumerator, rules.searchHealingDenominator)) {
        tryAddItem(player, ItemType::HealingDraught, rules, events);
    }

    if (rng.chance(rules.searchOldMinersMapNumerator,
                   rules.searchOldMinersMapDenominator)) {
        tryAddItem(player, ItemType::OldMinersMap, rules, events);
    }

    if (rng.chance(rules.searchJackalRepellentNumerator,
                   rules.searchJackalRepellentDenominator)) {
        tryAddItem(player, ItemType::JackalRepellent, rules, events);
    }

    if (rng.chance(rules.searchExoticNumerator, rules.searchExoticDenominator)) {
        // BasiliskCore only records the discovery. In online play the backend
        // owns the permanent Calling Card award and account mutation.
        events.push_back(GameEvent{
            GameEventType::ExoticCallingCardFound,
            player.id,
            std::nullopt,
            player.cave
        });
    }

    return events;
}

} // namespace basilisk
