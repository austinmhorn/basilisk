#pragma once

#include <cstdint>
#include <memory>

#include "ClientSessionController.hpp"
#include "basilisk/Types.hpp"
#include "basilisk/client/ai/RuntimeAiPolicy.hpp"

#if defined(BASILISK_GAME_DEBUG_BUILD)
#include "DebugMapProvider.hpp"
#endif

namespace basilisk::game {

class LocalAiSessionDriver {
public:
    virtual ~LocalAiSessionDriver() = default;
    virtual void advance(std::uint64_t elapsedMs) = 0;
};

struct LocalAiSession {
    std::unique_ptr<ClientSessionController> session;
    std::unique_ptr<LocalAiSessionDriver> driver;
    client::ai::AiBehavior resolvedBehavior{client::ai::AiBehavior::Balanced};
#if defined(BASILISK_GAME_DEBUG_BUILD)
    std::unique_ptr<debug::DebugMapProvider> mapProvider;
#endif
};

class LocalAiGameSessionAdapter {
public:
    [[nodiscard]] static LocalAiSession create(
        MapSeed mapSeed,
        MatchSeed matchSeed,
        client::ai::AiDifficulty difficulty,
        client::ai::AiBehavior behavior,
        client::ai::AiSeed aiSeed,
        client::ai::RuntimeAiPolicyConfig policy = {});
};

} // namespace basilisk::game
