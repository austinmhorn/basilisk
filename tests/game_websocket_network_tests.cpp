#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <ixwebsocket/IXGetFreePort.h>

#include "LocalWebSocketMatchServer.hpp"
#include "WebSocketNetworkSession.hpp"

using namespace basilisk;
using namespace basilisk::game;
using namespace basilisk::game::server;

namespace {

std::vector<client::PublicPlayerProfile> profiles() {
    return {
        {PlayerId{1}, "Mara Voss", client::CallingCardId{"ember-field"},
         client::EmblemId{"wayfinder"}},
        {PlayerId{2}, "Elias Thorn", client::CallingCardId{"blue-ward"},
         client::EmblemId{"ward"}},
    };
}

bool waitUntil(const std::function<bool()>& ready, int milliseconds = 5000) {
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 5) {
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ready();
}

const AvailableAction& searchAction(const ClientSessionController& controller) {
    const auto* snapshot = controller.displayedSnapshot();
    assert(snapshot != nullptr);
    const auto found = std::find_if(
        snapshot->availableActions.begin(), snapshot->availableActions.end(),
        [](const AvailableAction& action) {
            return action.type == ActionType::Search;
        });
    assert(found != snapshot->availableActions.end());
    return *found;
}

std::unique_ptr<LocalWebSocketMatchServer> startServer() {
    std::string error;
    for (int attempt = 0; attempt < 10; ++attempt) {
        LocalWebSocketServerConfig config;
        config.port = static_cast<std::uint16_t>(ix::getFreePort());
        config.p1Token = "test-p1";
        config.p2Token = "test-p2";
        config.profiles = profiles();
        auto server = LocalWebSocketMatchServer::start(std::move(config), error);
        if (server != nullptr) return server;
    }
    std::fprintf(stderr, "Unable to start loopback server: %s\n", error.c_str());
    assert(false && "Unable to reserve a loopback WebSocket test port");
    return nullptr;
}

std::string url(const LocalWebSocketMatchServer& server) {
    return "ws://127.0.0.1:" + std::to_string(server.port());
}

void twoRealWebSocketsAdvanceOneAuthoritativeRound() {
    auto server = startServer();
    std::string error;
    auto p1 = WebSocketNetworkSession::connect(url(*server), "test-p1", error);
    auto p2 = WebSocketNetworkSession::connect(url(*server), "test-p2", error);
    assert(p1 != nullptr && p2 != nullptr);
    assert(waitUntil([&] {
        p1->pump();
        p2->pump();
        return p1->controller() != nullptr && p2->controller() != nullptr;
    }));

    assert(p1->controller()->viewContext().localPlayer == PlayerId{1});
    assert(p2->controller()->viewContext().localPlayer == PlayerId{2});
    assert(p1->controller()->displayedSnapshot()->player == PlayerId{1});
    assert(p2->controller()->displayedSnapshot()->player == PlayerId{2});
    assert(p1->controller()->submitAndLock(searchAction(*p1->controller())));
    assert(p2->controller()->submitAndLock(searchAction(*p2->controller())));

    assert(waitUntil([&] {
        p1->pump();
        p2->pump();
        return p1->controller()->displayedSnapshot()->round == RoundNumber{2} &&
               p2->controller()->displayedSnapshot()->round == RoundNumber{2};
    }));
    assert(server->authoritativeRound() == RoundNumber{2});
    assert(server->resolvedRoundCount() == 1);
}

void invalidAndDuplicateTokensAreRejected() {
    auto server = startServer();
    std::string error;
    auto p1 = WebSocketNetworkSession::connect(url(*server), "test-p1", error);
    assert(p1 != nullptr);
    assert(waitUntil([&] {
        p1->pump();
        return p1->controller() != nullptr;
    }));

    auto duplicate = WebSocketNetworkSession::connect(
        url(*server), "test-p1", error);
    auto invalid = WebSocketNetworkSession::connect(
        url(*server), "not-a-token", error);
    assert(duplicate != nullptr && invalid != nullptr);
    assert(waitUntil([&] {
        duplicate->pump();
        invalid->pump();
        return duplicate->state() == NetworkConnectionState::Disconnected &&
               invalid->state() == NetworkConnectionState::Disconnected;
    }));
    assert(duplicate->controller() == nullptr);
    assert(invalid->controller() == nullptr);
}

void disconnectUsesExistingMatchLifecycle() {
    auto server = startServer();
    std::string error;
    auto p1 = WebSocketNetworkSession::connect(url(*server), "test-p1", error);
    auto p2 = WebSocketNetworkSession::connect(url(*server), "test-p2", error);
    assert(waitUntil([&] {
        p1->pump();
        p2->pump();
        return p1->controller() != nullptr && p2->controller() != nullptr;
    }));

    p2.reset();
    assert(waitUntil([&] { return server->connectedClientCount() == 1; }));
    assert(server->processedDisconnectCount() == 1);
    server->advanceTime(30'000);
    p1->pump();
    assert(p1->controller()->displayedSnapshot()->alive);
}

} // namespace

int main() {
    twoRealWebSocketsAdvanceOneAuthoritativeRound();
    invalidAndDuplicateTokensAreRejected();
    disconnectUsesExistingMatchLifecycle();
}
