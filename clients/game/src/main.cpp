#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
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
#include "ClashQteRenderer.hpp"
#include "ClientSessionController.hpp"
#include "ConnectionStatusPresentation.hpp"
#include "DemoMap.hpp"
#include "DemoUi.hpp"
#include "LocalGameSessionAdapter.hpp"
#include "LocalAiGameSessionAdapter.hpp"
#include "LocalSandboxSessionAdapter.hpp"
#include "MapRenderer.hpp"
#include "MainMenu.hpp"
#include "MainMenuRenderer.hpp"
#include "MapActionMenu.hpp"
#include "MapPresentation.hpp"
#include "NetworkEndpointConfig.hpp"
#include "PauseMenu.hpp"
#include "PauseMenuRenderer.hpp"
#include "PointerInput.hpp"
#include "ScreenShell.hpp"
#include "SessionTokenStorage.hpp"
#include "SvgTextureManager.hpp"
#include "TextRenderer.hpp"
#include "WebSocketNetworkSession.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Random.hpp"

#if defined(BASILISK_GAME_DEBUG)
#include "DebugInventoryMenu.hpp"
#include "DebugKillMenu.hpp"
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
    bool startupSessionRestore{false};
    std::optional<basilisk::game::PublicAccountProfile> authenticatedProfile;
    std::optional<std::string> authenticatedSessionToken;
    std::optional<basilisk::client::AccountCosmeticLoadout>
        confirmedCosmeticLoadout;
    basilisk::game::MainMenuState mainMenu;
    basilisk::game::MainMenuGeometry mainMenuGeometry;
    std::size_t handledLobbyResponseRevision{0};
    std::size_t handledCosmeticLoadoutResponseRevision{0};
    bool cancelLobbyWhenHosted{false};
    bool enterOnlineAfterAuthentication{false};
    std::string serverUrl{"ws://127.0.0.1:8765"};
    bool awaitingEligibleMatchStart{false};
    std::optional<std::int64_t> trophyMatchStartTotal;
    std::optional<basilisk::game::TrophyAwardPresentation> trophyAward;
    bool trophyAwardPresented{false};
    std::optional<std::int64_t> lastKnownTrophyTotal;

    std::unique_ptr<basilisk::game::ClientSessionController> ownedSession;
    basilisk::game::ClientSessionController* session{nullptr};
    std::unique_ptr<basilisk::game::WebSocketNetworkSession> networkSession;
    std::unique_ptr<basilisk::game::LocalAiSessionDriver> localAiDriver;
    std::unique_ptr<basilisk::game::LocalSandboxSessionDriver> localSandboxDriver;
    Uint64 localAiLastTick{0};
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
    basilisk::game::PauseMenuState pauseMenu;
    basilisk::game::PauseMenuGeometry pauseMenuGeometry;
    std::string clashInput;
    std::optional<basilisk::RoundNumber> mapActionMenuRound;
#if defined(BASILISK_GAME_DEBUG)
    std::unique_ptr<basilisk::game::debug::DebugMapProvider> debugMapProvider;
    basilisk::game::debug::DebugMapRevealState debugMapReveal;
    basilisk::game::debug::DebugMapRevealState debugGameplayReveal;
    basilisk::game::debug::DebugInventoryMenuState debugInventoryMenu;
    basilisk::game::debug::DebugKillMenuState debugKillMenu;
#endif
};

bool localGameplayActive(const AppState& state) noexcept {
    return state.localAiDriver != nullptr || state.localSandboxDriver != nullptr;
}

void resetTrophyAwardPresentation(AppState& state) {
    state.awaitingEligibleMatchStart = false;
    state.trophyMatchStartTotal.reset();
    state.trophyAward.reset();
    state.trophyAwardPresented = false;
}

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

void returnFromAuthenticationToStartGame(AppState& state) {
    state.view = AppView::MainMenu;
    (void)state.mainMenu.activate(basilisk::game::MainMenuAction::StartGame);
    state.enterOnlineAfterAuthentication = false;
    (void)SDL_StopTextInput(state.window);
}

void finishStartupSessionRestoreAtMainMenu(AppState& state) {
    state.startupSessionRestore = false;
    state.view = AppView::MainMenu;
    (void)SDL_StopTextInput(state.window);
}

bool beginOnlineAuthentication(
    AppState& state,
    bool enterOnlineAfterAuthentication,
    bool startupSessionRestore = false) {
    std::string error;
    state.networkSession =
        basilisk::game::WebSocketNetworkSession::connectForAuthentication(
            state.serverUrl, error);
    if (state.networkSession == nullptr) {
        SDL_Log("Unable to connect network session: %s", error.c_str());
        return false;
    }
    state.enterOnlineAfterAuthentication = enterOnlineAfterAuthentication;
    state.startupSessionRestore = startupSessionRestore;
    state.storedSessionAttempted = false;
    state.authResponseHandled = false;
    state.networkFailureLogged = false;
    state.view = AppView::Authentication;
    return true;
}

