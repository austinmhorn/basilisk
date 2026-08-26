#include "WebSocketNetworkSession.hpp"

#include <cctype>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <span>
#include <sstream>
#include <string_view>
#include <utility>

#include "NetworkGameSessionAdapter.hpp"
#include "NetworkWireCodec.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten/websocket.h>
#else
#if defined(_WIN32)
#include "NativeNetworkRuntime.hpp"
#endif

#include <ixwebsocket/IXWebSocket.h>
#endif

namespace basilisk::game {
namespace {

constexpr std::size_t kMaximumFrameBytes = 1U << 20;

std::string percentEncode(std::string_view value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char byte : value) {
        if (std::isalnum(byte) != 0 || byte == '-' || byte == '_' ||
            byte == '.' || byte == '~') {
            encoded.push_back(static_cast<char>(byte));
        } else {
            encoded.push_back('%');
            encoded.push_back(digits[byte >> 4U]);
            encoded.push_back(digits[byte & 0x0FU]);
        }
    }
    return encoded;
}

std::string authenticatedUrl(std::string url, const std::string& token) {
    url += url.find('?') == std::string::npos ? "?token=" : "&token=";
    url += percentEncode(token);
    return url;
}

struct SharedSocketState {
    mutable std::mutex mutex;
    NetworkConnectionState state{NetworkConnectionState::Connecting};
    std::string error;
    std::deque<network::WireBytes> frames;
    std::function<bool(std::span<const std::uint8_t>)> sendBinary;
    bool sessionEstablished{false};
    bool closed{false};
    bool shutdownRequested{false};
};

class WebSocketClientTransport final : public network::ClientTransport {
public:
    explicit WebSocketClientTransport(std::shared_ptr<SharedSocketState> socket)
        : socket_(std::move(socket)) {}

    [[nodiscard]] bool send(const network::ClientCommand& command) override {
        network::WireBytes bytes;
        std::string error;
        if (!network::encodeWire(command, bytes, error)) return false;
        std::lock_guard lock(socket_->mutex);
        if (socket_->closed ||
            socket_->state != NetworkConnectionState::Connected ||
            !socket_->sendBinary) return false;
        return socket_->sendBinary(bytes);
    }

private:
    std::shared_ptr<SharedSocketState> socket_;
};

} // namespace

class WebSocketNetworkSession::Impl {
public:
#if defined(_WIN32)
    Impl(
        std::string url,
        std::string token,
        std::unique_ptr<NativeNetworkRuntime> networkRuntime)
        : networkRuntime_(std::move(networkRuntime)),
          socketState_(std::make_shared<SharedSocketState>()),
          transport_(std::make_shared<WebSocketClientTransport>(socketState_)),
          url_(authenticatedUrl(std::move(url), token)) {}
    Impl(std::string url, std::unique_ptr<NativeNetworkRuntime> networkRuntime)
        : networkRuntime_(std::move(networkRuntime)),
          socketState_(std::make_shared<SharedSocketState>()),
          transport_(std::make_shared<WebSocketClientTransport>(socketState_)),
          url_(std::move(url)), authenticationMode_(true) {}
#else
    Impl(std::string url, std::string token)
        : socketState_(std::make_shared<SharedSocketState>()),
          transport_(std::make_shared<WebSocketClientTransport>(socketState_)),
          url_(authenticatedUrl(std::move(url), token)) {}
    explicit Impl(std::string url)
        : socketState_(std::make_shared<SharedSocketState>()),
          transport_(std::make_shared<WebSocketClientTransport>(socketState_)),
          url_(std::move(url)), authenticationMode_(true) {}
#endif

    ~Impl() { stop(); }

