#include "DebugMapProvider.hpp"

#include <utility>

namespace basilisk::game::debug {
DebugMapProvider::DebugMapProvider(DebugMapTruth truth)
    : truth_(std::move(truth)) {}

const DebugMapTruth& DebugMapProvider::truth() const noexcept {
    return truth_;
}

void DebugMapRevealState::toggle() noexcept {
    revealed_ = !revealed_;
}

bool DebugMapRevealState::revealed() const noexcept {
    return revealed_;
}

} // namespace basilisk::game::debug
