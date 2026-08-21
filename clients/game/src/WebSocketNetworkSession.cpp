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
};

class WebSocketClientTransport final : public network::ClientTransport {
public:
    explicit WebSocketClientTransport(std::shared_ptr<SharedSocketState> socket)
        : socket_(std::move(socket)) {}

    [[nodiscard]] bool send(const network::ClientCommand& command) override {
        network::WireBytes bytes;
        std::string error;
        if (!network::encodeWire(command, bytes, error)) return false;
        std::function<bool(std::span<const std::uint8_t>)> sender;
        {
            std::lock_guard lock(socket_->mutex);
            if (socket_->state != NetworkConnectionState::Connected ||
                !socket_->sendBinary) return false;
            sender = socket_->sendBinary;
        }
        return sender(bytes);
    }

private:
    std::shared_ptr<SharedSocketState> socket_;
};

} // namespace

class WebSocketNetworkSession::Impl {
public:
    Impl(std::string url, std::string token)
        : socketState_(std::make_shared<SharedSocketState>()),
          transport_(std::make_shared<WebSocketClientTransport>(socketState_)),
          url_(authenticatedUrl(std::move(url), token)) {}

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
#if defined(__EMSCRIPTEN__)
        if (socket_ > 0) {
            emscripten_websocket_close(socket_, 1000, "Client shutdown");
            emscripten_websocket_delete(socket_);
            socket_ = 0;
        }
#else
        socket_.stop();
#endif
    }

    void pump() {
        std::deque<network::WireBytes> frames;
        {
            std::lock_guard lock(socketState_->mutex);
            frames.swap(socketState_->frames);
        }
        for (network::WireBytes& frame : frames) {
            std::string decodeError;
            if (adapter_ == nullptr) {
                network::ServerBootstrap bootstrap;
                if (!network::decodeServerBootstrap(frame, bootstrap, decodeError)) {
                    fail("Invalid server bootstrap: " + decodeError);
                    return;
                }
                adapter_ = NetworkGameSessionAdapter::create(
                    std::move(bootstrap), transport_, decodeError);
                if (adapter_ == nullptr) {
                    fail("Rejected server bootstrap: " + decodeError);
                    return;
                }
            } else {
                network::ServerUpdate update;
                if (!network::decodeServerUpdate(frame, update, decodeError) ||
                    !adapter_->ingest(std::move(update), decodeError)) {
                    fail("Invalid server update: " + decodeError);
                    return;
                }
            }
        }
    }

    NetworkConnectionState state() const noexcept {
        std::lock_guard lock(socketState_->mutex);
        return socketState_->state;
    }

    std::string error() const {
        std::lock_guard lock(socketState_->mutex);
        return socketState_->error;
    }

    ClientSessionController* controller() noexcept {
        return adapter_ == nullptr ? nullptr : &adapter_->controller();
    }

private:
    void fail(std::string message) {
        std::lock_guard lock(socketState_->mutex);
        socketState_->state = NetworkConnectionState::Error;
        socketState_->error = std::move(message);
    }

    void opened() {
        std::lock_guard lock(socketState_->mutex);
        socketState_->state = NetworkConnectionState::Connected;
    }

    void closed(std::string reason) {
        std::lock_guard lock(socketState_->mutex);
        if (socketState_->state != NetworkConnectionState::Error) {
            socketState_->state = NetworkConnectionState::Disconnected;
            socketState_->error = std::move(reason);
        }
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
        static_cast<Impl*>(userData)->fail("Browser WebSocket error.");
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
            fail("WebSocket error: " + message->errorInfo.reason);
            break;
        case ix::WebSocketMessageType::Close:
            closed("WebSocket closed: " + message->closeInfo.reason);
            break;
        default:
            break;
        }
    }
    ix::WebSocket socket_;
#endif
    std::shared_ptr<SharedSocketState> socketState_;
    std::shared_ptr<WebSocketClientTransport> transport_;
    std::unique_ptr<NetworkGameSessionAdapter> adapter_;
    std::string url_;
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
    auto impl = std::make_unique<Impl>(std::move(url), std::move(token));
    if (!impl->start(error)) return nullptr;
    return std::unique_ptr<WebSocketNetworkSession>(
        new WebSocketNetworkSession(std::move(impl)));
}

void WebSocketNetworkSession::pump() { impl_->pump(); }
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

} // namespace basilisk::game
