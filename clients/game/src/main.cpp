#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ActionSelection.hpp"
#include "AuthScreen.hpp"
#include "AuthScreenRenderer.hpp"
#include "ClientLifecycle.hpp"
#include "ClientSessionController.hpp"
#include "ConnectionStatusPresentation.hpp"
#include "DemoMap.hpp"
#include "DemoUi.hpp"
#include "LocalGameSessionAdapter.hpp"
#include "MapRenderer.hpp"
#include "MainMenu.hpp"
#include "MainMenuRenderer.hpp"
#include "MapActionMenu.hpp"
#include "MapPresentation.hpp"
#include "ScreenShell.hpp"
#include "SessionTokenStorage.hpp"
#include "SvgTextureManager.hpp"
#include "TextRenderer.hpp"
#include "WebSocketNetworkSession.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Random.hpp"

#if defined(BASILISK_GAME_DEBUG)
#include "DebugMapProvider.hpp"
#endif

namespace {

enum class AppView {
    Authentication,
    MainMenu,
    Gameplay,
};

struct AppState {
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    std::unique_ptr<basilisk::game::TextRenderer> textRenderer;
    std::unique_ptr<basilisk::game::SvgTextureManager> svgTextures;
    Uint8 backgroundBlue{24};
    bool demoMapEnabled{false};
    bool screenShellEnabled{false};
    bool autoLockSelectedActions{false};
    std::size_t demoSnapshotStage{0};
    AppView view{AppView::MainMenu};
    basilisk::game::AuthScreenState authScreen;
    basilisk::game::AuthScreenGeometry authGeometry;
    bool authResponseHandled{false};
    bool storedSessionAttempted{false};
    bool restoringSession{false};
    std::optional<basilisk::game::PublicAccountProfile> authenticatedProfile;
    std::optional<std::string> authenticatedSessionToken;
    basilisk::game::MainMenuState mainMenu;
    basilisk::game::MainMenuGeometry mainMenuGeometry;
    std::size_t handledLobbyResponseRevision{0};
    bool cancelLobbyWhenHosted{false};

