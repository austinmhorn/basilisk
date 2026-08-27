#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "ClientSessionController.hpp"
#include "basilisk/Types.hpp"
#include "basilisk/client/ai/AiDecisionEngine.hpp"

#if defined(BASILISK_GAME_DEBUG_BUILD)
#include "DebugMapProvider.hpp"
#endif

namespace basilisk::game {

class LocalSandboxSessionDriver {
public:
    virtual ~LocalSandboxSessionDriver() = default;
    virtual void advance(std::uint64_t elapsedMs) = 0;
};

struct LocalSandboxSession {
    std::unique_ptr<ClientSessionController> session;
    std::unique_ptr<LocalSandboxSessionDriver> driver;
    std::vector<client::ai::AiBehavior> resolvedBehaviors;
#if defined(BASILISK_GAME_DEBUG_BUILD)
    std::unique_ptr<debug::DebugMapProvider> mapProvider;
#endif
};

class LocalSandboxSessionAdapter {
public:
    [[nodiscard]] static LocalSandboxSession create(
        std::size_t hunterCount,
        MapSeed mapSeed,
        MatchSeed matchSeed,
        client::ai::AiDifficulty difficulty,
        client::ai::AiBehavior behavior,
        client::ai::AiSeed aiSeed);
};

} // namespace basilisk::game
