#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "ClientSessionController.hpp"
#include "basilisk/Types.hpp"
#include "basilisk/client/SandboxConfiguration.hpp"
#include "basilisk/client/ai/RuntimeAiPolicy.hpp"

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
        const client::SandboxSessionConfig& config,
        client::ai::RuntimeAiPolicyConfig policy = {});
};

} // namespace basilisk::game
