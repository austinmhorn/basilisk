#pragma once

#include <memory>

#include "ClientSessionController.hpp"
#include "basilisk/Types.hpp"

#if defined(BASILISK_GAME_DEBUG_BUILD)
#include "DebugMapProvider.hpp"
#endif

namespace basilisk::game {

// Trusted local host boundary. Its implementation owns authoritative Core
// state, while callers receive only the ordinary player-safe session API.
class LocalGameSessionAdapter {
public:
    [[nodiscard]] static std::unique_ptr<ClientSessionController> create(
        MapSeed mapSeed,
        MatchSeed matchSeed);

#if defined(BASILISK_GAME_DEBUG_BUILD)
    struct DebugSession {
        std::unique_ptr<ClientSessionController> session;
        std::unique_ptr<debug::DebugMapProvider> mapProvider;
    };

    [[nodiscard]] static DebugSession createDebug(
        MapSeed mapSeed,
        MatchSeed matchSeed);
#endif
};

} // namespace basilisk::game