    bool start(std::string& error) {
#if defined(__EMSCRIPTEN__)
        if (!emscripten_websocket_is_supported()) {
            error = "This browser does not support WebSockets.";
            return false;
        }
        EmscriptenWebSocketCreateAttributes attributes;
        emscripten_websocket_init_create_attributes(&attributes);
        attributes.url = url_.c_str();
        attributes.createOnMainThread = true;
        socket_ = emscripten_websocket_new(&attributes);
        if (socket_ <= 0) {
            error = "Unable to create browser WebSocket.";
            return false;
        }
        emscripten_websocket_set_onopen_callback(socket_, this, &onOpen);
        emscripten_websocket_set_onmessage_callback(socket_, this, &onMessage);
        emscripten_websocket_set_onerror_callback(socket_, this, &onError);
        emscripten_websocket_set_onclose_callback(socket_, this, &onClose);
        socketState_->sendBinary = [this](std::span<const std::uint8_t> bytes) {
            if (bytes.size() > UINT32_MAX) return false;
            return emscripten_websocket_send_binary(
                socket_, const_cast<std::uint8_t*>(bytes.data()),
                static_cast<std::uint32_t>(bytes.size())) == EMSCRIPTEN_RESULT_SUCCESS;
        };
#else
        socket_.disableAutomaticReconnection();
        socket_.disablePerMessageDeflate();
        socket_.setUrl(url_);
        socket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
            handleNativeMessage(message);
        });
        socketState_->sendBinary = [this](std::span<const std::uint8_t> bytes) {
            const std::string payload(
                reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return socket_.sendBinary(payload).success;
        };
        socket_.start();
#endif
        error.clear();
        return true;
    }

    void stop() {
        {
            std::lock_guard lock(socketState_->mutex);
            if (!socketState_->closed) {
                socketState_->closed = true;
                socketState_->state = NetworkConnectionState::Disconnected;
                socketState_->error = "Connection closed by client.";
                socketState_->frames.clear();
                socketState_->sendBinary = {};
            }
        }
        shutdownSocket();
    }

    void shutdownSocket() {
        if (socketStopped_) return;
        socketStopped_ = true;
#if defined(__EMSCRIPTEN__)
        if (socket_ > 0) {
            emscripten_websocket_set_onopen_callback(socket_, nullptr, nullptr);
            emscripten_websocket_set_onmessage_callback(socket_, nullptr, nullptr);
            emscripten_websocket_set_onerror_callback(socket_, nullptr, nullptr);
            emscripten_websocket_set_onclose_callback(socket_, nullptr, nullptr);
            emscripten_websocket_close(socket_, 1000, "Client shutdown");
            emscripten_websocket_delete(socket_);
            socket_ = 0;
        }
#else
        // IXWebSocket invokes its callback on the socket worker. Replacing the
        // std::function before joining that worker races with an in-flight
        // callback and can corrupt the callback object during teardown.
        socket_.stop();
        socket_.setOnMessageCallback(nullptr);
#endif
    }

    void pump() {
        std::deque<network::WireBytes> frames;
        bool shutdownRequested = false;
        {
            std::lock_guard lock(socketState_->mutex);
            if (socketState_->closed) {
                socketState_->frames.clear();
                if (socketState_->state == NetworkConnectionState::Error ||
                    socketState_->state == NetworkConnectionState::Disconnected)
                    socketState_->shutdownRequested = true;
            }
            shutdownRequested = socketState_->shutdownRequested;
            frames.swap(socketState_->frames);
        }
        if (shutdownRequested) {
            shutdownSocket();
            return;
        }
        for (network::WireBytes& frame : frames) {
            std::string decodeError;
            network::WireMessageType frameType{};
            if (network::inspectWireMessageType(frame, frameType, decodeError) &&
                (frameType == network::WireMessageType::CosmeticLoadoutUpdated ||
                 frameType == network::WireMessageType::CosmeticLoadoutUpdateFailed)) {
                network::CosmeticLoadoutUpdateResponse response;
                if (!network::decodeCosmeticLoadoutUpdateResponse(
                        frame, response, decodeError)) {
                    fail("Invalid cosmetic loadout response: " + decodeError);
                    shutdownSocket();
                    return;
                }
                cosmeticLoadoutResponse_ = std::move(response);
                ++cosmeticLoadoutResponseRevision_;
                continue;
            }
            decodeError.clear();
            if (network::inspectWireMessageType(
                    frame, frameType, decodeError) &&
                frameType == network::WireMessageType::LogoutSuccess) {
                network::AuthenticationResponse response;
                if (!network::decodeAuthenticationResponse(
                        frame, response, decodeError)) {
                    fail("Invalid logout response: " + decodeError);
                    shutdownSocket();
                    return;
                }
                authenticationResponse_ = std::move(response);
                authenticated_ = false;
                awaitingAuthentication_ = false;
                adapter_.reset();
                continue;
            }
            decodeError.clear();
            if (authenticationMode_ && !authenticated_) {
                network::AuthenticationResponse response;
                if (!network::decodeAuthenticationResponse(
                        frame, response, decodeError)) {
                    fail("Invalid authentication response: " + decodeError);
                    shutdownSocket();
                    return;
                }
                authenticationResponse_ = std::move(response);
                awaitingAuthentication_ = false;
                if (std::holds_alternative<network::AuthenticationSuccess>(
                        authenticationResponse_->payload)) {
                    authenticated_ = true;
                }
                continue;
            }
            network::WireMessageType preMatchType{};
            if (network::inspectWireMessageType(
                    frame, preMatchType, decodeError) &&
                (preMatchType == network::WireMessageType::LobbyHosted ||
                 preMatchType == network::WireMessageType::LobbyMatchAssigned ||
                 preMatchType == network::WireMessageType::LobbyCancelled ||
                 preMatchType == network::WireMessageType::LobbyFailure ||
                 preMatchType == network::WireMessageType::MatchmakingQueued ||
                 preMatchType == network::WireMessageType::MatchmakingCancelled)) {
                network::LobbyResponse response;
                if (!network::decodeLobbyResponse(frame, response, decodeError)) {
                    fail("Invalid lobby response: " + decodeError);
                    shutdownSocket();
                    return;
                }
                lobbyResponse_ = std::move(response);
                ++lobbyResponseRevision_;
                continue;
            }
            decodeError.clear();
            if (adapter_ == nullptr) {
                if (!network::inspectWireMessageType(
                        frame, frameType, decodeError)) {
                    fail("Invalid server message: " + decodeError);
                    shutdownSocket();
                    return;
                }
                // A resolved-round update can already be buffered when an
                // intentional quit returns this authenticated connection to
                // pre-match state. It belongs to the released adapter, not to
                // the next match bootstrap.
                if (frameType == network::WireMessageType::ServerUpdate) {
                    continue;
                }
                decodeError.clear();
                network::ServerBootstrap bootstrap;
                if (!network::decodeServerBootstrap(frame, bootstrap, decodeError)) {
                    if (decodeError ==
                        "Unsupported Basilisk network protocol version.") {
                        fail("Protocol compatibility error: " + decodeError);
                    } else {
                        fail("Invalid server bootstrap: " + decodeError);
                    }
                    shutdownSocket();
                    return;
                }
                auto adapter = NetworkGameSessionAdapter::create(
                    std::move(bootstrap), transport_, decodeError);
                if (adapter == nullptr) {
                    if (decodeError ==
                        "Unsupported Basilisk network protocol version.") {
                        fail("Protocol compatibility error: " + decodeError);
                    } else {
                        fail("Rejected server bootstrap: " + decodeError);
                    }
                    shutdownSocket();
                    return;
                }
                bool accepted = false;
                {
                    std::lock_guard lock(socketState_->mutex);
                    if (!socketState_->closed) {
                        adapter_ = std::move(adapter);
                        accepted = true;
                    }
                    if (accepted)
                        socketState_->sessionEstablished = true;
                }
                if (!accepted) {
                    shutdownSocket();
                    return;
                }
            } else {
                bool ingested = false;
                network::WireMessageType type{};
                if (!network::inspectWireMessageType(frame, type, decodeError)) {
                    fail("Invalid server message: " + decodeError);
                    shutdownSocket();
                    return;
                }
                if (type == network::WireMessageType::LeaderboardPageResponse) {
                    network::LeaderboardPageResponse response;
                    if (network::decodeLeaderboardPageResponse(
                            frame, response, decodeError)) {
                        std::lock_guard lock(socketState_->mutex);
                        if (socketState_->closed) shutdownRequested = true;
                        else ingested = adapter_->ingest(
                            std::move(response), decodeError);
                    }
                } else if (type == network::WireMessageType::ClashStarted) {
                    network::ClashStarted clash;
                    if (network::decodeClashStarted(frame, clash, decodeError)) {
                        std::lock_guard lock(socketState_->mutex);
                        if (socketState_->closed) shutdownRequested = true;
                        else ingested = adapter_->ingest(std::move(clash), decodeError);
                    }
                } else if (type == network::WireMessageType::ClashResolved) {
                    network::ClashResolved clash;
                    if (network::decodeClashResolved(frame, clash, decodeError)) {
                        std::lock_guard lock(socketState_->mutex);
                        if (socketState_->closed) shutdownRequested = true;
                        else ingested = adapter_->ingest(std::move(clash), decodeError);
                    }
                } else {
                    network::ServerUpdate update;
                    if (type == network::WireMessageType::ServerUpdate &&
                        network::decodeServerUpdate(frame, update, decodeError)) {
                        std::lock_guard lock(socketState_->mutex);
                        if (socketState_->closed) shutdownRequested = true;
                        else ingested = adapter_->ingest(
                            std::move(update), decodeError);
                    } else if (decodeError.empty()) {
                        decodeError = "Unexpected server message type.";
                    }
                }
                if (shutdownRequested) {
                    shutdownSocket();
                    return;
                }
                if (!ingested) {
                    if (decodeError ==
                        "Unsupported Basilisk network protocol version.") {
                        fail("Protocol compatibility error: " + decodeError);
                    } else {
                        fail("Invalid server update: " + decodeError);
                    }
                    shutdownSocket();
                    return;
                }
            }
        }
    }

    NetworkConnectionState state() const noexcept {
        std::lock_guard lock(socketState_->mutex);
        return socketState_->state;
    }

    void clearGameplaySession() { adapter_.reset(); }

    std::string error() const {
        std::lock_guard lock(socketState_->mutex);
        return socketState_->error;
    }

    ClientSessionController* controller() noexcept {
        return adapter_ == nullptr ? nullptr : &adapter_->controller();
    }
    const std::optional<network::ClashStarted>& activeClash() const noexcept {
        static const std::optional<network::ClashStarted> none;
        return adapter_ ? adapter_->activeClash() : none;
    }
    bool submitClashResponse(std::string response) {
        return adapter_ && adapter_->submitClashResponse(std::move(response));
    }

    bool requestLeaderboard(std::uint32_t offset, std::uint32_t limit) {
        return adapter_ != nullptr && adapter_->requestLeaderboard(offset, limit);
    }

    const std::optional<network::LeaderboardPageResponse>&
    leaderboardPage() const noexcept {
        static const std::optional<network::LeaderboardPageResponse> empty;
        return adapter_ == nullptr ? empty : adapter_->leaderboardPage();
    }

    bool authenticate(const network::AuthenticationRequest& request) {
        network::WireBytes bytes;
        std::string encodeError;
        if (!authenticationMode_ || awaitingAuthentication_ || authenticated_ ||
            !network::encodeWire(request, bytes, encodeError)) return false;
        std::lock_guard lock(socketState_->mutex);
        if (socketState_->closed ||
            socketState_->state != NetworkConnectionState::Connected ||
            !socketState_->sendBinary || !socketState_->sendBinary(bytes))
            return false;
        authenticationResponse_.reset();
        awaitingAuthentication_ = true;
        return true;
    }

    bool logout(const std::string& sessionToken) {
        if (!authenticationMode_ || !authenticated_ || sessionToken.empty())
            return false;
        network::WireBytes bytes;
        std::string encodeError;
        if (!network::encodeWire(network::AuthenticationRequest{
                network::kProtocolVersion,
                network::LogoutRequest{sessionToken}}, bytes, encodeError))
            return false;
        std::lock_guard lock(socketState_->mutex);
        if (socketState_->closed || !socketState_->sendBinary ||
            !socketState_->sendBinary(bytes)) return false;
        authenticationResponse_.reset();
        awaitingAuthentication_ = true;
        return true;
    }

    bool requestLobby(const network::LobbyRequest& request) {
        network::WireBytes bytes;
        std::string encodeError;
        if (!authenticated_ || !network::encodeWire(request, bytes, encodeError))
            return false;
        std::lock_guard lock(socketState_->mutex);
        if (socketState_->closed || !socketState_->sendBinary ||
            !socketState_->sendBinary(bytes)) return false;
        lobbyResponse_.reset();
        return true;
    }

    bool updateCosmeticLoadout(
        const network::CosmeticLoadoutUpdateRequest& request) {
        network::WireBytes bytes;
        std::string encodeError;
        if (!authenticated_ || !network::encodeWire(request, bytes, encodeError))
            return false;
        std::lock_guard lock(socketState_->mutex);
        return !socketState_->closed && socketState_->sendBinary &&
            socketState_->sendBinary(bytes);
    }

    const std::optional<network::AuthenticationResponse>&
    authenticationResponse() const noexcept { return authenticationResponse_; }
    const std::optional<network::CosmeticLoadoutUpdateResponse>&
    cosmeticLoadoutResponse() const noexcept { return cosmeticLoadoutResponse_; }
    std::size_t cosmeticLoadoutResponseRevision() const noexcept {
        return cosmeticLoadoutResponseRevision_;
    }
    const std::optional<network::LobbyResponse>& lobbyResponse() const noexcept {
        return lobbyResponse_;
    }
    std::size_t lobbyResponseRevision() const noexcept {
        return lobbyResponseRevision_;
    }