    std::unique_ptr<basilisk::game::ClientSessionController> ownedSession;
    basilisk::game::ClientSessionController* session{nullptr};
    std::unique_ptr<basilisk::game::WebSocketNetworkSession> networkSession;
    bool networkFailureLogged{false};
    basilisk::game::PlayerMapLayout mapLayout;
    basilisk::game::MapPresentationState mapPresentation;
    basilisk::game::MapPresentationGeometry mapGeometry;
    basilisk::game::ActionSelectionState actionSelection;
    std::optional<std::size_t> hoveredActionIndex;
    basilisk::game::ActionPanelGeometry actionGeometry;
    basilisk::game::InventoryPanelGeometry inventoryGeometry;
    basilisk::game::MapActionMenuState mapActionMenu;
    basilisk::game::MapActionMenuGeometry mapActionMenuGeometry;
    basilisk::game::LifecycleModalGeometry lifecycleModalGeometry;
    std::optional<basilisk::RoundNumber> mapActionMenuRound;
#if defined(BASILISK_GAME_DEBUG)
    std::unique_ptr<basilisk::game::debug::DebugMapProvider> debugMapProvider;
    basilisk::game::debug::DebugMapRevealState debugMapReveal;
    basilisk::game::debug::DebugMapRevealState debugGameplayReveal;
#endif
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

void ingestDemoSnapshot(
    basilisk::game::ClientSessionController& session,
    basilisk::PlayerRoundSnapshot snapshot,
    bool advanceRound = false) {

    if (const auto* current = session.snapshotFor(snapshot.player)) {
        snapshot.round = advanceRound
            ? static_cast<basilisk::RoundNumber>(current->round + 1)
            : std::max(snapshot.round, current->round);
    }
    (void)session.ingestSnapshot(std::move(snapshot));
}

void autoLockSelectedAction(AppState& state) {
    if (!state.autoLockSelectedActions) return;
    if (!state.actionSelection.submitAndLock(*state.session)) {
        SDL_Log("Automatic local-game action submit/lock failed");
    }
}

SDL_AppResult handleMainMenuResult(
    AppState& state,
    basilisk::game::MainMenuResult result) {

    if (result == basilisk::game::MainMenuResult::Exit)
        return SDL_APP_SUCCESS;
    if (result == basilisk::game::MainMenuResult::Logout &&
        state.networkSession != nullptr &&
        state.authenticatedSessionToken.has_value()) {
        if (!state.networkSession->logout(*state.authenticatedSessionToken)) {
            SDL_Log("Logout request could not be sent");
            return SDL_APP_CONTINUE;
        }
        std::string storageError;
        if (!basilisk::game::SessionTokenStorage::clear(storageError))
            SDL_Log("Unable to clear saved session: %s", storageError.c_str());
        state.authenticatedSessionToken.reset();
        state.authenticatedProfile.reset();
        state.authScreen = basilisk::game::AuthScreenState{};
        state.authScreen.setWaiting(true);
        state.authResponseHandled = false;
        state.storedSessionAttempted = true;
        state.restoringSession = false;
        state.view = AppView::Authentication;
        (void)SDL_StartTextInput(state.window);
        return SDL_APP_CONTINUE;
    }
    if (result == basilisk::game::MainMenuResult::RequestLeaderboard &&
        state.networkSession != nullptr && state.session != nullptr) {
        if (!state.networkSession->requestLeaderboard(
                state.mainMenu.leaderboardOffset(),
                basilisk::game::MainMenuState::leaderboardPageSize)) {
            SDL_Log("Leaderboard request could not be sent");
        }
    }
    if (result == basilisk::game::MainMenuResult::RequestHostLobby &&
        state.networkSession != nullptr) {
        if (!state.networkSession->requestLobby({
                basilisk::game::network::kProtocolVersion,
                basilisk::game::network::HostLobbyRequest{}}))
            state.mainMenu.lobbyFailed("Unable to create lobby.");
    }
    if (result == basilisk::game::MainMenuResult::RequestJoinLobby &&
        state.networkSession != nullptr) {
        if (!state.networkSession->requestLobby({
                basilisk::game::network::kProtocolVersion,
                basilisk::game::network::JoinLobbyRequest{
                    state.mainMenu.lobbyCode()}}))
            state.mainMenu.lobbyFailed("Unable to join lobby.");
    }
    if (result == basilisk::game::MainMenuResult::RequestCancelLobby &&
        state.networkSession != nullptr) {
        if (state.mainMenu.lobbyCode().empty()) {
            state.cancelLobbyWhenHosted = true;
        } else {
            if (!state.networkSession->requestLobby({
                    basilisk::game::network::kProtocolVersion,
                    basilisk::game::network::CancelHostedLobbyRequest{
                        state.mainMenu.lobbyCode()}}))
                state.mainMenu.lobbyFailed("Unable to cancel lobby.");
        }
    }
    if (result == basilisk::game::MainMenuResult::RequestFindMatch &&
        state.networkSession != nullptr) {
        if (!state.networkSession->requestLobby({
                basilisk::game::network::kProtocolVersion,
                basilisk::game::network::FindMatchRequest{}}))
            state.mainMenu.lobbyFailed("Unable to enter matchmaking.");
    }
    if (result == basilisk::game::MainMenuResult::RequestCancelFindMatch &&
        state.networkSession != nullptr) {
        if (!state.networkSession->requestLobby({
                basilisk::game::network::kProtocolVersion,
                basilisk::game::network::CancelFindMatchRequest{}}))
            state.mainMenu.lobbyFailed("Unable to cancel matchmaking.");
    }
    return SDL_APP_CONTINUE;
}

void submitAuthentication(AppState& state) {
    if (state.networkSession == nullptr || state.authScreen.waiting()) return;
    basilisk::game::network::AuthenticationRequest request;
    if (!state.authScreen.request(request)) return;
    state.authResponseHandled = false;
    if (!state.networkSession->authenticate(request))
        state.authScreen.setError("Unable to send authentication request.");
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    auto* state = new (std::nothrow) AppState{};
    if (state == nullptr) {
        SDL_Log("Unable to allocate application state");
        return SDL_APP_FAILURE;
    }
    *appstate = state;
    state->ownedSession =
        std::make_unique<basilisk::game::ClientSessionController>();
    state->session = state->ownedSession.get();

    std::optional<std::string> connectUrl;
    std::optional<std::string> connectToken;
    for (int index = 1; index < argc; ++index) {
        if (argv == nullptr || argv[index] == nullptr) continue;
        const std::string_view argument{argv[index]};
        if (argument != "--connect" && argument != "--token") continue;
        if (index + 1 >= argc || argv[index + 1] == nullptr) {
            SDL_Log("%s requires a value", argv[index]);
            return SDL_APP_FAILURE;
        }
        if (argument == "--connect") connectUrl = argv[++index];
        else connectToken = argv[++index];
    }
    if (connectToken.has_value() && !connectUrl.has_value()) {
        SDL_Log("--token requires --connect");
        return SDL_APP_FAILURE;
    }
    bool developmentLaunch = false;
    for (int index = 1; index < argc; ++index) {
        if (argv != nullptr && argv[index] != nullptr &&
            (std::string_view{argv[index]} == "--demo-map" ||
             std::string_view{argv[index]} == "--local-game"))
            developmentLaunch = true;
    }
    if (!developmentLaunch && !connectUrl.has_value())
        connectUrl = "ws://127.0.0.1:8765";
    if (connectUrl.has_value()) {
        std::string error;
        state->networkSession = connectToken.has_value()
            ? basilisk::game::WebSocketNetworkSession::connect(
                *connectUrl, *connectToken, error)
            : basilisk::game::WebSocketNetworkSession::connectForAuthentication(
                *connectUrl, error);
        if (state->networkSession == nullptr) {
            SDL_Log("Unable to connect network session: %s", error.c_str());
            return SDL_APP_FAILURE;
        }
        state->ownedSession.reset();
        state->session = nullptr;
        state->screenShellEnabled = true;
        state->view = connectToken.has_value()
            ? AppView::Gameplay : AppView::Authentication;
        SDL_Log("Connecting to Basilisk server at %s", connectUrl->c_str());
    }

    for (int index = 1; index < argc; ++index) {
        if (argv != nullptr && argv[index] != nullptr &&
            std::string_view{argv[index]} == "--demo-map") {
            state->ownedSession =
                basilisk::game::demo::makeDemoSessionController();
            state->session = state->ownedSession.get();
            (void)state->session->ingestSnapshot(
                basilisk::game::demo::makeDemoMapSnapshot());
            (void)basilisk::game::selectRouteDestination(
                state->mapPresentation,
                state->session->displayedSnapshot()->map,
                basilisk::CaveId{34});
            state->demoMapEnabled = true;
            state->screenShellEnabled = true;
            state->view = AppView::Gameplay;
            SDL_Log("Development demo map enabled");
            break;
        } else if (argv != nullptr && argv[index] != nullptr &&
                   std::string_view{argv[index]} == "--local-game") {
            basilisk::MapSeed mapSeed{20260812};
            for (int seedIndex = 1; seedIndex < argc; ++seedIndex) {
                if (argv[seedIndex] == nullptr ||
                    std::string_view{argv[seedIndex]} != "--map-seed") {
                    continue;
                }
                if (seedIndex + 1 >= argc || argv[seedIndex + 1] == nullptr) {
                    SDL_Log("--map-seed requires an unsigned integer value");
                    return SDL_APP_FAILURE;
                }
                const std::string_view value{argv[++seedIndex]};
                basilisk::MapSeed parsed{};
                const auto result = std::from_chars(
                    value.data(), value.data() + value.size(), parsed);
                if (result.ec != std::errc{} ||
                    result.ptr != value.data() + value.size()) {
                    SDL_Log(
                        "Invalid --map-seed value '%s'; expected an unsigned integer",
                        argv[seedIndex]);
                    return SDL_APP_FAILURE;
                }
                mapSeed = parsed;
            }

#if defined(BASILISK_GAME_DEBUG)
            auto debugSession =
                basilisk::game::LocalGameSessionAdapter::createDebug(
                    mapSeed,
                    basilisk::MatchSeed{424242});
            state->ownedSession = std::move(debugSession.session);
            state->session = state->ownedSession.get();
            state->debugMapProvider = std::move(debugSession.mapProvider);
#else
            state->ownedSession = basilisk::game::LocalGameSessionAdapter::create(
                mapSeed,
                basilisk::MatchSeed{424242});
            state->session = state->ownedSession.get();
#endif
            if (state->session == nullptr) {
                SDL_Log("Unable to create local Core session");
                return SDL_APP_FAILURE;
            }
#if defined(BASILISK_GAME_DEBUG)
            if (state->debugMapProvider == nullptr) {
                SDL_Log("Unable to create debug map provider");
                return SDL_APP_FAILURE;
            }
#endif
            state->screenShellEnabled = true;
            state->view = AppView::Gameplay;
            state->autoLockSelectedActions = true;
            SDL_Log(
                "Trusted local Core session enabled (map seed: %llu)",
                static_cast<unsigned long long>(mapSeed));
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
    if (state->view == AppView::Authentication)
        (void)SDL_StartTextInput(state->window);

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

    if (state != nullptr && state->view == AppView::Authentication) {
        if (event->type == SDL_EVENT_TEXT_INPUT) {
            state->authScreen.append(event->text.text);
            return SDL_APP_CONTINUE;
        }
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            if (event->key.key == SDLK_BACKSPACE) state->authScreen.backspace();
            else if (event->key.key == SDLK_TAB) state->authScreen.nextField();
            else if (event->key.key == SDLK_RETURN ||
                     event->key.key == SDLK_KP_ENTER) submitAuthentication(*state);
            else if (event->key.key == SDLK_ESCAPE) return SDL_APP_SUCCESS;
            return SDL_APP_CONTINUE;
        }
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event->button.button == SDL_BUTTON_LEFT) {
            if (!SDL_ConvertEventToRenderCoordinates(state->renderer, event))
                return SDL_APP_FAILURE;
            const basilisk::game::PresentationPoint point{
                event->button.x, event->button.y};
            for (const auto& field : state->authGeometry.fields) {
                if (basilisk::game::hitTest(field.bounds, point)) {
                    state->authScreen.focus(field.field);
                    return SDL_APP_CONTINUE;
                }
            }
            if (basilisk::game::hitTest(state->authGeometry.submit, point))
                submitAuthentication(*state);
            else if (basilisk::game::hitTest(
                         state->authGeometry.switchMode, point))
                state->authScreen.switchMode();
        }
        return SDL_APP_CONTINUE;
    }

    if (state != nullptr && state->view == AppView::MainMenu) {
        if (state->mainMenu.page() == basilisk::game::MainMenuPage::JoinLobby &&
            event->type == SDL_EVENT_TEXT_INPUT) {
            state->mainMenu.appendLobbyCode(event->text.text);
            return SDL_APP_CONTINUE;
        }
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            if (state->mainMenu.page() ==
                    basilisk::game::MainMenuPage::JoinLobby &&
                event->key.key == SDLK_BACKSPACE) {
                state->mainMenu.eraseLobbyCode();
                return SDL_APP_CONTINUE;
            }
            if (event->key.key == SDLK_UP) {
                state->mainMenu.moveSelection(-1);
                return SDL_APP_CONTINUE;
            }
            if (event->key.key == SDLK_DOWN) {
                state->mainMenu.moveSelection(1);
                return SDL_APP_CONTINUE;
            }
            if (event->key.key == SDLK_RETURN ||
                event->key.key == SDLK_KP_ENTER) {
                const auto result = state->mainMenu.activateSelected();
                if (state->mainMenu.page() ==
                        basilisk::game::MainMenuPage::JoinLobby)
                    (void)SDL_StartTextInput(state->window);
                return handleMainMenuResult(*state, result);
            }
            if (event->key.key == SDLK_ESCAPE) {
                if (state->mainMenu.page() ==
                        basilisk::game::MainMenuPage::Main &&
                    state->session != nullptr &&
                    state->session->displayedSnapshot() != nullptr) {
                    state->view = AppView::Gameplay;
                } else {
                    return handleMainMenuResult(
                        *state, state->mainMenu.back());
                }
                return SDL_APP_CONTINUE;
            }
        }
        if (event->type == SDL_EVENT_MOUSE_MOTION ||
            (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
             event->button.button == SDL_BUTTON_LEFT)) {
            if (!SDL_ConvertEventToRenderCoordinates(state->renderer, event)) {
                SDL_Log("Unable to convert menu pointer coordinates: %s",
                    SDL_GetError());
                return SDL_APP_FAILURE;
            }
            const basilisk::game::PresentationPoint pointer =
                event->type == SDL_EVENT_MOUSE_MOTION
                ? basilisk::game::PresentationPoint{
                    event->motion.x, event->motion.y}
                : basilisk::game::PresentationPoint{
                    event->button.x, event->button.y};
            const auto hit = basilisk::game::hitTestMainMenu(
                state->mainMenuGeometry, pointer);
            if (hit.has_value()) state->mainMenu.select(*hit);
            if (hit.has_value() &&
                event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                const auto result = state->mainMenu.activateSelected();
                if (state->mainMenu.page() ==
                        basilisk::game::MainMenuPage::JoinLobby)
                    (void)SDL_StartTextInput(state->window);
                return handleMainMenuResult(*state, result);
            }
        }
        return SDL_APP_CONTINUE;
    }

#if defined(BASILISK_GAME_DEBUG)
    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !event->key.repeat && event->key.key == SDLK_F1) {
        if (state->debugMapProvider == nullptr) {
            SDL_Log("Debug map reveal is available only in --local-game");
        } else {
            state->debugMapReveal.toggle();
            SDL_Log(
                "Debug map reveal %s",
                state->debugMapReveal.revealed() ? "enabled" : "disabled");
        }
        return SDL_APP_CONTINUE;
    }
    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !event->key.repeat && event->key.key == SDLK_F2) {
        if (state->debugMapProvider == nullptr) {
            SDL_Log("Debug gameplay truth is available only in --local-game");
        } else {
            state->debugGameplayReveal.toggle();
            SDL_Log(
                "Debug gameplay truth %s",
                state->debugGameplayReveal.revealed()
                    ? "enabled"
                    : "disabled");
        }
        return SDL_APP_CONTINUE;
    }
    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !event->key.repeat && event->key.key == SDLK_F3) {
        if (state->debugMapProvider == nullptr) {
            SDL_Log("Debug Basilisk behavior control is available only in --local-game");
        } else if (!state->debugMapProvider->cycleBasiliskBehavior()) {
            SDL_Log("Unable to cycle debug Basilisk behavior");
        } else {
            SDL_Log("Debug Basilisk behavior cycled");
        }
        return SDL_APP_CONTINUE;
    }
