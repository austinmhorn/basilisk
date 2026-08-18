#pragma once

#include "basilisk/MatchState.hpp"
#include "basilisk/PublicMatchMetadata.hpp"

namespace basilisk {

class PublicMatchMetadataSystem {
public:
    [[nodiscard]] static PublicMatchMetadata build(const MatchState& state);
};

} // namespace basilisk
