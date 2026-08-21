#include "LocalWebSocketMatchServer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <string_view>
#include <utility>

#include <ixwebsocket/IXWebSocketServer.h>

#include "AuthoritativeInMemoryMatch.hpp"
#include "AccountAuthProtocol.hpp"
#include "LobbyProtocolService.hpp"
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
        std::shared_ptr<PublicTrophyReadModel> publicLeaderboard,
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
          publicLeaderboard_(std::move(publicLeaderboard)),
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
        assignedMatches_.clear();
        pendingAuthentication_.clear();
        authenticatedPreMatch_.clear();
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
        for (auto& [id, assigned] : assignedMatches_) {
            (void)id;
            assigned.match->advanceTime(elapsedMs);
        }
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
        std::optional<AccountIdentity> account;
        std::optional<std::string> assignedMatch;
        bool active{true};
    };

    struct PreMatchClient {
        AccountIdentity account;
        PublicAccountProfile profile;
    };

    struct AssignedMatch {
        std::unique_ptr<AuthoritativeInMemoryMatch> match;
        std::size_t connectedPlayers{2};
        bool trophyErrorReported{false};
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
        clients_.emplace(&socket, Client{*player, std::move(endpoint), std::nullopt,
                                         std::nullopt, true});
        usedPlayers_.insert(*player);
        drainAll();
    }

    void receive(ix::WebSocket& socket, const ix::WebSocketMessage& message) {
        const auto found = clients_.find(&socket);
        const auto preMatch = authenticatedPreMatch_.find(&socket);
        if (found == clients_.end() && preMatch != authenticatedPreMatch_.end()) {
            receivePreMatch(socket, preMatch->second.account, message);
            return;
        }
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
            if (found->second.account.has_value())
                lobbies_.cancelHostedBy(*found->second.account);
            if (found->second.account.has_value()) {
                std::string ignored;
                (void)lobbies_.cancelFindMatch(*found->second.account, ignored);
            }
            usedPlayers_.erase(found->second.player);
            disconnectClient(found->second);
            clients_.erase(found);
            pendingAuthentication_.insert(&socket);
            drainAll();
            return;
        }
        if (found->second.account.has_value() &&
            network::inspectWireMessageType(bytes, type, error) &&
            (type == network::WireMessageType::HostLobby ||
             type == network::WireMessageType::JoinLobby ||
             type == network::WireMessageType::CancelHostedLobby ||
             type == network::WireMessageType::FindMatch ||
             type == network::WireMessageType::CancelFindMatch)) {
            std::vector<LobbyProtocolDelivery> deliveries;
            if (!lobbyProtocol_.process(
                    *found->second.account, bytes, deliveries, error)) {
                reject(socket, found->second, 1008, error);
                return;
            }
            handleLobbyDeliveries(deliveries);
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

    void receivePreMatch(ix::WebSocket& socket, const AccountIdentity& account,
                         const ix::WebSocketMessage& message) {
        if (!message.binary || message.str.size() > kMaximumFrameBytes) {
            lobbies_.cancelHostedBy(account);
            std::string ignored;
            (void)lobbies_.cancelFindMatch(account, ignored);
            authenticatedPreMatch_.erase(&socket);
            socket.close(message.binary ? 1009 : 1003,
                message.binary ? "Frame exceeds 1 MiB limit" : "Binary frames required");
            return;
        }
        const auto* data = reinterpret_cast<const std::uint8_t*>(message.str.data());
        const std::span<const std::uint8_t> bytes{data, message.str.size()};
        std::string error;
        network::WireMessageType type{};
        if (!network::inspectWireMessageType(bytes, type, error)) {
            socket.close(1008, error);
            return;
        }
        if (type == network::WireMessageType::LogoutSession) {
            network::WireBytes response;
            if (!authenticationProtocol_.process(bytes, response, error)) {
                socket.close(1008, error);
                return;
            }
            const std::string payload(
                reinterpret_cast<const char*>(response.data()), response.size());
            (void)socket.sendBinary(payload);
            lobbies_.cancelHostedBy(account);
            authenticatedPreMatch_.erase(&socket);
            pendingAuthentication_.insert(&socket);
            return;
        }
        if (type != network::WireMessageType::HostLobby &&
            type != network::WireMessageType::JoinLobby &&
            type != network::WireMessageType::CancelHostedLobby &&
            type != network::WireMessageType::FindMatch &&
            type != network::WireMessageType::CancelFindMatch) {
            socket.close(1008, "Unexpected pre-match message type");
            return;
        }
        std::vector<LobbyProtocolDelivery> deliveries;
        if (!lobbyProtocol_.process(account, bytes, deliveries, error)) {
            socket.close(1008, error);
            return;
        }
        handleLobbyDeliveries(deliveries);
    }

    void handleLobbyDeliveries(
        const std::vector<LobbyProtocolDelivery>& deliveries) {
        if (deliveries.size() == 2) {
            network::LobbyResponse first;
            network::LobbyResponse second;
            std::string error;
            if (network::decodeLobbyResponse(deliveries[0].bytes, first, error) &&
                network::decodeLobbyResponse(deliveries[1].bytes, second, error)) {
                const auto* firstAssignment =
                    std::get_if<network::LobbyMatchAssigned>(&first.payload);
                const auto* secondAssignment =
                    std::get_if<network::LobbyMatchAssigned>(&second.payload);
                if (firstAssignment != nullptr && secondAssignment != nullptr &&
                    firstAssignment->lobbyCode == secondAssignment->lobbyCode) {
                    if (launchAssignedMatch(deliveries, *firstAssignment,
                                            *secondAssignment, error)) return;
                    std::fprintf(stderr, "Unable to launch assigned match: %s\n",
                                 error.c_str());
                    return;
                }
            }
        }
        for (const LobbyProtocolDelivery& delivery : deliveries)
            deliverLobby(delivery);
    }

    bool launchAssignedMatch(
        const std::vector<LobbyProtocolDelivery>& deliveries,
        const network::LobbyMatchAssigned& first,
        const network::LobbyMatchAssigned& second,
        std::string& error) {
        const auto accountForRole = [&](network::LobbyAssignmentRole role)
            -> std::optional<AccountIdentity> {
            if (first.role == role) return deliveries[0].recipient;
            if (second.role == role) return deliveries[1].recipient;
            return std::nullopt;
        };
        const auto host = accountForRole(network::LobbyAssignmentRole::Host);
        const auto guest = accountForRole(network::LobbyAssignmentRole::Guest);
        if (!host.has_value() || !guest.has_value() || *host == *guest) {
            error = "Match assignment must contain distinct host and guest accounts.";
            return false;
        }
        const auto findConnection = [&](const AccountIdentity& account) {
            return std::find_if(authenticatedPreMatch_.begin(),
                authenticatedPreMatch_.end(), [&](const auto& entry) {
                    return entry.second.account == account;
                });
        };
        auto hostConnection = findConnection(*host);
        auto guestConnection = findConnection(*guest);
        if (hostConnection == authenticatedPreMatch_.end() ||
            guestConnection == authenticatedPreMatch_.end()) {
            error = "Assigned player is no longer connected.";
            return false;
        }

        std::vector<client::PublicPlayerProfile> profiles{
            {PlayerId{1}, hostConnection->second.profile.displayName,
             client::CallingCardId{"default"}, client::EmblemId{"default"}},
            {PlayerId{2}, guestConnection->second.profile.displayName,
             client::CallingCardId{"default"}, client::EmblemId{"default"}},
        };
        const std::string matchId = "assigned-" +
            std::to_string(++nextMatchId_) + "-" + std::to_string(random_());
        std::optional<TrophyScoringContext> trophyScoring;
        if (trophyLedger_ != nullptr) {
            trophyScoring = TrophyScoringContext{
                TrophyMatchId{matchId},
                {{PlayerId{1}, *host}, {PlayerId{2}, *guest}},
                trophyLedger_,
            };
        }
        auto match = AuthoritativeInMemoryMatch::create(
            MapSeed{random_()}, MatchSeed{random_()}, std::move(profiles), error,
            std::move(trophyScoring), publicLeaderboard_);
        if (match == nullptr) return false;
        auto p1 = match->connect(PlayerId{1}, error);
        if (p1 == nullptr) return false;
        auto p2 = match->connect(PlayerId{2}, error);
        if (p2 == nullptr) return false;

        for (const LobbyProtocolDelivery& delivery : deliveries)
            deliverLobby(delivery);
        ix::WebSocket* hostSocket = hostConnection->first;
        ix::WebSocket* guestSocket = guestConnection->first;
        authenticatedPreMatch_.erase(hostConnection);
        authenticatedPreMatch_.erase(guestConnection);
        clients_.emplace(hostSocket, Client{PlayerId{1}, std::move(p1), *host,
                                            matchId, true});
        clients_.emplace(guestSocket, Client{PlayerId{2}, std::move(p2), *guest,
                                             matchId, true});
        assignedMatches_.emplace(matchId,
            AssignedMatch{std::move(match), 2, false});
        drainAll();
        return true;
    }

    void deliverLobby(const LobbyProtocolDelivery& delivery) {
        const std::string payload(
            reinterpret_cast<const char*>(delivery.bytes.data()),
            delivery.bytes.size());
        for (auto& [socket, client] : authenticatedPreMatch_) {
            if (client.account == delivery.recipient)
                (void)socket->sendBinary(payload);
        }
        for (auto& [socket, client] : clients_) {
            if (client.active && client.account.has_value() &&
                *client.account == delivery.recipient)
                (void)socket->sendBinary(payload);
        }
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
        for (const auto& [existingSocket, existing] : authenticatedPreMatch_) {
            if (existing.account == account && existingSocket != &socket) {
                socket.close(1008, "Account is already connected");
                pendingAuthentication_.erase(&socket);
                return;
            }
        }
        for (const auto& [existingSocket, existing] : clients_) {
            if (existing.active && existing.account == account &&
                existingSocket != &socket) {
                socket.close(1008, "Account is already connected");
                pendingAuthentication_.erase(&socket);
                return;
            }
        }
        const auto bound = authenticatedAccounts_.find(account);
        pendingAuthentication_.erase(&socket);
        if (bound == authenticatedAccounts_.end()) {
            authenticatedPreMatch_.emplace(
                &socket, PreMatchClient{account, success->profile});
            return;
        }
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
        clients_.emplace(
            &socket, Client{bound->second, std::move(endpoint), account,
                            std::nullopt, true});
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
        const auto preMatch = authenticatedPreMatch_.find(&socket);
        if (preMatch != authenticatedPreMatch_.end()) {
            lobbies_.cancelHostedBy(preMatch->second.account);
            std::string ignored;
            (void)lobbies_.cancelFindMatch(preMatch->second.account, ignored);
            authenticatedPreMatch_.erase(preMatch);
        }
        const auto found = clients_.find(&socket);
        if (found == clients_.end()) return;
        if (found->second.account.has_value())
            lobbies_.cancelHostedBy(*found->second.account);
        if (found->second.account.has_value()) {
            std::string ignored;
            (void)lobbies_.cancelFindMatch(*found->second.account, ignored);
        }
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
        if (client.assignedMatch.has_value()) {
            const auto found = assignedMatches_.find(*client.assignedMatch);
            if (found != assignedMatches_.end() &&
                --found->second.connectedPlayers == 0)
                assignedMatches_.erase(found);
        }
    }

    void reportTrophyError() {
        if (!trophyErrorReported_) {
            const auto error = match_->trophyScoringError();
            if (error.has_value()) {
                std::fprintf(stderr, "Basilisk trophy scoring error: %s\n",
                             error->c_str());
                trophyErrorReported_ = true;
            }
        }
        for (auto& [id, assigned] : assignedMatches_) {
            (void)id;
            if (assigned.trophyErrorReported) continue;
            const auto error = assigned.match->trophyScoringError();
            if (!error.has_value()) continue;
            std::fprintf(stderr, "Basilisk trophy scoring error: %s\n",
                         error->c_str());
            assigned.trophyErrorReported = true;
        }
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
    std::shared_ptr<PublicTrophyReadModel> publicLeaderboard_;
    ix::WebSocketServer server_;
    std::map<std::string, PlayerId> tokens_;
    std::shared_ptr<SQLiteAccountAuth> authentication_;
    AccountAuthProtocol authenticationProtocol_;
    std::map<AccountIdentity, PlayerId> authenticatedAccounts_;
    LobbyCoordinator lobbies_;
    LobbyProtocolService lobbyProtocol_{lobbies_};
    std::set<PlayerId> usedPlayers_;
    std::set<ix::WebSocket*> pendingAuthentication_;
    std::map<ix::WebSocket*, PreMatchClient> authenticatedPreMatch_;
    std::map<ix::WebSocket*, Client> clients_;
    std::map<std::string, AssignedMatch> assignedMatches_;
    std::mt19937_64 random_{std::random_device{}()};
    std::uint64_t nextMatchId_{0};
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
    const bool anyAccountBinding = config.p1AuthenticatedAccount.has_value() ||
        config.p2AuthenticatedAccount.has_value();
    const bool completeAccountBinding = config.p1AuthenticatedAccount.has_value() &&
        config.p2AuthenticatedAccount.has_value();
    if ((!accountAuthentication && anyAccountBinding) ||
        anyAccountBinding != completeAccountBinding ||
        (completeAccountBinding &&
         config.p1AuthenticatedAccount == config.p2AuthenticatedAccount)) {
        error = "Optional gameplay account bindings must be complete and distinct.";
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
        std::move(trophyScoring), publicLeaderboard);
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
        std::move(match), std::move(trophyLedger), std::move(publicLeaderboard),
        config.port,
        std::map<std::string, PlayerId>{
            {std::move(config.p1Token), PlayerId{1}},
            {std::move(config.p2Token), PlayerId{2}},
        },
        std::move(config.authentication),
        completeAccountBinding
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
