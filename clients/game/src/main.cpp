#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <string_view>

#include "DemoMap.hpp"
#include "DemoUi.hpp"
#include "MapRenderer.hpp"
#include "MapPresentation.hpp"
#include "ScreenShell.hpp"
#include "TextRenderer.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Random.hpp"

namespace {

struct AppState {
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    std::unique_ptr<basilisk::game::TextRenderer> textRenderer;
    Uint8 backgroundBlue{24};
    bool demoMapEnabled{false};

    // Future gameplay presentation should consume this player-safe view,
    // rather than exposing the authoritative MatchState to the client.
    basilisk::PlayerRoundSnapshot snapshot{};
    basilisk::game::PlayerMapLayout mapLayout;
    basilisk::game::MapPresentationState mapPresentation;
    basilisk::game::MapPresentationGeometry mapGeometry;
    basilisk::game::ScreenShellData demoScreenData;
};

std::string bundledFontDirectory() {
    const char* basePath = SDL_GetBasePath();
    if (basePath == nullptr) return {};
    return std::string{basePath} + "assets/fonts";
}

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
            state->demoScreenData = basilisk::game::demo::makeDemoScreenShellData();
            (void)basilisk::game::selectRouteDestination(
                state->mapPresentation, state->snapshot.map, basilisk::CaveId{34});
            state->demoMapEnabled = true;
            SDL_Log("Development demo map enabled");
            break;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    constexpr SDL_WindowFlags windowFlags =
        SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    if (!SDL_CreateWindowAndRenderer(
            "Basilisk", 1440, 900, windowFlags, &state->window, &state->renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int logicalWidth = 0;
    int logicalHeight = 0;
    int outputWidth = 0;
    int outputHeight = 0;
    if (SDL_GetWindowSize(state->window, &logicalWidth, &logicalHeight) &&
        SDL_GetRenderOutputSize(state->renderer, &outputWidth, &outputHeight)) {
        SDL_Log(
            "Window: logical=%dx%d, render-output=%dx%d, pixel-density=%.2f, "
            "display-scale=%.2f",
            logicalWidth,
            logicalHeight,
            outputWidth,
            outputHeight,
            SDL_GetWindowPixelDensity(state->window),
            SDL_GetWindowDisplayScale(state->window));
    }

    if (!SDL_SetRenderVSync(state->renderer, 1)) {
        SDL_Log("SDL_SetRenderVSync failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->textRenderer = std::make_unique<basilisk::game::TextRenderer>();
    std::string textError;
    if (!state->textRenderer->initialize(
            state->renderer, bundledFontDirectory(), textError)) {
        SDL_Log("Typography initialization failed: %s", textError.c_str());
        return SDL_APP_FAILURE;
    }

    if (!state->textRenderer
             ->measureText("BASILISK", basilisk::game::FontWeight::Bold, 18.0F, textError)
             .has_value()) {
        SDL_Log("Typography measurement failed: %s", textError.c_str());
        return SDL_APP_FAILURE;
    }

    // A linked, deterministic Core call proves this client is not SDL-only.
    basilisk::RandomGenerator coreRandom{0xB451115ULL};
    state->backgroundBlue = static_cast<Uint8>(coreRandom.range(24, 24));

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* state = static_cast<AppState*>(appstate);
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (state != nullptr &&
        (event->type == SDL_EVENT_MOUSE_MOTION ||
         event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
        if (!SDL_ConvertEventToRenderCoordinates(state->renderer, event)) {
            SDL_Log("Unable to convert pointer coordinates: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
        const basilisk::game::PresentationPoint pointer =
            event->type == SDL_EVENT_MOUSE_MOTION
            ? basilisk::game::PresentationPoint{event->motion.x, event->motion.y}
            : basilisk::game::PresentationPoint{event->button.x, event->button.y};
        const basilisk::game::MapHitTarget hit =
            basilisk::game::hitTestPlayerKnownMap(
                state->snapshot.map,
                state->mapLayout,
                state->mapGeometry,
                pointer);
        basilisk::game::updateMapHover(state->mapPresentation, hit);
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event->button.button == SDL_BUTTON_LEFT) {
            (void)basilisk::game::selectRouteFromHit(
                state->mapPresentation, state->snapshot.map, hit);
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* state = static_cast<AppState*>(appstate);

    SDL_SetRenderDrawColor(
        state->renderer, 12, 16, state->backgroundBlue, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(state->renderer);

    int outputWidth = 0;
    int outputHeight = 0;
    if (SDL_GetRenderOutputSize(state->renderer, &outputWidth, &outputHeight)) {
        if (state->demoMapEnabled) {
            std::string screenError;
            if (!basilisk::game::renderScreenShell(
                    state->renderer,
                    *state->textRenderer,
                    state->snapshot,
                    state->mapLayout,
                    state->mapPresentation,
                    state->mapGeometry,
                    state->demoScreenData,
                    outputWidth,
                    outputHeight,
                    screenError)) {
                SDL_Log("Screen shell rendering failed: %s", screenError.c_str());
                return SDL_APP_FAILURE;
            }
        } else {
            state->mapLayout.update(state->snapshot.map);
            constexpr float padding = 24.0F;
            const basilisk::game::PresentationRect bounds{
                padding,
                padding,
                std::max(0.0F, static_cast<float>(outputWidth) - padding * 2.0F),
                std::max(0.0F, static_cast<float>(outputHeight) - padding * 2.0F)};
            const double pixelDensity = std::max(
                1.0, static_cast<double>(SDL_GetWindowPixelDensity(state->window)));
            state->mapGeometry = basilisk::game::buildMapPresentationGeometry(
                state->snapshot.map,
                state->mapLayout,
                state->snapshot.temporarilyRevealedPitCaves,
                bounds,
                padding,
                pixelDensity);
            std::string mapError;
            if (!basilisk::game::renderPlayerKnownMap(
                    state->renderer,
                    *state->textRenderer,
                    state->snapshot,
                    state->mapLayout,
                    state->mapGeometry,
                    state->mapPresentation,
                    mapError)) {
                SDL_Log("Map rendering failed: %s", mapError.c_str());
                return SDL_APP_FAILURE;
            }
        }
    }

    SDL_RenderPresent(state->renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    auto* state = static_cast<AppState*>(appstate);
    if (state != nullptr) {
        state->textRenderer.reset();
        SDL_DestroyRenderer(state->renderer);
        SDL_DestroyWindow(state->window);
        delete state;
    }
    SDL_Quit();
}
