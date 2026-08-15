#include "DemoActionCommandSink.hpp"

#include <SDL3/SDL.h>

#include "ActionPresentation.hpp"

namespace basilisk::game::demo {

bool DemoActionCommandSink::submitAction(const PlayerAction& action) {
    AvailableAction available;
    available.type = action.type;
    available.targetCave = action.targetCave;
    available.targetTunnel = action.targetTunnel;
    available.targetItem = action.targetItem;
    available.contextualAction = action.contextualAction;
    const PresentedAction presented = presentAvailableAction(available);
    SDL_Log(
        "Demo command: submit player %llu action '%s'",
        static_cast<unsigned long long>(action.player),
        presented.title.c_str());
    return true;
}

bool DemoActionCommandSink::lockAction(PlayerId player) {
    SDL_Log(
        "Demo command: lock player %llu",
        static_cast<unsigned long long>(player));
    return true;
}

} // namespace basilisk::game::demo