private:
    void fail(std::string message) {
        std::lock_guard lock(socketState_->mutex);
        if (socketState_->closed) return;
        socketState_->state = NetworkConnectionState::Error;
        socketState_->error = std::move(message);
        socketState_->closed = true;
        socketState_->frames.clear();
        socketState_->sendBinary = {};
        socketState_->shutdownRequested = true;
    }

    void opened() {
        std::lock_guard lock(socketState_->mutex);
        if (socketState_->closed) return;
        socketState_->state = NetworkConnectionState::Connected;
    }

    void transportFailed(std::string reason) {
        std::lock_guard lock(socketState_->mutex);
        if (socketState_->closed) return;
        if (socketState_->sessionEstablished) {
            socketState_->state = NetworkConnectionState::Disconnected;
            socketState_->error =
                "Connection to the Basilisk server was lost: " + reason;
        } else {
            socketState_->state = NetworkConnectionState::Error;
            socketState_->error =
                "Unable to connect to the Basilisk server: " + reason;
        }
        socketState_->closed = true;
        socketState_->frames.clear();
        socketState_->sendBinary = {};
        socketState_->shutdownRequested = true;
    }

    void closed(std::string reason) {
        std::lock_guard lock(socketState_->mutex);
        if (socketState_->closed) return;
        if (socketState_->sessionEstablished) {
            socketState_->state = NetworkConnectionState::Disconnected;
            socketState_->error =
                "Connection to the Basilisk server was lost: " + reason;
        } else {
            socketState_->state = NetworkConnectionState::Error;
            socketState_->error =
                "Unable to establish a Basilisk session: " + reason;
        }
        socketState_->closed = true;
        socketState_->frames.clear();
        socketState_->sendBinary = {};
        socketState_->shutdownRequested = true;
    }

    void receive(const std::uint8_t* data, std::size_t size, bool text) {
        if (text) {
            fail("Server sent a text frame; Basilisk requires binary v1 frames.");
            return;
        }
        if (size > kMaximumFrameBytes) {
            fail("Server frame exceeds the 1 MiB limit.");
            return;
        }
        network::WireBytes frame(data, data + size);
        std::lock_guard lock(socketState_->mutex);
        if (socketState_->closed) return;
        socketState_->frames.push_back(std::move(frame));
    }

