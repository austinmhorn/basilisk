#include "DebugMapProvider.hpp"

#include <utility>

namespace basilisk::game::debug {
DebugMapProvider::DebugMapProvider(
    DebugMapTruth mapTruth,
    GameplayTruthSource gameplayTruthSource)
    : mapTruth_(std::move(mapTruth)),
      gameplayTruthSource_(std::move(gameplayTruthSource)) {}

const DebugMapTruth& DebugMapProvider::mapTruth() const noexcept {
    return mapTruth_;
}

DebugGameplayTruth DebugMapProvider::gameplayTruth() const {
    return gameplayTruthSource_ == nullptr
        ? DebugGameplayTruth{}
        : gameplayTruthSource_();
}

void DebugMapRevealState::toggle() noexcept {
    revealed_ = !revealed_;
}

bool DebugMapRevealState::revealed() const noexcept {
    return revealed_;
}

} // namespace basilisk::game::debug