SDL_AppResult handleMainMenuResult(
    AppState& state,
    basilisk::game::MainMenuResult result) {

    if (result == basilisk::game::MainMenuResult::Exit)
        return SDL_APP_SUCCESS;
    if (result == basilisk::game::MainMenuResult::RequestPlayOnline) {
        if (state.authenticatedProfile.has_value() &&
            state.networkSession != nullptr) {
            state.mainMenu.openOnline();
            return SDL_APP_CONTINUE;
        }
        if (!beginOnlineAuthentication(state, true)) {
            return SDL_APP_CONTINUE;
        }
        (void)SDL_StartTextInput(state.window);
        return SDL_APP_CONTINUE;
    }
    if (result == basilisk::game::MainMenuResult::StartAiGame) {
        resetTrophyAwardPresentation(state);
        const Uint64 entropy = SDL_GetPerformanceCounter() ^ SDL_GetTicksNS();
        auto local = basilisk::game::LocalAiGameSessionAdapter::create(
            basilisk::MapSeed{entropy}, basilisk::MatchSeed{entropy ^ 0xA17EULL},
            state.mainMenu.aiDifficulty(), state.mainMenu.aiBehavior(),
            basilisk::client::ai::AiSeed{entropy ^ 0xB451115CULL});
        if (local.session == nullptr || local.driver == nullptr) {
            SDL_Log("Unable to create local AI match");
            return SDL_APP_CONTINUE;
        }
        state.ownedSession = std::move(local.session);
        state.session = state.ownedSession.get();
        state.localAiDriver = std::move(local.driver);
#if defined(BASILISK_GAME_DEBUG)
        state.debugMapProvider = std::move(local.mapProvider);
        state.debugMapReveal = basilisk::game::debug::DebugMapRevealState{};
        state.debugGameplayReveal = basilisk::game::debug::DebugMapRevealState{};
        state.debugInventoryMenu.close();
#endif
        state.localAiLastTick = SDL_GetTicks();
        state.screenShellEnabled = true;
        state.view = AppView::Gameplay;
        return SDL_APP_CONTINUE;
    }
    if (result == basilisk::game::MainMenuResult::StartSandbox) {
        resetTrophyAwardPresentation(state);
        const Uint64 entropy = SDL_GetPerformanceCounter() ^ SDL_GetTicksNS();
        auto config = state.mainMenu.sandboxConfig();
        config.mapSeed = basilisk::MapSeed{entropy};
        config.matchSeed = basilisk::MatchSeed{entropy ^ 0x53414e44424f58ULL};
        config.aiSeed = basilisk::client::ai::AiSeed{
            entropy ^ 0x48554e54455253ULL};
        auto local = basilisk::game::LocalSandboxSessionAdapter::create(config);
        if (local.session == nullptr || local.driver == nullptr) {
            SDL_Log("Unable to create local Sandbox match");
            return SDL_APP_CONTINUE;
        }
        state.ownedSession = std::move(local.session);
        state.session = state.ownedSession.get();
        state.localSandboxDriver = std::move(local.driver);
#if defined(BASILISK_GAME_DEBUG)
        state.debugMapProvider = std::move(local.mapProvider);
        state.debugMapReveal = basilisk::game::debug::DebugMapRevealState{};
        state.debugGameplayReveal = basilisk::game::debug::DebugMapRevealState{};
        state.debugInventoryMenu.close();
        state.debugKillMenu.close();
#endif
        state.localAiLastTick = SDL_GetTicks();
        state.screenShellEnabled = true;
        state.view = AppView::Gameplay;
        return SDL_APP_CONTINUE;
    }
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
        state.confirmedCosmeticLoadout.reset();
        state.lastKnownTrophyTotal.reset();
        state.authScreen = basilisk::game::AuthScreenState{};
        state.mainMenu = basilisk::game::MainMenuState{};
        state.handledLobbyResponseRevision =
            state.networkSession->lobbyResponseRevision();
        state.cancelLobbyWhenHosted = false;
        state.authScreen.setWaiting(true);
        state.authResponseHandled = false;
        state.storedSessionAttempted = true;
        state.restoringSession = false;
        state.view = AppView::Authentication;
        (void)SDL_StartTextInput(state.window);
        return SDL_APP_CONTINUE;
    }
    if (result == basilisk::game::MainMenuResult::RequestLeaderboard &&
        state.networkSession != nullptr) {
        if (!state.networkSession->requestLeaderboard(
                state.mainMenu.leaderboardOffset(),
                basilisk::game::MainMenuState::leaderboardPageSize)) {
            SDL_Log("Leaderboard request could not be sent");
            state.mainMenu.leaderboardFailed(
                "Unable to load the leaderboard.");
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
    if (result == basilisk::game::MainMenuResult::RequestHostSandboxLobby &&
        state.networkSession != nullptr) {
        if (!state.networkSession->requestLobby({
                basilisk::game::network::kProtocolVersion,
                basilisk::game::network::HostSandboxLobbyRequest{
                    state.mainMenu.sandboxConfig()}}))
            state.mainMenu.lobbyFailed("Unable to create Sandbox lobby.");
    }
    if (result == basilisk::game::MainMenuResult::RequestJoinSandboxLobby &&
        state.networkSession != nullptr) {
        if (!state.networkSession->requestLobby({
                basilisk::game::network::kProtocolVersion,
                basilisk::game::network::JoinSandboxLobbyRequest{
                    state.mainMenu.lobbyCode()}}))
            state.mainMenu.lobbyFailed("Unable to join Sandbox lobby.");
    }
    if (result == basilisk::game::MainMenuResult::RequestLeaveSandboxLobby &&
        state.networkSession != nullptr) {
        if (!state.networkSession->requestLobby({
                basilisk::game::network::kProtocolVersion,
                basilisk::game::network::LeaveSandboxLobbyRequest{
                    state.mainMenu.lobbyCode()}}))
            state.mainMenu.lobbyFailed("Unable to leave Sandbox lobby.");
    }
    if (result == basilisk::game::MainMenuResult::RequestSetSandboxReady &&
        state.networkSession != nullptr) {
        if (!state.networkSession->requestLobby({
                basilisk::game::network::kProtocolVersion,
                basilisk::game::network::SetSandboxReadyRequest{
                    state.mainMenu.lobbyCode(),
                    !state.mainMenu.sandboxLocalReady()}}))
            state.mainMenu.lobbyFailed("Unable to update ready state.");
    }
    if (result == basilisk::game::MainMenuResult::RequestStartSandboxMatch &&
        state.networkSession != nullptr) {
        if (!state.networkSession->requestLobby({
                basilisk::game::network::kProtocolVersion,
                basilisk::game::network::StartSandboxMatchRequest{
                    state.mainMenu.lobbyCode()}}))
            state.mainMenu.lobbyFailed("Unable to start Sandbox match.");
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

void requestCosmeticLoadout(
    AppState& state,
    basilisk::client::AccountCosmeticLoadout requested) {
    if (state.networkSession == nullptr ||
        !state.authenticatedSessionToken.has_value() ||
        !state.networkSession->updateCosmeticLoadout({
            basilisk::game::network::kProtocolVersion,
            *state.authenticatedSessionToken,
            std::move(requested)})) {
        SDL_Log("Unable to update account cosmetics");
    }
}

bool pointerInRenderCoordinates(
    SDL_Renderer* renderer,
    float windowX,
    float windowY,
    basilisk::game::PresentationPoint& point) {
    float renderX = 0.0F;
    float renderY = 0.0F;
    if (!SDL_RenderCoordinatesFromWindow(
            renderer, windowX, windowY, &renderX, &renderY)) {
        SDL_Log("Unable to convert menu pointer coordinates: %s",
            SDL_GetError());
        return false;
    }
    point = {renderX, renderY};
    return true;
}

SDL_AppResult finishGameplayQuit(AppState& state) {
    resetTrophyAwardPresentation(state);
    state.pauseMenu.close();
    if (state.localAiDriver != nullptr || state.localSandboxDriver != nullptr) {
        state.localAiDriver.reset();
        state.localSandboxDriver.reset();
#if defined(BASILISK_GAME_DEBUG)
        state.debugMapProvider.reset();
        state.debugInventoryMenu.close();
#endif
        state.ownedSession =
            std::make_unique<basilisk::game::ClientSessionController>();
        state.session = state.ownedSession.get();
        state.screenShellEnabled = false;
        state.view = AppView::MainMenu;
        state.mapActionMenu.dismiss();
        state.actionSelection = basilisk::game::ActionSelectionState{};
        return SDL_APP_CONTINUE;
    }
    if (state.networkSession == nullptr) return SDL_APP_SUCCESS;
    state.networkSession->clearGameplaySession();
    state.session = nullptr;
    state.mainMenu = basilisk::game::MainMenuState{};
    if (state.confirmedCosmeticLoadout.has_value()) {
        state.mainMenu.applyConfirmedCosmeticLoadout(
            *state.confirmedCosmeticLoadout);
    }
    state.screenShellEnabled = false;
    state.view = AppView::MainMenu;
    state.mapActionMenu.dismiss();
    state.actionSelection = basilisk::game::ActionSelectionState{};
    return SDL_APP_CONTINUE;
}

SDL_AppResult handlePauseResult(
    AppState& state,
    basilisk::game::PauseMenuResult result) {
    if (result == basilisk::game::PauseMenuResult::Resume) {
        state.pauseMenu.close();
    } else if (result == basilisk::game::PauseMenuResult::QuitGame &&
               state.session != nullptr && state.session->quit()) {
        return finishGameplayQuit(state);
    }
    return SDL_APP_CONTINUE;
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
#if defined(BASILISK_NATIVE_PRODUCTION_ENDPOINT)
    auto endpointConfig = basilisk::game::clientNetworkEndpointConfig(
        basilisk::game::ClientEndpointDefault::Production);
#else
    auto endpointConfig = basilisk::game::clientNetworkEndpointConfig(
        basilisk::game::ClientEndpointDefault::LocalDevelopment);
#endif
    for (int index = 1; index < argc; ++index) {
        if (argv == nullptr || argv[index] == nullptr) continue;
        const std::string_view argument{argv[index]};
        if (argument != "--connect" && argument != "--token") continue;
        if (index + 1 >= argc || argv[index + 1] == nullptr) {
            SDL_Log("%s requires a value", argv[index]);
            return SDL_APP_FAILURE;
        }
        if (argument == "--connect") {
            std::string endpointError;
            if (!basilisk::game::applyNetworkEndpointOption(
                    argument, argv[++index], endpointConfig, endpointError)) {
                SDL_Log("%s", endpointError.c_str());
                return SDL_APP_FAILURE;
            }
            connectUrl = endpointConfig.connectUrl;
        }
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
    state->serverUrl = connectUrl.value_or(endpointConfig.connectUrl);
    // Fixed-token development launches connect immediately. Stored account
    // sessions are restored below after SDL initialization; otherwise normal
    // launches remain offline at the Main Menu until Play Online is chosen.
    if (!connectToken.has_value() && !developmentLaunch) connectUrl.reset();
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
#if defined(BASILISK_GAME_DEBUG)
            basilisk::MatchSeed matchSeed{424242};
#endif
            for (int seedIndex = 1; seedIndex < argc; ++seedIndex) {
                if (argv[seedIndex] == nullptr) continue;
                const std::string_view seedOption{argv[seedIndex]};
                const bool mapSeedOption = seedOption == "--map-seed";
#if defined(BASILISK_GAME_DEBUG)
                const bool matchSeedOption = seedOption == "--match-seed";
                if (!mapSeedOption && !matchSeedOption) continue;
#else
                if (!mapSeedOption) continue;
#endif
                if (seedIndex + 1 >= argc || argv[seedIndex + 1] == nullptr) {
                    SDL_Log(
                        "%s requires an unsigned integer value",
                        argv[seedIndex]);
                    return SDL_APP_FAILURE;
                }
                const std::string_view value{argv[++seedIndex]};
                std::uint64_t parsed{};
                const auto result = std::from_chars(
                    value.data(), value.data() + value.size(), parsed);
                if (result.ec != std::errc{} ||
                    result.ptr != value.data() + value.size()) {
                    SDL_Log(
                        "Invalid %s value '%s'; expected an unsigned integer",
                        mapSeedOption ? "--map-seed" : "--match-seed",
                        argv[seedIndex]);
                    return SDL_APP_FAILURE;
                }
                if (mapSeedOption) mapSeed = parsed;
#if defined(BASILISK_GAME_DEBUG)
                else matchSeed = parsed;
#endif
            }

#if defined(BASILISK_GAME_DEBUG)
            auto debugSession =
                basilisk::game::LocalGameSessionAdapter::createDebug(
                    mapSeed,
                    matchSeed);
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
#if defined(BASILISK_GAME_DEBUG)
            SDL_Log(
                "Trusted local Core session enabled (map seed: %llu, match seed: %llu)",
                static_cast<unsigned long long>(mapSeed),
                static_cast<unsigned long long>(matchSeed));
#else
            SDL_Log(
                "Trusted local Core session enabled (map seed: %llu)",
                static_cast<unsigned long long>(mapSeed));
#endif
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
    if (basilisk::game::shouldAttemptStartupSessionRestore(
            developmentLaunch,
            connectToken.has_value(),
            basilisk::game::SessionTokenStorage::load().has_value())) {
        (void)beginOnlineAuthentication(*state, false, true);
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
            else if (event->key.key == SDLK_ESCAPE) {
                returnFromAuthenticationToStartGame(*state);
            }
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
            else if (basilisk::game::hitTest(state->authGeometry.back, point))
                returnFromAuthenticationToStartGame(*state);
        }
        return SDL_APP_CONTINUE;
    }

    if (state != nullptr && state->view == AppView::MainMenu) {
        if ((state->mainMenu.page() == basilisk::game::MainMenuPage::JoinLobby ||
             state->mainMenu.page() ==
                basilisk::game::MainMenuPage::JoinSandboxLobby) &&
            event->type == SDL_EVENT_TEXT_INPUT) {
            state->mainMenu.appendLobbyCode(event->text.text);
            return SDL_APP_CONTINUE;
        }
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            if ((state->mainMenu.page() ==
                    basilisk::game::MainMenuPage::JoinLobby ||
                 state->mainMenu.page() ==
                    basilisk::game::MainMenuPage::JoinSandboxLobby) &&
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
            if (event->key.key == SDLK_LEFT || event->key.key == SDLK_RIGHT) {
                state->mainMenu.adjustSelected(
                    event->key.key == SDLK_LEFT ? -1 : 1);
                return SDL_APP_CONTINUE;
            }
            if (event->key.key == SDLK_RETURN ||
                event->key.key == SDLK_KP_ENTER) {
                const auto result = state->mainMenu.activateSelected();
                if (state->mainMenu.page() ==
                        basilisk::game::MainMenuPage::JoinLobby ||
                    state->mainMenu.page() ==
                        basilisk::game::MainMenuPage::JoinSandboxLobby)
                    (void)SDL_StartTextInput(state->window);
                return handleMainMenuResult(*state, result);
            }
            if (event->key.key == SDLK_ESCAPE) {
                return handleMainMenuResult(*state, state->mainMenu.back());
            }
        }
        if (event->type == SDL_EVENT_MOUSE_MOTION ||
            (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
             event->button.button == SDL_BUTTON_LEFT)) {
            basilisk::game::PresentationPoint pointer;
            const float windowX = event->type == SDL_EVENT_MOUSE_MOTION
                ? event->motion.x : event->button.x;
            const float windowY = event->type == SDL_EVENT_MOUSE_MOTION
                ? event->motion.y : event->button.y;
            if (!pointerInRenderCoordinates(
                    state->renderer, windowX, windowY, pointer))
                return SDL_APP_FAILURE;
            const auto hit = basilisk::game::hitTestMainMenu(
                state->mainMenuGeometry, pointer);
            if (hit.has_value()) {
                const auto actions = state->mainMenu.actions();
                const auto found = std::find(actions.begin(), actions.end(), *hit);
                if (found != actions.end())
                    state->mainMenu.select(static_cast<std::size_t>(
                        found - actions.begin()));
            }
            if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                const auto callingCard =
                    basilisk::game::hitTestCallingCardGallery(
                        state->mainMenuGeometry, pointer);
                if (callingCard.has_value()) {
                    requestCosmeticLoadout(*state, {
                        *callingCard, state->mainMenu.selectedEmblem()});
                    return SDL_APP_CONTINUE;
                }
                const auto emblem = basilisk::game::hitTestEmblemGallery(
                    state->mainMenuGeometry, pointer);
                if (emblem.has_value()) {
                    requestCosmeticLoadout(*state, {
                        state->mainMenu.selectedCallingCard(), *emblem});
                    return SDL_APP_CONTINUE;
                }
            }
            if (hit.has_value() &&
                event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                const auto result = state->mainMenu.activateSelected();
                if (state->mainMenu.page() ==
                        basilisk::game::MainMenuPage::JoinLobby ||
                    state->mainMenu.page() ==
                        basilisk::game::MainMenuPage::JoinSandboxLobby)
                    (void)SDL_StartTextInput(state->window);
                return handleMainMenuResult(*state, result);
            }
        }
        return SDL_APP_CONTINUE;
    }

    const basilisk::PlayerRoundSnapshot* snapshot = state == nullptr ||
            state->session == nullptr
        ? nullptr
        : state->session->displayedSnapshot();
    const bool lifecycleModalActive = snapshot != nullptr &&
        basilisk::game::lifecycleModalPresentation(
            *snapshot,
            state->session->viewContext(),
            state->session->profiles(),
            state->session->matchMode()).has_value();

    if (state != nullptr && state->session != nullptr &&
        state->session->activeClash().has_value()) {
        (void)SDL_StartTextInput(state->window);
        if (event->type == SDL_EVENT_TEXT_INPUT) {
            if (state->clashInput.size() + std::char_traits<char>::length(event->text.text) <= 64)
                state->clashInput += event->text.text;
        } else if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            if (event->key.key == SDLK_BACKSPACE && !state->clashInput.empty())
                state->clashInput.pop_back();
            else if ((event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER) &&
                     !state->clashInput.empty()) {
                if (state->session->submitClashResponse(state->clashInput))
                    state->clashInput.clear();
            }
        }
        return SDL_APP_CONTINUE;
    }

    if (lifecycleModalActive && state != nullptr) {
        state->pauseMenu.close();
#if defined(BASILISK_GAME_DEBUG)
        state->debugInventoryMenu.close();
        state->debugKillMenu.close();
#endif
    }

    if (state != nullptr && state->pauseMenu.active()) {
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            if (event->key.key == SDLK_ESCAPE) {
                state->pauseMenu.close();
            } else if (event->key.key == SDLK_UP) {
                state->pauseMenu.moveSelection(-1);
            } else if (event->key.key == SDLK_DOWN) {
                state->pauseMenu.moveSelection(1);
            } else if (event->key.key == SDLK_RETURN ||
                       event->key.key == SDLK_KP_ENTER) {
                return handlePauseResult(
                    *state, state->pauseMenu.activateSelected());
            }
        } else if (event->type == SDL_EVENT_MOUSE_MOTION ||
                   (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event->button.button == SDL_BUTTON_LEFT)) {
            basilisk::game::PresentationPoint pointer;
            const float windowX = event->type == SDL_EVENT_MOUSE_MOTION
                ? event->motion.x : event->button.x;
            const float windowY = event->type == SDL_EVENT_MOUSE_MOTION
                ? event->motion.y : event->button.y;
            if (!pointerInRenderCoordinates(
                    state->renderer, windowX, windowY, pointer))
                return SDL_APP_FAILURE;
            const auto hit = basilisk::game::hitTestPauseMenu(
                state->pauseMenuGeometry, pointer);
            if (hit.has_value()) state->pauseMenu.select(*hit);
            if (hit.has_value() &&
                event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                return handlePauseResult(
                    *state, state->pauseMenu.activateSelected());
            }
        }
        return SDL_APP_CONTINUE;
    }

#if defined(BASILISK_GAME_DEBUG)
    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !lifecycleModalActive && !event->key.repeat && event->key.key == SDLK_F1) {
        if (state->debugMapProvider == nullptr) {
            SDL_Log("Debug map reveal requires a local debug match");
        } else {
            state->debugMapReveal.toggle();
            SDL_Log(
                "Debug map reveal %s",
                state->debugMapReveal.revealed() ? "enabled" : "disabled");
        }
        return SDL_APP_CONTINUE;
    }
    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !lifecycleModalActive && !event->key.repeat && event->key.key == SDLK_F2) {
        if (state->debugMapProvider == nullptr) {
            SDL_Log("Debug gameplay truth requires a local debug match");
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
        !lifecycleModalActive && !event->key.repeat && event->key.key == SDLK_F3) {
        if (state->debugMapProvider == nullptr) {
            SDL_Log("Debug Basilisk behavior control requires a local debug match");
        } else if (!state->debugMapProvider->cycleBasiliskBehavior()) {
            SDL_Log("Unable to cycle debug Basilisk behavior");
        } else {
            SDL_Log("Debug Basilisk behavior cycled");
        }
        return SDL_APP_CONTINUE;
    }
    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !lifecycleModalActive && !event->key.repeat && event->key.key == SDLK_F4) {
        if (state->debugMapProvider == nullptr) {
            SDL_Log("Debug inventory requires a local debug match");
        } else {
            state->debugKillMenu.close();
            state->debugInventoryMenu.toggle(
                state->debugMapProvider->participants(),
                state->session != nullptr &&
                    state->session->matchMode() == basilisk::client::MatchMode::Sandbox);
        }
        return SDL_APP_CONTINUE;
    }
    if (state != nullptr && state->debugInventoryMenu.active() &&
        !(event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
          event->key.key == SDLK_F5)) {
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            if (event->key.key == SDLK_ESCAPE) {
                state->debugInventoryMenu.close();
            } else if (event->key.key == SDLK_UP) {
                state->debugInventoryMenu.moveSelection(-1);
            } else if (event->key.key == SDLK_DOWN) {
                state->debugInventoryMenu.moveSelection(1);
            } else if (event->key.key == SDLK_RETURN ||
                       event->key.key == SDLK_KP_ENTER) {
                const auto grant = state->debugInventoryMenu.activate();
                if (!grant.has_value()) return SDL_APP_CONTINUE;
                if (state->debugMapProvider == nullptr ||
                    !state->debugMapProvider->grantItem(
                        grant->first, grant->second)) {
                    SDL_Log("Debug item grant failed (inventory may be full)");
                } else {
                    SDL_Log("Debug item granted");
                }
            }
        }
        return SDL_APP_CONTINUE;
    }
    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !lifecycleModalActive && !event->key.repeat && event->key.key == SDLK_F5) {
        if (state->debugMapProvider == nullptr ||
            !state->debugMapProvider->killControlAvailable()) {
            SDL_Log("Debug kill control requires a Play AI debug match");
        } else {
            state->debugInventoryMenu.close();
            state->debugKillMenu.toggle(state->debugMapProvider->participants());
        }
        return SDL_APP_CONTINUE;
    }
    if (state != nullptr && state->debugKillMenu.active()) {
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            if (event->key.key == SDLK_ESCAPE) {
                state->debugKillMenu.close();
            } else if (event->key.key == SDLK_UP) {
                state->debugKillMenu.moveSelection(-1);
            } else if (event->key.key == SDLK_DOWN) {
                state->debugKillMenu.moveSelection(1);
            } else if (event->key.key == SDLK_RETURN ||
                       event->key.key == SDLK_KP_ENTER) {
                if (state->debugMapProvider == nullptr ||
                    !state->debugMapProvider->killPlayer(
                        state->debugKillMenu.selectedPlayer())) {
                    SDL_Log("Debug kill failed");
                } else {
                    state->debugKillMenu.close();
                    SDL_Log("Debug hunter killed through authoritative elimination");
                }
            }
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

    if (state != nullptr && event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
        state->hoveredActionIndex.reset();
        basilisk::game::updateMapHover(
            state->mapPresentation,
            basilisk::game::MapHitTarget{});
        return SDL_APP_CONTINUE;
    }

    if (state != nullptr && event->type == SDL_EVENT_KEY_DOWN &&
        !event->key.repeat && event->key.key == SDLK_ESCAPE) {
        if (!lifecycleModalActive) {
            state->mapActionMenu.dismiss();
            state->hoveredActionIndex.reset();
            basilisk::game::updateMapHover(
                state->mapPresentation,
                basilisk::game::MapHitTarget{});
            state->pauseMenu.open();
        }
        return SDL_APP_CONTINUE;
    }

    if (state != nullptr && state->session != nullptr && snapshot != nullptr &&
        !lifecycleModalActive &&
        state->session->viewContext().mode ==
            basilisk::client::ClientViewMode::Spectating &&
        state->session->matchMode() == basilisk::client::MatchMode::Sandbox &&
        event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
        (event->key.key == SDLK_LEFT || event->key.key == SDLK_UP ||
         event->key.key == SDLK_RIGHT || event->key.key == SDLK_DOWN)) {
        const int direction = event->key.key == SDLK_LEFT || event->key.key == SDLK_UP
            ? -1 : 1;
        if (state->session->cycleSpectatedPlayer(direction)) {
            state->mapActionMenu.dismiss();
            state->hoveredActionIndex.reset();
            state->mapPresentation = {};
            state->mapLayout = {};
            SDL_Log("Sandbox now watching player %llu",
                static_cast<unsigned long long>(
                    state->session->viewContext().viewedPlayer));
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
        if (snapshot == nullptr) return SDL_APP_CONTINUE;
        basilisk::game::PresentationPoint pointer;
        const float windowX = event->type == SDL_EVENT_MOUSE_MOTION
            ? event->motion.x
            : event->type == SDL_EVENT_MOUSE_WHEEL
                ? event->wheel.mouse_x
                : event->button.x;
        const float windowY = event->type == SDL_EVENT_MOUSE_MOTION
            ? event->motion.y
            : event->type == SDL_EVENT_MOUSE_WHEEL
                ? event->wheel.mouse_y
                : event->button.y;
        if (!basilisk::game::tryGameplayPointerCoordinates(
                windowX,
                windowY,
                pointer,
                [renderer = state->renderer](
                    float x, float y, float& renderX, float& renderY) {
                    return SDL_RenderCoordinatesFromWindow(
                        renderer, x, y, &renderX, &renderY);
                })) {
            SDL_Log(
                "Unable to convert gameplay pointer coordinates: %s",
                SDL_GetError());
            return SDL_APP_CONTINUE;
        }
        if (event->type == SDL_EVENT_MOUSE_WHEEL) {
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
                    return finishGameplayQuit(*state);
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

    if (state->localAiDriver != nullptr) {
        const Uint64 now = SDL_GetTicks();
        state->localAiDriver->advance(now - state->localAiLastTick);
        state->localAiLastTick = now;
    }
    if (state->localSandboxDriver != nullptr) {
        const Uint64 now = SDL_GetTicks();
        state->localSandboxDriver->advance(now - state->localAiLastTick);
        state->localAiLastTick = now;
    }

    if (state->networkSession != nullptr) {
        state->networkSession->pump();
        if (state->mainMenu.page() ==
                basilisk::game::MainMenuPage::Leaderboards &&
            state->mainMenu.leaderboardLoadState() ==
                basilisk::game::LeaderboardLoadState::Loading) {
            const auto& page = state->networkSession->leaderboardPage();
            if (page.has_value() &&
                page->offset == state->mainMenu.leaderboardOffset()) {
                state->mainMenu.leaderboardLoaded(
                    page->entries.size() ==
                    basilisk::game::MainMenuState::leaderboardPageSize);
            }
        }
        if (!localGameplayActive(*state))
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
                state->awaitingEligibleMatchStart = true;
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
            } else if (const auto* sandbox = std::get_if<
                    basilisk::game::network::SandboxLobbyUpdated>(
                        &response.payload)) {
                state->mainMenu.sandboxLobbyUpdated(*sandbox);
                (void)SDL_StopTextInput(state->window);
            } else if (std::holds_alternative<
                    basilisk::game::network::SandboxLobbyClosed>(
                        response.payload)) {
                state->mainMenu.sandboxLobbyClosed("Sandbox lobby closed.");
            }
        }
        if (state->view == AppView::MainMenu &&
            basilisk::game::hasAuthoritativeGameplaySession(state->session)) {
            state->trophyAward.reset();
            state->trophyAwardPresented = false;
            const auto* restoredSnapshot =
                state->session->displayedSnapshot();
            const bool activeEligibleMatch =
                state->session->matchMode() ==
                    basilisk::client::MatchMode::Online &&
                restoredSnapshot != nullptr &&
                restoredSnapshot->matchStatus == basilisk::MatchStatus::Active;
            if ((state->awaitingEligibleMatchStart || activeEligibleMatch) &&
                state->session->matchMode() ==
                    basilisk::client::MatchMode::Online) {
                state->trophyMatchStartTotal = state->session->trophyTotal();
            } else {
                state->trophyMatchStartTotal.reset();
            }
            state->awaitingEligibleMatchStart = false;
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
                state->mainMenu = basilisk::game::MainMenuState{};
                state->handledLobbyResponseRevision =
                    state->networkSession->lobbyResponseRevision();
                state->cancelLobbyWhenHosted = false;
                state->authenticatedProfile = success->profile;
                state->confirmedCosmeticLoadout = success->cosmeticLoadout;
                state->mainMenu.applyConfirmedCosmeticLoadout(
                    *state->confirmedCosmeticLoadout);
                state->authenticatedSessionToken = success->sessionToken;
                std::string storageError;
                if (!basilisk::game::SessionTokenStorage::save(
                        success->sessionToken, storageError))
                    SDL_Log("Unable to save session: %s", storageError.c_str());
                state->restoringSession = false;
                state->authScreen.setWaiting(false);
                if (state->enterOnlineAfterAuthentication) {
                    (void)state->mainMenu.activate(
                        basilisk::game::MainMenuAction::StartGame);
                    state->mainMenu.openOnline();
                }
                state->enterOnlineAfterAuthentication = false;
                state->startupSessionRestore = false;
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
                    state->authScreen.setError(failure->message);
                } else {
                    state->authScreen.setError(failure->message);
                }
                if (state->startupSessionRestore) {
                    finishStartupSessionRestoreAtMainMenu(*state);
                }
            } else if (std::holds_alternative<
                           basilisk::game::network::LogoutSuccess>(
                               response.payload)) {
                state->authScreen.setWaiting(false);
            }
        }
        if (state->networkSession->cosmeticLoadoutResponseRevision() !=
                state->handledCosmeticLoadoutResponseRevision) {
            state->handledCosmeticLoadoutResponseRevision =
                state->networkSession->cosmeticLoadoutResponseRevision();
            const auto& response =
                state->networkSession->cosmeticLoadoutResponse();
            if (response.has_value()) {
                if (const auto* success = std::get_if<
                        basilisk::game::network::CosmeticLoadoutUpdateSuccess>(
                            &response->payload)) {
                    state->confirmedCosmeticLoadout = success->loadout;
                    state->mainMenu.applyConfirmedCosmeticLoadout(
                        *state->confirmedCosmeticLoadout);
                } else if (const auto* failure = std::get_if<
                        basilisk::game::network::CosmeticLoadoutUpdateFailure>(
                            &response->payload)) {
                    SDL_Log("Cosmetic update rejected: %s",
                        failure->message.c_str());
                }
            }
        }
        if (!state->networkFailureLogged &&
            (state->networkSession->state() ==
                 basilisk::game::NetworkConnectionState::Error ||
             state->networkSession->state() ==
                 basilisk::game::NetworkConnectionState::Disconnected)) {
            SDL_Log("Network session ended: %s",
                state->networkSession->error().c_str());
            const bool startupRestoreFailed =
                state->view == AppView::Authentication &&
                state->startupSessionRestore;
            if (state->view == AppView::Authentication) {
                if (state->startupSessionRestore) {
                    finishStartupSessionRestoreAtMainMenu(*state);
                } else {
                    state->authScreen.setError(state->networkSession->error());
                }
            }
            if (state->view == AppView::MainMenu && !startupRestoreFailed) {
                std::string error = state->networkSession->error();
                if (error.empty()) error = "Connection to the server was lost.";
                state->mainMenu.connectionLost(std::move(error));
            }
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
                    state->renderer, *state->textRenderer, *state->svgTextures,
                    state->authScreen, state->authGeometry,
                    outputWidth, outputHeight, authError)) {
                SDL_Log("Authentication rendering failed: %s", authError.c_str());
                return SDL_APP_FAILURE;
            }
        } else if (state->view == AppView::MainMenu) {
            std::string menuError;
            const std::optional<std::int64_t> trophies =
                state->networkSession == nullptr
                ? std::nullopt
                : (state->session == nullptr
                    ? state->lastKnownTrophyTotal
                    : std::optional<std::int64_t>{
                        state->session->trophyTotal()});
            static const std::optional<
                basilisk::game::network::LeaderboardPageResponse>
                noLeaderboard;
            const auto& leaderboard = state->networkSession == nullptr
                ? noLeaderboard
                : state->networkSession->leaderboardPage();
            if (!basilisk::game::renderMainMenu(
                    state->renderer,
                    *state->textRenderer,
                    *state->svgTextures,
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
          if (snapshot != nullptr && state->session != nullptr) {
              if (state->session->matchMode() ==
                  basilisk::client::MatchMode::Online) {
                  state->lastKnownTrophyTotal = state->session->trophyTotal();
              }
              const auto award = basilisk::game::terminalTrophyAward(
                  state->session->matchMode(), snapshot->matchStatus,
                  state->trophyMatchStartTotal,
                  state->session->trophyTotal(),
                  state->trophyAwardPresented);
              if (award.has_value()) {
                  state->trophyAward = award;
                  state->trophyAwardPresented = true;
              }
          }
          const bool blockingLifecycle = snapshot != nullptr && state->session != nullptr &&
              basilisk::game::lifecycleModalPresentation(
                  *snapshot, state->session->viewContext(), state->session->profiles(),
                  state->session->matchMode()).has_value();
          if (state->session != nullptr && state->session->activeClash().has_value()) {
              state->pauseMenu.close();
#if defined(BASILISK_GAME_DEBUG)
              state->debugInventoryMenu.close();
              state->debugKillMenu.close();
#endif
          } else if (blockingLifecycle) {
              state->pauseMenu.close();
#if defined(BASILISK_GAME_DEBUG)
              state->debugInventoryMenu.close();
              state->debugKillMenu.close();
#endif
          }
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
                    state->trophyAward,
#if defined(BASILISK_GAME_DEBUG)
                    state->debugMapProvider == nullptr
                        ? nullptr
                        : &state->debugMapProvider->mapTruth(),
                    debugGameplayTruth.has_value()
                        ? &*debugGameplayTruth
                        : nullptr,
                    state->debugMapReveal.revealed(),
                    state->debugGameplayReveal.revealed(),
                    state->debugInventoryMenu.active(),
                    state->debugKillMenu.active(),
                    state->debugMapProvider->killControlAvailable(),
#endif
                    outputWidth,
                    outputHeight,
                    screenError)) {
                SDL_Log("Screen shell rendering failed: %s", screenError.c_str());
                return SDL_APP_FAILURE;
            }

            if (state->networkSession != nullptr &&
                !localGameplayActive(*state)) {
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

    if (state->pauseMenu.active()) {
        std::string pauseError;
        if (!basilisk::game::renderPauseMenu(
                state->renderer,
                *state->textRenderer,
                state->pauseMenu,
                state->pauseMenuGeometry,
                outputWidth,
                outputHeight,
                pauseError)) {
            SDL_Log("Pause menu rendering failed: %s", pauseError.c_str());
            return SDL_APP_FAILURE;
        }
    }

    if (state->session != nullptr && state->session->activeClash().has_value()) {
        std::string clashError;
        if (!basilisk::game::renderClashQte(state->renderer, *state->textRenderer,
                *state->session->activeClash(), state->clashInput,
                outputWidth, outputHeight, clashError)) {
            SDL_Log("Clash QTE rendering failed: %s", clashError.c_str());
            return SDL_APP_FAILURE;
        }
    } else if (!state->clashInput.empty()) {
        state->clashInput.clear();
    }

#if defined(BASILISK_GAME_DEBUG)
    if (state->debugInventoryMenu.active()) {
        std::string inventoryError;
        if (!basilisk::game::debug::renderDebugInventoryMenu(
                state->renderer,
                *state->textRenderer,
                state->debugInventoryMenu,
                outputWidth,
                outputHeight,
                inventoryError)) {
            SDL_Log(
                "Debug inventory menu rendering failed: %s",
                inventoryError.c_str());
            return SDL_APP_FAILURE;
        }
    }
    if (state->debugKillMenu.active()) {
        std::string killError;
        if (!basilisk::game::debug::renderDebugKillMenu(
                state->renderer, *state->textRenderer, state->debugKillMenu,
                outputWidth, outputHeight, killError)) {
            SDL_Log("Debug kill menu rendering failed: %s", killError.c_str());
            return SDL_APP_FAILURE;
        }
    }
#endif

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