#if defined(__EMSCRIPTEN__)
    static bool onOpen(int, const EmscriptenWebSocketOpenEvent*, void* userData) {
        static_cast<Impl*>(userData)->opened();
        return true;
    }
    static bool onMessage(
        int, const EmscriptenWebSocketMessageEvent* event, void* userData) {
        static_cast<Impl*>(userData)->receive(
            event->data, event->numBytes, event->isText);
        return true;
    }
    static bool onError(int, const EmscriptenWebSocketErrorEvent*, void* userData) {
        static_cast<Impl*>(userData)->browserTransportError();
        return true;
    }
    static bool onClose(
        int, const EmscriptenWebSocketCloseEvent* event, void* userData) {
        std::ostringstream reason;
        reason << "WebSocket closed (" << event->code << "): " << event->reason;
        static_cast<Impl*>(userData)->closed(reason.str());
        return true;
    }
    EMSCRIPTEN_WEBSOCKET_T socket_{0};
#else
    void handleNativeMessage(const ix::WebSocketMessagePtr& message) {
        switch (message->type) {
        case ix::WebSocketMessageType::Open:
            opened();
            break;
        case ix::WebSocketMessageType::Message:
            receive(
                reinterpret_cast<const std::uint8_t*>(message->str.data()),
                message->str.size(), !message->binary);
            break;
        case ix::WebSocketMessageType::Error:
            transportFailed("WebSocket error: " + message->errorInfo.reason);
            break;
        case ix::WebSocketMessageType::Close:
            closed("WebSocket closed: " + message->closeInfo.reason);
            break;
        default:
            break;
        }
    }
