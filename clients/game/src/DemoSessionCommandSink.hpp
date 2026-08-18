#pragma once

#include "ClientLifecycle.hpp"

namespace basilisk::game::demo {

class DemoSessionCommandSink final : public ClientSessionCommandSink {
public:
    [[nodiscard]] bool quitGame(PlayerId localPlayer) override;
};

} // namespace basilisk::game::demo
