#pragma once

#include <cstddef>
#include <vector>

#include "basilisk/Types.hpp"

namespace basilisk {

enum class PlayerSlot {
    P1,
    P2
};

struct PublicPlayerSlot {
    PlayerId player{};
    PlayerSlot slot{PlayerSlot::P1};
};

// Player-safe match metadata. The cave count is a scalar only; this contract
// intentionally contains no world topology or other authoritative state.
struct PublicMatchMetadata {
    std::size_t totalCaves{0};
    std::vector<PublicPlayerSlot> players;
};

} // namespace basilisk
