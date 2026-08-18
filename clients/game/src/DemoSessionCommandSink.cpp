#include "DemoSessionCommandSink.hpp"

#include <SDL3/SDL.h>

namespace basilisk::game::demo {

bool DemoSessionCommandSink::quitGame(PlayerId localPlayer) {
    SDL_Log(
        "Demo session command: quit local player %llu",
        static_cast<unsigned long long>(localPlayer));
    return true;
}

} // namespace basilisk::game::demo
