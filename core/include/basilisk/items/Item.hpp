#pragma once

namespace basilisk {

enum class ItemType {
    HealingDraught,
    OldMinersMap,
    SurveyFragment,
    JackalRepellent,
    BloodBait
};

struct ItemInstance {
    ItemType type{};
};

} // namespace basilisk
