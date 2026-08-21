#include "LocalWebSocketMatchServer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <utility>

#include <ixwebsocket/IXWebSocketServer.h>

#include "AuthoritativeInMemoryMatch.hpp"
#include "AccountAuthProtocol.hpp"
#include "SQLiteTrophyPersistence.hpp"
#if defined(_WIN32)
#include "NativeNetworkRuntime.hpp"
#endif

namespace basilisk::game::server {
namespace {

constexpr std::size_t kMaximumFrameBytes = 1U << 20;

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::optional<std::string> percentDecode(std::string_view value) {
    std::string decoded;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '%') {
            decoded.push_back(value[index]);
            continue;
        }
        if (index + 2 >= value.size()) return std::nullopt;
        const int high = hexValue(value[index + 1]);
        const int low = hexValue(value[index + 2]);
        if (high < 0 || low < 0) return std::nullopt;
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return decoded;
}

std::optional<std::string> tokenFromUri(std::string_view uri) {
    const std::size_t query = uri.find('?');
    if (query == std::string_view::npos) return std::nullopt;
    std::optional<std::string> token;
    std::string_view remaining = uri.substr(query + 1);
    while (!remaining.empty()) {
        const std::size_t separator = remaining.find('&');
        const std::string_view part = remaining.substr(0, separator);
        const std::size_t equals = part.find('=');
        if (equals != std::string_view::npos && part.substr(0, equals) == "token") {
            if (token.has_value()) return std::nullopt;
            token = percentDecode(part.substr(equals + 1));
            if (!token.has_value() || token->empty()) return std::nullopt;
        }
        if (separator == std::string_view::npos) break;
        remaining.remove_prefix(separator + 1);
    }
    return token;
}

} // namespace

class LocalWebSocketMatchServer::Impl {
public:
    Impl(
        std::unique_ptr<AuthoritativeInMemoryMatch> match,
        std::shared_ptr<TrophyLedger> trophyLedger,
        std::uint16_t port,
        std::map<std::string, PlayerId> tokens,
        std::shared_ptr<SQLiteAccountAuth> authentication,
        std::map<AccountIdentity, PlayerId> authenticatedAccounts
#if defined(_WIN32)
        , std::unique_ptr<NativeNetworkRuntime> networkRuntime
#endif
        )
#if defined(_WIN32)
        : networkRuntime_(std::move(networkRuntime)),
          match_(std::move(match)),
#else
        : match_(std::move(match)),
#endif
          trophyLedger_(std::move(trophyLedger)),
          server_(port, "127.0.0.1", 5, 2),
          tokens_(std::move(tokens)),
          authentication_(std::move(authentication)),
          authenticationProtocol_(authentication_),
          authenticatedAccounts_(std::move(authenticatedAccounts)) {}

    bool start(std::string& error) {
        server_.disablePerMessageDeflate();
        server_.setOnClientMessageCallback(
            [this](std::shared_ptr<ix::ConnectionState>, ix::WebSocket& socket,
                   const ix::WebSocketMessagePtr& message) {
                handle(socket, message);
            });
        const auto listening = server_.listen();
        if (!listening.first) {
            error = listening.second;
            return false;
        }
        server_.start();
        error.clear();
        return true;
    }

    void stop() {
        {
            std::lock_guard lock(mutex_);
            if (stopped_) return;
            stopped_ = true;
            for (auto& [socket, client] : clients_) {
                (void)socket;
                disconnectClient(client);
            }
        }
        server_.stop();
        std::lock_guard lock(mutex_);
        clients_.clear();
        pendingAuthentication_.clear();
        usedPlayers_.clear();
    }

    std::uint16_t port() { return static_cast<std::uint16_t>(server_.getPort()); }

    std::size_t connectedClientCount() const noexcept {
        std::lock_guard lock(mutex_);
        return static_cast<std::size_t>(std::count_if(
            clients_.begin(), clients_.end(),
            [](const auto& entry) { return entry.second.active; }));
    }

