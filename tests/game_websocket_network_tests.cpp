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
#include <ixwebsocket/IXWebSocketServer.h>

#include "AuthoritativeInMemoryMatch.hpp"
#include "LocalWebSocketMatchServer.hpp"
#if defined(_WIN32)
#include "NativeNetworkRuntime.hpp"
#endif
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

struct IncompatibleServer {
    std::unique_ptr<ix::WebSocketServer> socket;
    std::uint16_t port{};

    ~IncompatibleServer() {
        if (socket != nullptr) socket->stop();
    }
};

std::unique_ptr<IncompatibleServer> startIncompatibleServer() {
    std::string error;
    auto match = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error);
    assert(match != nullptr);
    auto endpoint = match->connect(PlayerId{1}, error);
    assert(endpoint != nullptr);
    auto frame = endpoint->takeNextServerFrame();
    assert(frame.has_value() && frame->size() > 8);
    (*frame)[8] = 2; // V1's big-endian version header becomes unsupported V2.

    for (int attempt = 0; attempt < 10; ++attempt) {
        const auto port = static_cast<std::uint16_t>(ix::getFreePort());
        auto socket = std::make_unique<ix::WebSocketServer>(
            port, "127.0.0.1", 5, 1);
        socket->disablePerMessageDeflate();
        socket->setOnClientMessageCallback(
            [bytes = *frame](std::shared_ptr<ix::ConnectionState>,
                             ix::WebSocket& client,
                             const ix::WebSocketMessagePtr& message) {
                if (message->type != ix::WebSocketMessageType::Open) return;
                const std::string payload(
                    reinterpret_cast<const char*>(bytes.data()), bytes.size());
                (void)client.sendBinary(payload);
            });
        const auto listening = socket->listen();
        if (!listening.first) continue;
        socket->start();
        auto result = std::make_unique<IncompatibleServer>();
        result->socket = std::move(socket);
        result->port = port;
        return result;
    }
    assert(false && "Unable to reserve an incompatible-server test port");
    return nullptr;
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
    assert(duplicate != nullptr);
    assert(waitUntil([&] {
        duplicate->pump();
        return duplicate->state() == NetworkConnectionState::Error;
    }));
    assert(duplicate->controller() == nullptr);

    auto invalid = WebSocketNetworkSession::connect(
        url(*server), "not-a-token", error);
    assert(invalid != nullptr);
    assert(waitUntil([&] {
        invalid->pump();
        return invalid->state() == NetworkConnectionState::Error;
    }));
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

void incompatibleProtocolIsReportedExplicitly() {
    auto server = startIncompatibleServer();
    std::string error;
    auto session = WebSocketNetworkSession::connect(
        "ws://127.0.0.1:" + std::to_string(server->port), "test-p1", error);
    assert(session != nullptr);
    assert(waitUntil([&] {
        session->pump();
        return session->state() == NetworkConnectionState::Error;
    }));
    assert(session->controller() == nullptr);
    assert(session->error() ==
           "Protocol compatibility error: Unsupported Basilisk network "
           "protocol version.");
}

void unavailableServerIsAnInitialConnectionFailure() {
    const auto unusedPort = static_cast<std::uint16_t>(ix::getFreePort());
    std::string error;
    auto session = WebSocketNetworkSession::connect(
        "ws://127.0.0.1:" + std::to_string(unusedPort), "test-p1", error);
    assert(session != nullptr);
    assert(waitUntil([&] {
        session->pump();
        return session->state() == NetworkConnectionState::Error;
    }));
    assert(session->controller() == nullptr);
    assert(session->error().starts_with(
        "Unable to connect to the Basilisk server:"));
}

void establishedSessionReportsConnectionLoss() {
    auto server = startServer();
    std::string error;
    auto session = WebSocketNetworkSession::connect(
        url(*server), "test-p1", error);
    assert(session != nullptr);
    assert(waitUntil([&] {
        session->pump();
        return session->controller() != nullptr;
    }));
    const ClientSessionController* controller = session->controller();

    server->stop();
    assert(waitUntil([&] {
        session->pump();
        return session->state() == NetworkConnectionState::Disconnected;
    }));
    assert(session->controller() == controller);
    assert(session->error().starts_with(
        "Connection to the Basilisk server was lost:"));
}

void callbacksAfterClientCloseAreIgnored() {
    auto server = startServer();
    std::string error;
    auto session = WebSocketNetworkSession::connect(
        url(*server), "test-p1", error);
    assert(session != nullptr);
    assert(waitUntil([&] {
        session->pump();
        return session->controller() != nullptr;
    }));
    const ClientSessionController* controller = session->controller();
    const RoundNumber round = controller->displayedSnapshot()->round;

    session->close();
    const std::string closedError = session->error();
    server->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    session->pump();

    assert(session->state() == NetworkConnectionState::Disconnected);
    assert(session->error() == closedError);
    assert(session->error() == "Connection closed by client.");
    assert(session->controller() == controller);
    assert(controller->displayedSnapshot()->round == round);
}

} // namespace

int main() {
#if defined(_WIN32)
    std::string networkError;
    auto networkRuntime = NativeNetworkRuntime::acquire(networkError);
    assert(networkRuntime != nullptr && networkError.empty());
#endif
    twoRealWebSocketsAdvanceOneAuthoritativeRound();
    invalidAndDuplicateTokensAreRejected();
    disconnectUsesExistingMatchLifecycle();
    incompatibleProtocolIsReportedExplicitly();
    unavailableServerIsAnInitialConnectionFailure();
    establishedSessionReportsConnectionLoss();
    callbacksAfterClientCloseAreIgnored();
}