#if defined(_WIN32)
    std::unique_ptr<NativeNetworkRuntime> networkRuntime_;
#endif
    ix::WebSocket socket_;
#endif
    std::shared_ptr<SharedSocketState> socketState_;
    std::shared_ptr<WebSocketClientTransport> transport_;
    std::unique_ptr<NetworkGameSessionAdapter> adapter_;
    std::string url_;
    std::optional<network::AuthenticationResponse> authenticationResponse_;
    std::optional<network::CosmeticLoadoutUpdateResponse>
        cosmeticLoadoutResponse_;
    std::size_t cosmeticLoadoutResponseRevision_{0};
    std::optional<network::LobbyResponse> lobbyResponse_;
    std::size_t lobbyResponseRevision_{0};
    bool authenticationMode_{false};
    bool awaitingAuthentication_{false};
    bool authenticated_{false};
    bool socketStopped_{false};

#if defined(__EMSCRIPTEN__)
    void browserTransportError() {
        std::lock_guard lock(socketState_->mutex);
        if (socketState_->closed) return;
        // Browser error events contain no close code or reason. The following
        // close event carries those diagnostics, so do not tear down first.
        socketState_->error = "Browser WebSocket error; awaiting close details.";
    }
#endif
};

WebSocketNetworkSession::WebSocketNetworkSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