    std::size_t processedDisconnectCount() const noexcept {
        std::lock_guard lock(mutex_);
        return processedDisconnectCount_;
    }

    RoundNumber authoritativeRound() const noexcept {
        std::lock_guard lock(mutex_);
        return match_->authoritativeRound();
    }

    std::size_t resolvedRoundCount() const noexcept {
        std::lock_guard lock(mutex_);
        return match_->resolvedRoundCount();
    }

    void advanceTime(std::uint64_t elapsedMs) {
        std::lock_guard lock(mutex_);
        if (stopped_) return;
        match_->advanceTime(elapsedMs);
        reportTrophyError();
        drainAll();
    }

    bool trophyTotal(
        const AccountIdentity& account,
        std::int64_t& total,
        std::string& error) const {

        std::lock_guard lock(mutex_);
        if (trophyLedger_ == nullptr) {
            error = "Trophy scoring is not configured.";
            return false;
        }
        return trophyLedger_->trophyTotal(account, total, error);
    }

    bool leaderboard(
        std::vector<TrophyLeaderboardEntry>& entries,
        std::string& error) const {

        std::lock_guard lock(mutex_);
        if (trophyLedger_ == nullptr) {
            error = "Trophy scoring is not configured.";
            return false;
        }
        return trophyLedger_->leaderboard(entries, error);
    }

    std::optional<std::string> trophyScoringError() const {
        std::lock_guard lock(mutex_);
        return match_->trophyScoringError();
    }

private:
    struct Client {
        PlayerId player{};
        std::shared_ptr<InMemoryMatchEndpoint> endpoint;
        bool active{true};
    };

    void handle(ix::WebSocket& socket, const ix::WebSocketMessagePtr& message) {
        std::lock_guard lock(mutex_);
        if (stopped_) return;
        switch (message->type) {
        case ix::WebSocketMessageType::Open:
            authenticate(socket, message->openInfo.uri);
            break;
        case ix::WebSocketMessageType::Message:
            receive(socket, *message);
            break;
        case ix::WebSocketMessageType::Close:
        case ix::WebSocketMessageType::Error:
            disconnect(socket);
            break;
        default:
            break;
        }
    }

    void authenticate(ix::WebSocket& socket, const std::string& uri) {
        if (authentication_ != nullptr) {
            if (tokenFromUri(uri).has_value()) {
                socket.close(1008, "Session tokens must use binary frames");
                return;
            }
            pendingAuthentication_.insert(&socket);
            return;
        }
        const auto token = tokenFromUri(uri);
        std::optional<PlayerId> player;
        if (token.has_value()) {
            const auto found = tokens_.find(*token);
            if (found != tokens_.end()) player = found->second;
        }
        if (!player.has_value()) {
            socket.close(1008, "Invalid authentication token");
            return;
        }
        if (usedPlayers_.contains(*player)) {
            socket.close(1008, "Token is already in use");
            return;
        }
        std::string error;
        auto endpoint = match_->connect(*player, error);
        if (endpoint == nullptr) {
            socket.close(1008, error);
            return;
        }
        clients_.emplace(&socket, Client{*player, std::move(endpoint), true});
        usedPlayers_.insert(*player);
        drainAll();
    }

