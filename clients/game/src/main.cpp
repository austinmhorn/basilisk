#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>

#include "ActionSelection.hpp"
#include "DemoActionCommandSink.hpp"
#include "DemoMap.hpp"
#include "DemoUi.hpp"
#include "MapRenderer.hpp"
#include "MapActionMenu.hpp"
#include "MapPresentation.hpp"
#include "ScreenShell.hpp"
#include "SvgTextureManager.hpp"
#include "TextRenderer.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Random.hpp"

namespace {

struct AppState {
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    std::unique_ptr<basilisk::game::TextRenderer> textRenderer;
    std::unique_ptr<basilisk::game::SvgTextureManager> svgTextures;
    Uint8 backgroundBlue{24};
    bool demoMapEnabled{false};
    std::size_t demoSnapshotStage{0};

    // Future gameplay presentation should consume this player-safe view,
    // rather than exposing the authoritative MatchState to the client.
    basilisk::PlayerRoundSnapshot snapshot{};
    basilisk::game::PlayerMapLayout mapLayout;
    basilisk::game::MapPresentationState mapPresentation;
    basilisk::game::MapPresentationGeometry mapGeometry;
    basilisk::game::ActionSelectionState actionSelection;
    basilisk::game::ActionPanelGeometry actionGeometry;
    basilisk::game::MapActionMenuState mapActionMenu;
    basilisk::game::MapActionMenuGeometry mapActionMenuGeometry;
    std::optional<basilisk::RoundNumber> mapActionMenuRound;
    std::unique_ptr<basilisk::game::ActionCommandSink> actionCommands;
    basilisk::game::ScreenShellData demoScreenData;
};

std::string bundledFontDirectory() {
    const char* basePath = SDL_GetBasePath();
    if (basePath == nullptr) return {};
    return std::string{basePath} + "assets/fonts";
}

std::string bundledAssetDirectory() {
    const char* basePath = SDL_GetBasePath();
    if (basePath == nullptr) return {};
    return std::string{basePath} + "assets";
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
            state->actionCommands =
                std::make_unique<basilisk::game::demo::DemoActionCommandSink>();
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

    state->svgTextures = std::make_unique<basilisk::game::SvgTextureManager>();
    std::string assetError;
    if (!state->svgTextures->initialize(
            state->renderer, bundledAssetDirectory(), assetError)) {
        SDL_Log("SVG asset initialization failed: %s", assetError.c_str());
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

    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !event->key.repeat && event->key.key == SDLK_ESCAPE) {
        state->mapActionMenu.dismiss();
        return SDL_APP_CONTINUE;
    }

    if (state != nullptr && state->demoMapEnabled &&
        event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
        event->key.key == SDLK_F6) {
        constexpr std::array stages{
            basilisk::game::demo::DemoSnapshotStage::NormalStart,
            basilisk::game::demo::DemoSnapshotStage::RecoverableSigil,
            basilisk::game::demo::DemoSnapshotStage::SecuredSigilHiddenExtraction,
            basilisk::game::demo::DemoSnapshotStage::SecuredSigilVisibleExtraction,
            basilisk::game::demo::DemoSnapshotStage::NextRound,
        };
        state->demoSnapshotStage = (state->demoSnapshotStage + 1) % stages.size();
        state->snapshot = basilisk::game::demo::makeDemoMapSnapshot(
            stages[state->demoSnapshotStage]);
        SDL_Log(
            "Development snapshot stage %zu/%zu, round %u",
            state->demoSnapshotStage + 1,
            stages.size(),
            static_cast<unsigned int>(state->snapshot.round));
        return SDL_APP_CONTINUE;
    }

    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !event->key.repeat && event->key.key >= SDLK_1 && event->key.key <= SDLK_9) {
        const std::size_t index = static_cast<std::size_t>(event->key.key - SDLK_1);
        if (state->actionSelection.select(
                index,
                state->snapshot.availableActions,
                state->demoScreenData.viewContext)) {
            state->mapActionMenu.dismiss();
            state->actionSelection.ensureVisible(
                index, state->actionGeometry.visibleCapacity);
        }
        return SDL_APP_CONTINUE;
    }

