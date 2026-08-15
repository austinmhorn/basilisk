#pragma once

#include "ActionCommands.hpp"

namespace basilisk::game::demo {

// Development-only proof of the command boundary. It records nothing in Core
// and intentionally mutates no gameplay state.
class DemoActionCommandSink final : public ActionCommandSink {
public:
    [[nodiscard]] bool submitAction(const PlayerAction& action) override;
    [[nodiscard]] bool lockAction(PlayerId player) override;
};

} // namespace basilisk::game::demo