WebSocketNetworkSession::~WebSocketNetworkSession() = default;
WebSocketNetworkSession::WebSocketNetworkSession(WebSocketNetworkSession&&) noexcept = default;
WebSocketNetworkSession& WebSocketNetworkSession::operator=(WebSocketNetworkSession&&) noexcept = default;

std::unique_ptr<WebSocketNetworkSession> WebSocketNetworkSession::connect(
    std::string url, std::string token, std::string& error) {
    if (url.empty() || token.empty()) {
        error = "--connect and --token both require non-empty values.";
        return nullptr;
    }
#if defined(_WIN32)
    auto networkRuntime = NativeNetworkRuntime::acquire(error);
    if (networkRuntime == nullptr) return nullptr;
    auto impl = std::make_unique<Impl>(
        std::move(url), std::move(token), std::move(networkRuntime));
#else
    auto impl = std::make_unique<Impl>(std::move(url), std::move(token));
#endif
    if (!impl->start(error)) return nullptr;
    return std::unique_ptr<WebSocketNetworkSession>(
        new WebSocketNetworkSession(std::move(impl)));
}

std::unique_ptr<WebSocketNetworkSession>
WebSocketNetworkSession::connectForAuthentication(
    std::string url, std::string& error) {
    if (url.empty()) {
        error = "--connect requires a non-empty value.";
        return nullptr;
    }
#if defined(_WIN32)
    auto networkRuntime = NativeNetworkRuntime::acquire(error);
    if (networkRuntime == nullptr) return nullptr;
    auto impl = std::make_unique<Impl>(
        std::move(url), std::move(networkRuntime));
#else
    auto impl = std::make_unique<Impl>(std::move(url));
#endif
    if (!impl->start(error)) return nullptr;
    return std::unique_ptr<WebSocketNetworkSession>(
        new WebSocketNetworkSession(std::move(impl)));
}

