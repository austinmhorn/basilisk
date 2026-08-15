#pragma once

#include <memory>

#include "ClientSessionController.hpp"
#include "basilisk/Types.hpp"

namespace basilisk::game {

// Trusted local host boundary. Its implementation owns authoritative Core
// state, while callers receive only the ordinary player-safe session API.
class LocalGameSessionAdapter {
public:
    [[nodiscard]] static std::unique_ptr<ClientSessionController> create(
        MapSeed mapSeed,
        MatchSeed matchSeed);
};

} // namespace basilisk::game