#endif

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
        ingestDemoSnapshot(
            *state->session,
            basilisk::game::demo::makeDemoMapSnapshot(
                stages[state->demoSnapshotStage]),
            stages[state->demoSnapshotStage] ==
                basilisk::game::demo::DemoSnapshotStage::NextRound);
        state->session->setViewContext(basilisk::client::ClientViewContext{
            basilisk::PlayerId{1},
            basilisk::PlayerId{1},
            basilisk::client::ClientViewMode::Playing,
            std::nullopt,
        });
        const auto* snapshot = state->session->displayedSnapshot();
        SDL_Log(
            "Development snapshot stage %zu/%zu, round %u",
            state->demoSnapshotStage + 1,
            stages.size(),
            snapshot == nullptr ? 0U : static_cast<unsigned int>(snapshot->round));
        return SDL_APP_CONTINUE;
    }

    if (state != nullptr && state->demoMapEnabled &&
        event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        if (event->key.key == SDLK_F7) {
            ingestDemoSnapshot(
                *state->session,
                basilisk::game::demo::makeDemoDefeatedSnapshot(false),
                true);
            ingestDemoSnapshot(
                *state->session,
                basilisk::game::demo::makeDemoSurvivorSnapshot(false));
            state->session->setViewContext(basilisk::client::ClientViewContext{
                basilisk::PlayerId{1},
                basilisk::PlayerId{1},
                basilisk::client::ClientViewMode::Defeated,
                basilisk::PlayerId{2},
            });
            state->mapActionMenu.dismiss();
            SDL_Log("Development first-death snapshot enabled");
            return SDL_APP_CONTINUE;
        }
        if (event->key.key == SDLK_F8) {
            ingestDemoSnapshot(
                *state->session,
                basilisk::game::demo::makeDemoDefeatedSnapshot(true),
                true);
            state->session->setViewContext(basilisk::client::ClientViewContext{
                basilisk::PlayerId{1},
                basilisk::PlayerId{1},
                basilisk::client::ClientViewMode::Defeated,
                std::nullopt,
            });
            state->mapActionMenu.dismiss();
            SDL_Log("Development final-death snapshot enabled");
            return SDL_APP_CONTINUE;
        }
        if (event->key.key == SDLK_F9) {
            ingestDemoSnapshot(
                *state->session,
                basilisk::game::demo::makeDemoSurvivorSnapshot(true));
            state->session->setViewContext(basilisk::client::ClientViewContext{
                basilisk::PlayerId{1},
                basilisk::PlayerId{2},
                basilisk::client::ClientViewMode::Spectating,
                basilisk::PlayerId{2},
            });
            state->mapActionMenu.dismiss();
            SDL_Log("Development spectator match-end snapshot enabled");
            return SDL_APP_CONTINUE;
        }
    }

    const basilisk::PlayerRoundSnapshot* snapshot = state == nullptr ||
            state->session == nullptr
        ? nullptr
        : state->session->displayedSnapshot();
    const bool lifecycleModalActive = snapshot != nullptr &&
        basilisk::game::lifecycleModalPresentation(
            *snapshot,
            state->session->viewContext(),
            state->session->profiles()).has_value();

    if (state != nullptr && event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
        state->hoveredActionIndex.reset();
        basilisk::game::updateMapHover(
            state->mapPresentation,
            basilisk::game::MapHitTarget{});
        return SDL_APP_CONTINUE;
    }

    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !event->key.repeat && event->key.key == SDLK_ESCAPE) {
        if (!lifecycleModalActive && state->mapActionMenu.isOpen()) {
            state->mapActionMenu.dismiss();
        } else if (!lifecycleModalActive && state->networkSession != nullptr) {
            state->view = AppView::MainMenu;
            (void)state->mainMenu.back();
        }
        return SDL_APP_CONTINUE;
    }

    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !event->key.repeat && event->key.key >= SDLK_1 && event->key.key <= SDLK_9) {
        if (lifecycleModalActive || snapshot == nullptr) return SDL_APP_CONTINUE;
        const std::size_t index = static_cast<std::size_t>(event->key.key - SDLK_1);
        if (state->actionSelection.select(
                index,
                snapshot->availableActions,
                state->session->viewContext())) {
            state->mapActionMenu.dismiss();
            state->actionSelection.ensureVisible(
                index, state->actionGeometry.visibleCapacity);
            autoLockSelectedAction(*state);
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
        if (snapshot == nullptr) return SDL_APP_CONTINUE;
        basilisk::game::PresentationPoint pointer;
        if (event->type == SDL_EVENT_MOUSE_MOTION) {
            pointer = {event->motion.x, event->motion.y};
        } else if (event->type == SDL_EVENT_MOUSE_WHEEL) {
            pointer = {event->wheel.mouse_x, event->wheel.mouse_y};
            if (lifecycleModalActive) return SDL_APP_CONTINUE;
            if (basilisk::game::hitTestActionPanel(state->actionGeometry, pointer)) {
                const int delta = event->wheel.y > 0.0F
                    ? -1
                    : event->wheel.y < 0.0F ? 1 : 0;
                state->actionSelection.scrollRows(
                    delta,
                    snapshot->availableActions.size(),
                    state->actionGeometry.visibleCapacity);
            }
            state->hoveredActionIndex.reset();
            basilisk::game::updateMapHover(
                state->mapPresentation,
                basilisk::game::MapHitTarget{});
            return SDL_APP_CONTINUE;
        } else {
            pointer = {event->button.x, event->button.y};
        }

        if (lifecycleModalActive) {
            if (event->type == SDL_EVENT_MOUSE_MOTION) {
                state->hoveredActionIndex.reset();
                basilisk::game::updateMapHover(
                    state->mapPresentation,
                    basilisk::game::MapHitTarget{});
            }
            if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event->button.button == SDL_BUTTON_LEFT) {
                if (basilisk::game::hitTestLifecycleWatch(
                        state->lifecycleModalGeometry, pointer)) {
                    if (state->session->watchRemainingHunter()) {
                        state->mapActionMenu.dismiss();
                        SDL_Log(
                            "Now spectating player %llu",
                            static_cast<unsigned long long>(
                                state->session->viewContext().viewedPlayer));
                    }
                } else if (basilisk::game::hitTestLifecycleQuit(
                               state->lifecycleModalGeometry, pointer) &&
                           state->session->quit()) {
                    return SDL_APP_SUCCESS;
                }
            }
            return SDL_APP_CONTINUE;
        }

        if (event->type == SDL_EVENT_MOUSE_MOTION) {
            const bool actionWasHovered =
                state->hoveredActionIndex.has_value();
            state->hoveredActionIndex = basilisk::game::hitTestActionRow(
                state->actionGeometry, pointer);
            if (state->hoveredActionIndex.has_value() &&
                *state->hoveredActionIndex < snapshot->availableActions.size()) {
                basilisk::game::updateMapHover(
                    state->mapPresentation,
                    basilisk::game::mapHoverTargetForAction(
                        snapshot->availableActions[*state->hoveredActionIndex],
                        snapshot->map.currentCave));
                return SDL_APP_CONTINUE;
            }
            if (actionWasHovered) {
                basilisk::game::updateMapHover(
                    state->mapPresentation,
                    basilisk::game::MapHitTarget{});
            }
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
                                snapshot->availableActions,
                                state->session->viewContext(),
                                state->actionSelection)) {
                            state->actionSelection.ensureVisible(
                                menuChoice->actionIndex,
                                state->actionGeometry.visibleCapacity);
                            autoLockSelectedAction(*state);
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
                            snapshot->map,
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
            if (const auto item = basilisk::game::hitTestInventoryItem(
                    state->inventoryGeometry, pointer); item.has_value()) {
                if (basilisk::game::selectInventoryItemAction(
                        *item,
                        snapshot->availableActions,
                        state->session->viewContext(),
                        state->actionSelection)) {
                    autoLockSelectedAction(*state);
                }
                state->mapActionMenu.dismiss();
                return SDL_APP_CONTINUE;
            }
            if (const auto actionIndex = basilisk::game::hitTestActionRow(
                    state->actionGeometry, pointer); actionIndex.has_value()) {
                if (state->actionSelection.select(
                        *actionIndex,
                        snapshot->availableActions,
                        state->session->viewContext())) {
                    autoLockSelectedAction(*state);
                }
                state->mapActionMenu.dismiss();
                return SDL_APP_CONTINUE;
            }
            if (basilisk::game::hitTestActionLockButton(
                    state->actionGeometry, pointer)) {
                if (state->actionSelection.canLock(
                        state->session->viewContext()) &&
                    !state->actionSelection.submitAndLock(*state->session)) {
                    SDL_Log("Action submit/lock command failed");
                }
                return SDL_APP_CONTINUE;
            }
        }

        const basilisk::game::MapHitTarget hit =
            basilisk::game::hitTestPlayerKnownMap(
                snapshot->map,
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
                        snapshot->availableActions,
                        target,
                        snapshot->map.currentCave).empty();
                const basilisk::game::DestinationControl destinationControl =
                    basilisk::game::destinationControlForCave(
                        snapshot->map,
                        *hit.cave,
                        state->mapPresentation.routeDestination,
                        hasMatchingLegalAction);
                (void)state->mapActionMenu.open(
                    target,
                    pointer.x,
                    pointer.y,
                    snapshot->availableActions,
                    snapshot->map.currentCave,
                    state->session->viewContext(),
                    destinationControl,
                    !state->actionSelection.locked());
            } else if (hit.kind == basilisk::game::MapHitKind::UnknownExit &&
                       hit.unknownExit.has_value()) {
                (void)state->mapActionMenu.open(
                    basilisk::game::unknownExitActionTarget(
                        hit.unknownExit->source,
                        hit.unknownExit->tunnel),
                    pointer.x,
                    pointer.y,
                    snapshot->availableActions,
                    snapshot->map.currentCave,
                    state->session->viewContext(),
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

    if (state->networkSession != nullptr) {
        state->networkSession->pump();
        state->session = state->networkSession->controller();
        if (state->networkSession->lobbyResponseRevision() !=
                state->handledLobbyResponseRevision &&
            state->networkSession->lobbyResponse().has_value()) {
            state->handledLobbyResponseRevision =
                state->networkSession->lobbyResponseRevision();
            const auto& response = *state->networkSession->lobbyResponse();
            if (const auto* hosted = std::get_if<
                    basilisk::game::network::LobbyHosted>(&response.payload)) {
                state->mainMenu.lobbyHosted(hosted->lobbyCode);
                if (state->cancelLobbyWhenHosted) {
                    state->cancelLobbyWhenHosted = false;
                    (void)state->networkSession->requestLobby({
                        basilisk::game::network::kProtocolVersion,
                        basilisk::game::network::CancelHostedLobbyRequest{
                            hosted->lobbyCode}});
                }
            } else if (const auto* assigned = std::get_if<
                    basilisk::game::network::LobbyMatchAssigned>(
                        &response.payload)) {
                state->mainMenu.lobbyAssigned(assigned->lobbyCode);
                (void)SDL_StopTextInput(state->window);
            } else if (std::holds_alternative<
                           basilisk::game::network::LobbyCancelled>(
                               response.payload)) {
                state->mainMenu.lobbyCancelled();
            } else if (std::holds_alternative<
                           basilisk::game::network::MatchmakingQueued>(
                               response.payload)) {
                // The menu is already showing its FIFO waiting state.
            } else if (std::holds_alternative<
                           basilisk::game::network::MatchmakingCancelled>(
                               response.payload)) {
                state->mainMenu.matchmakingCancelled();
            } else if (const auto* failure = std::get_if<
                    basilisk::game::network::LobbyFailure>(
                        &response.payload)) {
                state->mainMenu.lobbyFailed(failure->message);
            }
        }
        if (state->view == AppView::MainMenu &&
            state->mainMenu.page() == basilisk::game::MainMenuPage::MatchReady &&
            state->session != nullptr &&
            state->session->displayedSnapshot() != nullptr) {
            state->view = AppView::Gameplay;
            state->screenShellEnabled = true;
        }
        if (state->view == AppView::Authentication &&
            !state->storedSessionAttempted &&
            state->networkSession->state() ==
                basilisk::game::NetworkConnectionState::Connected) {
            state->storedSessionAttempted = true;
            state->authenticatedSessionToken =
                basilisk::game::SessionTokenStorage::load();
            if (state->authenticatedSessionToken.has_value()) {
                state->restoringSession = true;
                state->authScreen.setWaiting(true);
                state->authResponseHandled = false;
                if (!state->networkSession->authenticate({
                        basilisk::game::network::kProtocolVersion,
                        basilisk::game::network::AuthenticateSessionRequest{
                            *state->authenticatedSessionToken}})) {
                    state->authScreen.setError(
                        "Unable to restore the saved session.");
                }
            }
        }
        if (state->view == AppView::Authentication &&
            !state->authResponseHandled &&
            state->networkSession->authenticationResponse().has_value()) {
            state->authResponseHandled = true;
            const auto& response =
                *state->networkSession->authenticationResponse();
            if (const auto* success = std::get_if<
                    basilisk::game::network::AuthenticationSuccess>(
                        &response.payload)) {
                state->authenticatedProfile = success->profile;
                state->authenticatedSessionToken = success->sessionToken;
                std::string storageError;
                if (!basilisk::game::SessionTokenStorage::save(
                        success->sessionToken, storageError))
                    SDL_Log("Unable to save session: %s", storageError.c_str());
                state->restoringSession = false;
                state->authScreen.setWaiting(false);
                state->view = AppView::MainMenu;
                (void)SDL_StopTextInput(state->window);
            } else if (const auto* failure = std::get_if<
                    basilisk::game::network::AuthenticationFailure>(
                        &response.payload)) {
                if (state->restoringSession) {
                    std::string ignored;
                    (void)basilisk::game::SessionTokenStorage::clear(ignored);
                    state->authenticatedSessionToken.reset();
                    state->restoringSession = false;
                    state->authScreen.setError(
                        "Your session expired. Sign in again.");
                } else {
                    state->authScreen.setError(failure->message);
                }
            } else if (std::holds_alternative<
                           basilisk::game::network::LogoutSuccess>(
                               response.payload)) {
                state->authScreen.setWaiting(false);
            }
        }
        if (!state->networkFailureLogged &&
            (state->networkSession->state() ==
                 basilisk::game::NetworkConnectionState::Error ||
             state->networkSession->state() ==
                 basilisk::game::NetworkConnectionState::Disconnected)) {
            SDL_Log("Network session ended: %s",
                state->networkSession->error().c_str());
            if (state->view == AppView::Authentication)
                state->authScreen.setError(state->networkSession->error());
            state->networkFailureLogged = true;
        }
    }

    SDL_SetRenderDrawColor(
        state->renderer, 12, 16, state->backgroundBlue, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(state->renderer);

    int outputWidth = 0;
    int outputHeight = 0;
    if (SDL_GetRenderOutputSize(state->renderer, &outputWidth, &outputHeight)) {
        if (state->view == AppView::Authentication) {
            std::string authError;
            if (!basilisk::game::renderAuthScreen(
                    state->renderer, *state->textRenderer, state->authScreen,
                    state->authGeometry, outputWidth, outputHeight, authError)) {
                SDL_Log("Authentication rendering failed: %s", authError.c_str());
                return SDL_APP_FAILURE;
            }
        } else if (state->view == AppView::MainMenu) {
            std::string menuError;
            const std::optional<std::int64_t> trophies =
                state->networkSession == nullptr || state->session == nullptr
                ? std::nullopt
                : std::optional<std::int64_t>{state->session->trophyTotal()};
            static const std::optional<
                basilisk::game::network::LeaderboardPageResponse>
                noLeaderboard;
            const auto& leaderboard = state->networkSession == nullptr
                ? noLeaderboard
                : state->networkSession->leaderboardPage();
            if (!basilisk::game::renderMainMenu(
                    state->renderer,
                    *state->textRenderer,
                    state->mainMenu,
                    trophies,
                    state->authenticatedProfile,
                    leaderboard,
                    state->mainMenuGeometry,
                    outputWidth,
                    outputHeight,
                    menuError)) {
                SDL_Log("Main menu rendering failed: %s", menuError.c_str());
                return SDL_APP_FAILURE;
            }
        } else {
          const basilisk::PlayerRoundSnapshot* snapshot = state->session == nullptr
              ? nullptr
              : state->session->displayedSnapshot();
          if (state->screenShellEnabled) {
            if (snapshot != nullptr && state->session != nullptr) {
                state->actionSelection.synchronize(
                    snapshot->round,
                    snapshot->availableActions.size(),
                    state->session->viewContext());
                if (!state->mapActionMenuRound.has_value() ||
                    *state->mapActionMenuRound != snapshot->round) {
                    state->mapActionMenu.dismiss();
                    state->hoveredActionIndex.reset();
                    basilisk::game::updateMapHover(
                        state->mapPresentation,
                        basilisk::game::MapHitTarget{});
                    state->mapActionMenuRound = snapshot->round;
                }
            }
            std::string screenError;
#if defined(BASILISK_GAME_DEBUG)
            std::optional<basilisk::game::debug::DebugGameplayTruth>
                debugGameplayTruth;
            if (state->debugMapProvider != nullptr) {
                debugGameplayTruth = state->debugMapProvider->gameplayTruth();
            }
#endif
            if (state->session != nullptr &&
                !basilisk::game::renderScreenShell(
                    state->renderer,
                    *state->textRenderer,
                    *state->svgTextures,
                    *state->session,
                    state->mapLayout,
                    state->mapPresentation,
                    state->mapGeometry,
                    state->actionSelection,
                    state->hoveredActionIndex,
                    state->actionGeometry,
                    state->inventoryGeometry,
                    state->mapActionMenu,
                    state->mapActionMenuGeometry,
                    state->lifecycleModalGeometry,
#if defined(BASILISK_GAME_DEBUG)
                    state->debugMapProvider == nullptr
                        ? nullptr
                        : &state->debugMapProvider->mapTruth(),
                    debugGameplayTruth.has_value()
                        ? &*debugGameplayTruth
                        : nullptr,
                    state->debugMapReveal.revealed(),
                    state->debugGameplayReveal.revealed(),
#endif
                    outputWidth,
                    outputHeight,
                    screenError)) {
                SDL_Log("Screen shell rendering failed: %s", screenError.c_str());
                return SDL_APP_FAILURE;
            }

            if (state->networkSession != nullptr) {
                std::string connectionError;
                const bool sessionReady = snapshot != nullptr;
                if (!basilisk::game::renderConnectionStatus(
                        state->renderer,
                        *state->textRenderer,
                        state->networkSession->state(),
                        state->networkSession->error(),
                        sessionReady,
                        outputWidth,
                        outputHeight,
                        connectionError)) {
                    SDL_Log(
                        "Connection status rendering failed: %s",
                        connectionError.c_str());
                    return SDL_APP_FAILURE;
                }
            }
          } else if (snapshot != nullptr) {
            if (const auto* fixed = state->session->displayedMapGeometry()) {
                state->mapLayout.updateFixed(*fixed);
            } else {
                state->mapLayout.update(snapshot->map);
            }
            constexpr float padding = 24.0F;
            const basilisk::game::PresentationRect bounds{
                padding,
                padding,
                std::max(0.0F, static_cast<float>(outputWidth) - padding * 2.0F),
                std::max(0.0F, static_cast<float>(outputHeight) - padding * 2.0F)};
            const double pixelDensity = std::max(
                1.0, static_cast<double>(SDL_GetWindowPixelDensity(state->window)));
            state->mapGeometry = basilisk::game::buildMapPresentationGeometry(
                snapshot->map,
                state->mapLayout,
                snapshot->temporarilyRevealedPitCaves,
                bounds,
                padding,
                pixelDensity);
            std::string mapError;
            if (!basilisk::game::renderPlayerKnownMap(
                    state->renderer,
                    *state->textRenderer,
                    *snapshot,
                    state->mapLayout,
                    state->mapGeometry,
                    state->mapPresentation,
                    mapError)) {
                SDL_Log("Map rendering failed: %s", mapError.c_str());
                return SDL_APP_FAILURE;
            }
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
