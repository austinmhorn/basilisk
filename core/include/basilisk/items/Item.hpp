#pragma once

namespace basilisk {

enum class ItemType {
    HealingDraught,
    OldMinersMap,
    SurveyFragment,
    JackalRepellent,
    BloodBait,
    OldHuntersMap
};

struct ItemInstance {
    ItemType type{};
};

} // namespace basilisk