    void receive(ix::WebSocket& socket, const ix::WebSocketMessage& message) {
        const auto found = clients_.find(&socket);
        if (found == clients_.end() && pendingAuthentication_.contains(&socket)) {
            receiveAuthentication(socket, message);
            return;
        }
        if (found == clients_.end() || !found->second.active) {
            socket.close(1008, "Unauthenticated or disconnected client");
            return;
        }
        if (!message.binary) {
            reject(socket, found->second, 1003, "Binary frames required");
            return;
        }
        if (message.str.size() > kMaximumFrameBytes) {
            reject(socket, found->second, 1009, "Frame exceeds 1 MiB limit");
            return;
        }
        const auto* data = reinterpret_cast<const std::uint8_t*>(message.str.data());
        const std::span<const std::uint8_t> bytes{data, message.str.size()};
        std::string error;
        network::WireMessageType type{};
        if (authentication_ != nullptr &&
            network::inspectWireMessageType(bytes, type, error) &&
            type == network::WireMessageType::LogoutSession) {
            network::WireBytes response;
            if (!authenticationProtocol_.process(bytes, response, error)) {
                reject(socket, found->second, 1008, error);
                return;
            }
            const std::string payload(
                reinterpret_cast<const char*>(response.data()), response.size());
            (void)socket.sendBinary(payload);
            usedPlayers_.erase(found->second.player);
            disconnectClient(found->second);
            clients_.erase(found);
            pendingAuthentication_.insert(&socket);
            drainAll();
            return;
        }
        if (!found->second.endpoint->sendBytes(
                bytes, error)) {
            reject(socket, found->second, 1008, error);
            return;
        }
        reportTrophyError();
        drainAll();
    }

    void receiveAuthentication(
        ix::WebSocket& socket, const ix::WebSocketMessage& message) {
        if (!message.binary) {
            socket.close(1003, "Binary frames required");
            pendingAuthentication_.erase(&socket);
            return;
        }
        if (message.str.size() > kMaximumFrameBytes) {
            socket.close(1009, "Frame exceeds 1 MiB limit");
            pendingAuthentication_.erase(&socket);
            return;
        }
        const auto* data = reinterpret_cast<const std::uint8_t*>(message.str.data());
        network::WireBytes response;
        std::string error;
        if (!authenticationProtocol_.process(
                std::span<const std::uint8_t>{data, message.str.size()},
                response, error)) {
            socket.close(1008, error);
            pendingAuthentication_.erase(&socket);
            return;
        }
        const std::string payload(
            reinterpret_cast<const char*>(response.data()), response.size());
        if (!socket.sendBinary(payload).success) return;

        network::AuthenticationResponse decoded;
        if (!network::decodeAuthenticationResponse(response, decoded, error)) return;
        const auto* success = std::get_if<network::AuthenticationSuccess>(
            &decoded.payload);
        if (success == nullptr) return;
        AccountIdentity account;
        if (!authentication_->resolveSession(
                AuthSessionToken{success->sessionToken}, account, error)) return;
        const auto bound = authenticatedAccounts_.find(account);
        if (bound == authenticatedAccounts_.end()) return;
        if (usedPlayers_.contains(bound->second)) {
            socket.close(1008, "Account is already in use");
            pendingAuthentication_.erase(&socket);
            return;
        }
        auto endpoint = match_->connect(bound->second, error);
        if (endpoint == nullptr) {
            socket.close(1008, error);
            pendingAuthentication_.erase(&socket);
            return;
        }
        pendingAuthentication_.erase(&socket);
        clients_.emplace(
            &socket, Client{bound->second, std::move(endpoint), true});
        usedPlayers_.insert(bound->second);
        drainAll();
    }

    void reject(
        ix::WebSocket& socket,
        Client& client,
        std::uint16_t code,
        const std::string& reason) {
        disconnectClient(client);
        drainAll();
        socket.close(code, reason);
    }

    void disconnect(ix::WebSocket& socket) {
        pendingAuthentication_.erase(&socket);
        const auto found = clients_.find(&socket);
        if (found == clients_.end()) return;
        disconnectClient(found->second);
        clients_.erase(found);
        drainAll();
    }

    void disconnectClient(Client& client) {
        if (!client.active) return;
        client.active = false;
        if (client.endpoint->send(network::ClientCommand{
            network::kProtocolVersion,
            network::QuitCommand{client.player},
        })) ++processedDisconnectCount_;
        reportTrophyError();
    }

    void reportTrophyError() {
        if (trophyErrorReported_) return;
        const auto error = match_->trophyScoringError();
        if (!error.has_value()) return;
        std::fprintf(stderr, "Basilisk trophy scoring error: %s\n", error->c_str());
        trophyErrorReported_ = true;
    }

