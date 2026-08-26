#include "DebugMapProvider.hpp"

#include <utility>

namespace basilisk::game::debug {
DebugMapProvider::DebugMapProvider(
    DebugMapTruth mapTruth,
    GameplayTruthSource gameplayTruthSource,
    BehaviorControlSource behaviorControlSource,
    ItemGrantSource itemGrantSource)
    : mapTruth_(std::move(mapTruth)),
      gameplayTruthSource_(std::move(gameplayTruthSource)),
      behaviorControlSource_(std::move(behaviorControlSource)),
      itemGrantSource_(std::move(itemGrantSource)) {}

const DebugMapTruth& DebugMapProvider::mapTruth() const noexcept {
    return mapTruth_;
}

DebugGameplayTruth DebugMapProvider::gameplayTruth() const {
    return gameplayTruthSource_ == nullptr
        ? DebugGameplayTruth{}
        : gameplayTruthSource_();
}

bool DebugMapProvider::cycleBasiliskBehavior() {
    if (behaviorControlSource_ == nullptr) return false;
    BasiliskBehavior next = BasiliskBehavior::Normal;
    switch (gameplayTruth().basiliskBehavior) {
        case BasiliskBehavior::Normal: next = BasiliskBehavior::Restless; break;
        case BasiliskBehavior::Restless: next = BasiliskBehavior::Lurker; break;
        case BasiliskBehavior::Lurker: next = BasiliskBehavior::Skittish; break;
        case BasiliskBehavior::Skittish: next = BasiliskBehavior::Territorial; break;
        case BasiliskBehavior::Territorial: next = BasiliskBehavior::Enraged; break;
        case BasiliskBehavior::Enraged: next = BasiliskBehavior::Normal; break;
    }
    return behaviorControlSource_(next);
}

bool DebugMapProvider::grantItem(ItemType item) {
    return itemGrantSource_ != nullptr && itemGrantSource_(item);
}

void DebugMapRevealState::toggle() noexcept {
    revealed_ = !revealed_;
}

bool DebugMapRevealState::revealed() const noexcept {
    return revealed_;
}

} // namespace basilisk::game::debug
