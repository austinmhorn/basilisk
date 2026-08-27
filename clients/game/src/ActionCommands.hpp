#pragma once

#include <string>

#include "basilisk/Action.hpp"
#include "basilisk/Clash.hpp"
#include "basilisk/ClientSnapshot.hpp"

namespace basilisk::game {

[[nodiscard]] PlayerAction makePlayerAction(
    const AvailableAction& available,
    PlayerId localPlayer);

// Transport-independent boundary. A future local coordinator or network
// transport can implement these commands without entering ScreenShell.
class ActionCommandSink {
public:
    virtual ~ActionCommandSink() = default;

    [[nodiscard]] virtual bool submitAction(const PlayerAction& action) = 0;
    [[nodiscard]] virtual bool lockAction(PlayerId player) = 0;
    [[nodiscard]] virtual bool submitClashResponse(
        PlayerId,
        ClashId,
        std::string) { return false; }
};

} // namespace basilisk::game
