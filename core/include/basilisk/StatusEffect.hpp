#pragma once

namespace basilisk {

enum class StatusEffectType {
    Stunned
};

struct StatusEffect {
    StatusEffectType type{StatusEffectType::Stunned};
    int remainingApplications{0};
};

} // namespace basilisk
