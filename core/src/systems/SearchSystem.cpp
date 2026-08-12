#include "basilisk/systems/SearchSystem.hpp"

#include <array>
#include <cstdint>

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

void resolveWeightedOrdinaryLoot(PlayerState& player,
                                 const Rules& rules,
                                 RandomGenerator& rng,
                                 std::vector<GameEvent>& events) {
    struct WeightedReward {
        std::uint32_t weight;
        std::optional<ItemType> item;
    };

    const std::array<WeightedReward, 6> rewards{{
        {rules.searchNothingWeight, std::nullopt},
        {rules.searchHealingWeight, ItemType::HealingDraught},
        {rules.searchJackalRepellentWeight, ItemType::JackalRepellent},
        {rules.searchOldMinersMapWeight, ItemType::OldMinersMap},
        {rules.searchSurveyFragmentWeight, ItemType::SurveyFragment},
        {rules.searchBloodBaitWeight, ItemType::BloodBait}
    }};

    std::uint64_t total = 0;
    for (const auto& reward : rewards) total += reward.weight;
    if (total == 0) return;

    const auto roll = static_cast<std::uint64_t>(
        rng.range(1, static_cast<int>(total)));
    std::uint64_t cumulative = 0;
    for (const auto& reward : rewards) {
        cumulative += reward.weight;
        if (roll > cumulative) continue;
        if (reward.item.has_value()) {
            tryAddItem(player, *reward.item, rules, events);
        }
        return;
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

    // Exactly one ordinary weighted reward outcome (including Nothing).
    resolveWeightedOrdinaryLoot(player, rules, rng, events);

    if (rng.chance(rules.searchExoticNumerator, rules.searchExoticDenominator)) {
        // Prototype probability/event hook only. In online play the backend
        // owns the permanent Calling Card RNG, weighted rarity pool, duplicate
        // policy, ownership validation, and account mutation.
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
