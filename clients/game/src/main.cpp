#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <new>
#include <string_view>

#include "DemoMap.hpp"
#include "MapRenderer.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Random.hpp"

namespace {

struct AppState {
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    Uint8 backgroundBlue{24};

    // Future gameplay presentation should consume this player-safe view,
    // rather than exposing the authoritative MatchState to the client.
    basilisk::PlayerRoundSnapshot snapshot{};
    basilisk::game::PlayerMapLayout mapLayout;
};

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    auto* state = new (std::nothrow) AppState{};
    if (state == nullptr) {
        SDL_Log("Unable to allocate application state");
        return SDL_APP_FAILURE;
    }
    *appstate = state;

    for (int index = 1; index < argc; ++index) {
        if (argv != nullptr && argv[index] != nullptr &&
            std::string_view{argv[index]} == "--demo-map") {
            state->snapshot = basilisk::game::demo::makeDemoMapSnapshot();
            SDL_Log("Development demo map enabled");
            break;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer(
            "Basilisk", 960, 540, 0, &state->window, &state->renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_SetRenderVSync(state->renderer, 1)) {
        SDL_Log("SDL_SetRenderVSync failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // A linked, deterministic Core call proves this client is not SDL-only.
    basilisk::RandomGenerator coreRandom{0xB451115ULL};
    state->backgroundBlue = static_cast<Uint8>(coreRandom.range(24, 24));

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void*, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* state = static_cast<AppState*>(appstate);

    state->mapLayout.update(state->snapshot.map);

    SDL_SetRenderDrawColor(
        state->renderer, 12, 16, state->backgroundBlue, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(state->renderer);

    int outputWidth = 0;
    int outputHeight = 0;
    if (SDL_GetRenderOutputSize(state->renderer, &outputWidth, &outputHeight)) {
        constexpr float padding = 24.0F;
        const basilisk::game::MapViewport viewport{
            SDL_FRect{
                padding,
                padding,
                std::max(0.0F, static_cast<float>(outputWidth) - padding * 2.0F),
                std::max(0.0F, static_cast<float>(outputHeight) - padding * 2.0F)},
            basilisk::game::LogicalPoint{},
            42.0F};
        basilisk::game::renderPlayerKnownMap(
            state->renderer,
            state->snapshot.map,
            state->mapLayout,
            state->snapshot.currentCave,
            viewport);
    }

    SDL_RenderPresent(state->renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    auto* state = static_cast<AppState*>(appstate);
    if (state != nullptr) {
        SDL_DestroyRenderer(state->renderer);
        SDL_DestroyWindow(state->window);
        delete state;
    }
    SDL_Quit();
}
