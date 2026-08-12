#pragma once

#include <optional>

#include "basilisk/Types.hpp"

namespace basilisk {

enum class ExtractionRevealPolicy {
    RevealImmediately,
    DiscoverThroughExploration,
    ProximityOnly,
    Hidden
};

struct ExtractionState {
    bool active{false};
    std::optional<CaveId> cave;
    std::optional<PlayerId> sigilHolder;
    ExtractionRevealPolicy revealPolicy{ExtractionRevealPolicy::RevealImmediately};
};

} // namespace basilisk