void WebSocketNetworkSession::pump() { impl_->pump(); }
void WebSocketNetworkSession::close() { impl_->stop(); }
void WebSocketNetworkSession::clearGameplaySession() {
    impl_->clearGameplaySession();
}
NetworkConnectionState WebSocketNetworkSession::state() const noexcept {
    return impl_->state();
}
std::string WebSocketNetworkSession::error() const { return impl_->error(); }
ClientSessionController* WebSocketNetworkSession::controller() noexcept {
    return impl_->controller();
}
const ClientSessionController* WebSocketNetworkSession::controller() const noexcept {
    return impl_->controller();
}
const std::optional<network::ClashStarted>& WebSocketNetworkSession::activeClash() const noexcept {
    return impl_->activeClash();
}
bool WebSocketNetworkSession::submitClashResponse(std::string response) {
    return impl_->submitClashResponse(std::move(response));
}
bool WebSocketNetworkSession::requestLeaderboard(
    std::uint32_t offset,
    std::uint32_t limit) {
    return impl_->requestLeaderboard(offset, limit);
}
const std::optional<network::LeaderboardPageResponse>&
WebSocketNetworkSession::leaderboardPage() const noexcept {
    return impl_->leaderboardPage();
}
bool WebSocketNetworkSession::authenticate(
    const network::AuthenticationRequest& request) {
    return impl_->authenticate(request);
}
bool WebSocketNetworkSession::logout(const std::string& sessionToken) {
    return impl_->logout(sessionToken);
}
bool WebSocketNetworkSession::updateCosmeticLoadout(
    const network::CosmeticLoadoutUpdateRequest& request) {
    return impl_->updateCosmeticLoadout(request);
}
bool WebSocketNetworkSession::requestLobby(
    const network::LobbyRequest& request) {
    return impl_->requestLobby(request);
}
const std::optional<network::AuthenticationResponse>&
WebSocketNetworkSession::authenticationResponse() const noexcept {
    return impl_->authenticationResponse();
}
const std::optional<network::CosmeticLoadoutUpdateResponse>&
WebSocketNetworkSession::cosmeticLoadoutResponse() const noexcept {
    return impl_->cosmeticLoadoutResponse();
}
std::size_t WebSocketNetworkSession::cosmeticLoadoutResponseRevision() const noexcept {
    return impl_->cosmeticLoadoutResponseRevision();
}
const std::optional<network::LobbyResponse>&
WebSocketNetworkSession::lobbyResponse() const noexcept {
    return impl_->lobbyResponse();
}
std::size_t WebSocketNetworkSession::lobbyResponseRevision() const noexcept {
    return impl_->lobbyResponseRevision();
}

} // namespace basilisk::game