    void drainAll() {
        for (auto& [socket, client] : clients_) {
            if (!client.active) continue;
            while (auto frame = client.endpoint->takeNextServerFrame()) {
                const std::string payload(
                    reinterpret_cast<const char*>(frame->data()), frame->size());
                if (!socket->sendBinary(payload).success) break;
            }
        }
    }

#if defined(_WIN32)
    std::unique_ptr<NativeNetworkRuntime> networkRuntime_;
#endif
    std::unique_ptr<AuthoritativeInMemoryMatch> match_;
    std::shared_ptr<TrophyLedger> trophyLedger_;
    ix::WebSocketServer server_;
    std::map<std::string, PlayerId> tokens_;
    std::shared_ptr<SQLiteAccountAuth> authentication_;
    AccountAuthProtocol authenticationProtocol_;
    std::map<AccountIdentity, PlayerId> authenticatedAccounts_;
    std::set<PlayerId> usedPlayers_;
    std::set<ix::WebSocket*> pendingAuthentication_;
    std::map<ix::WebSocket*, Client> clients_;
    mutable std::mutex mutex_;
    bool stopped_{false};
    bool trophyErrorReported_{false};
    std::size_t processedDisconnectCount_{0};
};

LocalWebSocketMatchServer::LocalWebSocketMatchServer(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

LocalWebSocketMatchServer::~LocalWebSocketMatchServer() { stop(); }

std::unique_ptr<LocalWebSocketMatchServer> LocalWebSocketMatchServer::start(
    LocalWebSocketServerConfig config, std::string& error) {
    const bool accountAuthentication = config.authentication != nullptr;
    if (accountAuthentication != config.p1AuthenticatedAccount.has_value() ||
        accountAuthentication != config.p2AuthenticatedAccount.has_value() ||
        (accountAuthentication &&
         config.p1AuthenticatedAccount == config.p2AuthenticatedAccount)) {
        error = "Account authentication requires two distinct account bindings.";
        return nullptr;
    }
    if (!accountAuthentication &&
        (config.p1Token.empty() || config.p2Token.empty() ||
         config.p1Token == config.p2Token)) {
        error = "P1 and P2 tokens must be non-empty and distinct.";
        return nullptr;
    }
    std::shared_ptr<TrophyLedger> trophyLedger;
    std::shared_ptr<PublicTrophyReadModel> publicLeaderboard;
    std::optional<TrophyScoringContext> trophyScoring;
    if (config.trophies.has_value()) {
        const LocalServerTrophyConfig& trophies = *config.trophies;
        if (trophies.match.value.empty() || trophies.p1Account.value.empty() ||
            trophies.p2Account.value.empty() ||
            trophies.p1Account == trophies.p2Account) {
            error = "Trophy scoring requires a match ID and two distinct account IDs.";
            return nullptr;
        }
        if (trophies.p1PublicProfile.has_value() !=
            trophies.p2PublicProfile.has_value()) {
            error = "Public profiles must be configured for both players.";
            return nullptr;
        }
        if (trophies.p1PublicProfile.has_value() &&
            trophies.sqliteDatabasePath.empty()) {
            error = "Public profiles require SQLite trophy persistence.";
            return nullptr;
        }
        if (trophies.sqliteDatabasePath.empty()) {
            trophyLedger = std::make_shared<TrophyLedger>();
        } else {
            auto persistence = SQLiteTrophyPersistence::open(
                trophies.sqliteDatabasePath, error);
            if (persistence == nullptr) {
                error = "Unable to open trophy database: " + error;
                return nullptr;
            }
            const auto persistProfile = [&](PlayerSlot slot,
                                            const AccountIdentity& account,
                                            const PublicAccountProfile& profile) {
                const PublicProfileStoreResult result =
                    persistence->storeProfile(account, profile, error);
                if (result == PublicProfileStoreResult::Stored ||
                    result == PublicProfileStoreResult::AlreadyStored)
                    return true;
                const char* player = slot == PlayerSlot::P1 ? "P1" : "P2";
                if (result == PublicProfileStoreResult::DuplicateHandle) {
                    error = std::string{"Public handle '"} +
                        profile.handle.value + "' for " + player +
                        " is already in use.";
                } else if (result == PublicProfileStoreResult::AccountConflict) {
                    error = std::string{"Public profile for "} + player +
                        " conflicts with the account's existing stable profile.";
                } else if (error.empty()) {
                    error = std::string{"Unable to persist public profile for "} +
                        player + ".";
                }
                return false;
            };
            if (trophies.p1PublicProfile.has_value() &&
                (!persistProfile(
                    PlayerSlot::P1, trophies.p1Account,
                    *trophies.p1PublicProfile) ||
                 !persistProfile(
                    PlayerSlot::P2, trophies.p2Account,
                    *trophies.p2PublicProfile))) return nullptr;
            trophyLedger = std::make_shared<TrophyLedger>(persistence);
            publicLeaderboard = std::make_shared<PublicTrophyReadModel>(
                trophyLedger, persistence);
        }
        trophyScoring = TrophyScoringContext{
            trophies.match,
            {
                {PlayerId{1}, trophies.p1Account},
                {PlayerId{2}, trophies.p2Account},
            },
            trophyLedger,
        };
    }
    auto match = AuthoritativeInMemoryMatch::create(
        config.mapSeed, config.matchSeed, std::move(config.profiles), error,
        std::move(trophyScoring), std::move(publicLeaderboard));
    if (match == nullptr) return nullptr;
    if (config.port == 0) {
        error = "WebSocket server port must be non-zero.";
        return nullptr;
    }
#if defined(_WIN32)
    auto networkRuntime = NativeNetworkRuntime::acquire(error);
    if (networkRuntime == nullptr) return nullptr;
#endif
    auto impl = std::make_unique<Impl>(
        std::move(match), std::move(trophyLedger), config.port,
        std::map<std::string, PlayerId>{
            {std::move(config.p1Token), PlayerId{1}},
            {std::move(config.p2Token), PlayerId{2}},
        },
        std::move(config.authentication),
        accountAuthentication
            ? std::map<AccountIdentity, PlayerId>{
                {*config.p1AuthenticatedAccount, PlayerId{1}},
                {*config.p2AuthenticatedAccount, PlayerId{2}},
              }
            : std::map<AccountIdentity, PlayerId>{}
#if defined(_WIN32)
        , std::move(networkRuntime)
#endif
        );
    if (!impl->start(error)) return nullptr;
    return std::unique_ptr<LocalWebSocketMatchServer>(
        new LocalWebSocketMatchServer(std::move(impl)));
}

std::uint16_t LocalWebSocketMatchServer::port() const noexcept {
    return impl_->port();
}
std::size_t LocalWebSocketMatchServer::connectedClientCount() const noexcept {
    return impl_->connectedClientCount();
}
std::size_t LocalWebSocketMatchServer::processedDisconnectCount() const noexcept {
    return impl_->processedDisconnectCount();
}
RoundNumber LocalWebSocketMatchServer::authoritativeRound() const noexcept {
    return impl_->authoritativeRound();
}
std::size_t LocalWebSocketMatchServer::resolvedRoundCount() const noexcept {
    return impl_->resolvedRoundCount();
}
bool LocalWebSocketMatchServer::trophyTotal(
    const AccountIdentity& account,
    std::int64_t& total,
    std::string& error) const {
    return impl_->trophyTotal(account, total, error);
}
bool LocalWebSocketMatchServer::leaderboard(
    std::vector<TrophyLeaderboardEntry>& entries,
    std::string& error) const {
    return impl_->leaderboard(entries, error);
}
std::optional<std::string>
LocalWebSocketMatchServer::trophyScoringError() const {
    return impl_->trophyScoringError();
}
void LocalWebSocketMatchServer::advanceTime(std::uint64_t elapsedMs) {
    impl_->advanceTime(elapsedMs);
}
void LocalWebSocketMatchServer::stop() { impl_->stop(); }

} // namespace basilisk::game::server