    if (state != nullptr &&
        (event->type == SDL_EVENT_MOUSE_MOTION ||
         event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event->type == SDL_EVENT_MOUSE_WHEEL)) {
        if (!SDL_ConvertEventToRenderCoordinates(state->renderer, event)) {
            SDL_Log("Unable to convert pointer coordinates: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
        basilisk::game::PresentationPoint pointer;
        if (event->type == SDL_EVENT_MOUSE_MOTION) {
            pointer = {event->motion.x, event->motion.y};
        } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
            pointer = {event->wheel.mouse_x, event->wheel.mouse_y};
            if (basilisk::game::hitTestActionPanel(state->actionGeometry, pointer)) {
                const int delta = event->wheel.y > 0.0F
                    ? -1
                    : event->wheel.y < 0.0F ? 1 : 0;
                state->actionSelection.scrollRows(
                    delta,
                    state->snapshot.availableActions.size(),
                    state->actionGeometry.visibleCapacity);
            }
            return SDL_APP_CONTINUE;
        } else {
            pointer = {event->button.x, event->button.y};
        }

        if (state->mapActionMenu.isOpen()) {
            const auto menuChoice = basilisk::game::hitTestMapActionRow(
                state->mapActionMenuGeometry, pointer);
            state->mapActionMenu.setHoveredChoice(menuChoice);
            if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event->button.button == SDL_BUTTON_LEFT) {
                if (menuChoice.has_value()) {
                    if (menuChoice->kind ==
                        basilisk::game::MapActionMenuChoiceKind::GameplayAction) {
                        if (state->mapActionMenu.chooseGameplayAction(
                                *menuChoice,
                                state->snapshot.availableActions,
                                state->demoScreenData.viewContext,
                                state->actionSelection)) {
                            state->actionSelection.ensureVisible(
                                menuChoice->actionIndex,
                                state->actionGeometry.visibleCapacity);
                        }
                    } else if (state->mapActionMenu.target().has_value() &&
                               state->mapActionMenu.target()->kind ==
                                   basilisk::game::SpatialActionTargetKind::Cave) {
                        const basilisk::game::DestinationControl control =
                            menuChoice->kind == basilisk::game::MapActionMenuChoiceKind::MarkDestination
                            ? basilisk::game::DestinationControl::Mark
                            : basilisk::game::DestinationControl::Clear;
                        (void)basilisk::game::applyDestinationControl(
                            state->mapPresentation,
                            state->snapshot.map,
                            state->mapActionMenu.target()->cave,
                            control);
                        state->mapActionMenu.dismiss();
                    }
                    return SDL_APP_CONTINUE;
                }
                if (basilisk::game::hitTestMapActionMenu(
                        state->mapActionMenuGeometry, pointer)) {
                    return SDL_APP_CONTINUE;
                }
                state->mapActionMenu.dismiss();
            } else if (basilisk::game::hitTestMapActionMenu(
                           state->mapActionMenuGeometry, pointer)) {
                return SDL_APP_CONTINUE;
            }
        }

        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event->button.button == SDL_BUTTON_LEFT) {
            if (const auto actionIndex = basilisk::game::hitTestActionRow(
                    state->actionGeometry, pointer); actionIndex.has_value()) {
                (void)state->actionSelection.select(
                    *actionIndex,
                    state->snapshot.availableActions,
                    state->demoScreenData.viewContext);
                state->mapActionMenu.dismiss();
                return SDL_APP_CONTINUE;
            }
            if (basilisk::game::hitTestActionLockButton(
                    state->actionGeometry, pointer)) {
                if (state->actionCommands != nullptr &&
                    state->actionSelection.canLock(
                        state->demoScreenData.viewContext) &&
                    !state->actionSelection.submitAndLock(
                        state->demoScreenData.viewContext,
                        *state->actionCommands)) {
                    SDL_Log("Action submit/lock command failed");
                }
                return SDL_APP_CONTINUE;
            }
        }

        const basilisk::game::MapHitTarget hit =
            basilisk::game::hitTestPlayerKnownMap(
                state->snapshot.map,
                state->mapLayout,
                state->mapGeometry,
                pointer);
        basilisk::game::updateMapHover(state->mapPresentation, hit);
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event->button.button == SDL_BUTTON_LEFT) {
            if (hit.kind == basilisk::game::MapHitKind::DiscoveredCave &&
                       hit.cave.has_value()) {
                const basilisk::game::SpatialActionTarget target =
                    basilisk::game::caveActionTarget(*hit.cave);
                const bool hasMatchingLegalAction =
                    !basilisk::game::matchingSpatialActionIndices(
                        state->snapshot.availableActions, target).empty();
                const basilisk::game::DestinationControl destinationControl =
                    basilisk::game::destinationControlForCave(
                        state->snapshot.map,
                        *hit.cave,
                        state->mapPresentation.routeDestination,
                        hasMatchingLegalAction);
                (void)state->mapActionMenu.open(
                    target,
                    pointer.x,
                    pointer.y,
                    state->snapshot.availableActions,
                    state->demoScreenData.viewContext,
                    destinationControl,
                    !state->actionSelection.locked());
            } else if (hit.kind == basilisk::game::MapHitKind::UnknownExit &&
                       hit.unknownExit.has_value()) {
                (void)state->mapActionMenu.open(
                    basilisk::game::unknownExitActionTarget(
                        hit.unknownExit->tunnel),
                    pointer.x,
                    pointer.y,
                    state->snapshot.availableActions,
                    state->demoScreenData.viewContext,
                    basilisk::game::DestinationControl::None,
                    !state->actionSelection.locked());
            } else {
                state->mapActionMenu.dismiss();
            }
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
            state->actionSelection.synchronize(
                state->snapshot.round,
                state->snapshot.availableActions.size(),
                state->demoScreenData.viewContext);
            if (!state->mapActionMenuRound.has_value() ||
                *state->mapActionMenuRound != state->snapshot.round) {
                state->mapActionMenu.dismiss();
                state->mapActionMenuRound = state->snapshot.round;
            }
            std::string screenError;
            if (!basilisk::game::renderScreenShell(
                    state->renderer,
                    *state->textRenderer,
                    *state->svgTextures,
                    state->snapshot,
                    state->mapLayout,
                    state->mapPresentation,
                    state->mapGeometry,
                    state->actionSelection,
                    state->actionGeometry,
                    state->mapActionMenu,
                    state->mapActionMenuGeometry,
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
        state->svgTextures.reset();
        state->textRenderer.reset();
        SDL_DestroyRenderer(state->renderer);
        SDL_DestroyWindow(state->window);
        delete state;
    }
    SDL_Quit();
}
