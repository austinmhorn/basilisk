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
        std::shared_ptr<PublicAccountProfileStore> publicProfiles,
        std::uint16_t port,
        std::string bindAddress,
        int webSocketPingIntervalSeconds,
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
          publicProfiles_(std::move(publicProfiles)),
          server_(
              port,
              std::move(bindAddress),
              kServerWebSocketListenBacklog,
              kServerWebSocketMaxConnections,
              ix::WebSocketServer::kDefaultHandShakeTimeoutSecs,
              ix::SocketServer::kDefaultAddressFamily,
              webSocketPingIntervalSeconds),
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
                disconnectClient(client, false);
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
        for (auto matchIt = assignedMatches_.begin();
             matchIt != assignedMatches_.end();) {
            auto& assigned = matchIt->second;
            assigned.match->advanceTime(elapsedMs);
            for (auto participant = assigned.participants.begin();
                 participant != assigned.participants.end();) {
                if (participant->second.connected) {
                    ++participant;
                    continue;
                }
                if (elapsedMs >= participant->second.graceRemainingMs) {
                    participant = assigned.participants.erase(participant);
                } else {
                    participant->second.graceRemainingMs -= elapsedMs;
                    ++participant;
                }
            }
            if (assigned.participants.empty()) {
                matchIt = assignedMatches_.erase(matchIt);
            } else {
                ++matchIt;
            }
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
        bool reconnectable{false};
        bool active{true};
    };

    struct PreMatchClient {
        AccountIdentity account;
        PublicAccountProfile profile;
        client::AccountCosmeticLoadout cosmeticLoadout;
    };

    struct AssignedMatch {
        struct Participant {
            PlayerId player{};
            std::uint64_t graceRemainingMs{};
            bool connected{true};
        };

        std::unique_ptr<AuthoritativeInMemoryMatch> match;
        std::map<AccountIdentity, Participant> participants;
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
            std::fprintf(stderr,
                "Basilisk WebSocket closed: code=%u reason=%s remote=%s\n",
                message->closeInfo.code,
                message->closeInfo.reason.c_str(),
                message->closeInfo.remote ? "true" : "false");
            disconnect(socket);
            break;
        case ix::WebSocketMessageType::Error:
            std::fprintf(stderr, "Basilisk WebSocket error: %s\n",
                message->errorInfo.reason.c_str());
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
                                         std::nullopt, false, true});
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
            disconnectClient(found->second, false);
            clients_.erase(found);
            pendingAuthentication_.insert(&socket);
            drainAll();
            return;
        }
        if (authentication_ != nullptr && found->second.account.has_value() &&
            network::inspectWireMessageType(bytes, type, error) &&
            type == network::WireMessageType::UpdateCosmeticLoadout) {
            network::CosmeticLoadoutUpdateRequest request;
            AccountIdentity requestedAccount;
            network::WireBytes response;
            if (!network::decodeCosmeticLoadoutUpdateRequest(
                    bytes, request, error) ||
                !authentication_->resolveSession(
                    AuthSessionToken{request.sessionToken}, requestedAccount, error) ||
                requestedAccount != *found->second.account ||
                !authenticationProtocol_.processCosmeticUpdate(
                    bytes, response, error)) {
                if (error.empty()) error = "Cosmetic session does not match connection.";
                reject(socket, found->second, 1008, error);
                return;
            }
            const std::string payload(
                reinterpret_cast<const char*>(response.data()), response.size());
            (void)socket.sendBinary(payload);
            return;
        }
        if (found->second.account.has_value() &&
            network::inspectWireMessageType(bytes, type, error) &&
            (type == network::WireMessageType::HostLobby ||
             type == network::WireMessageType::JoinLobby ||
             type == network::WireMessageType::CancelHostedLobby ||
             type == network::WireMessageType::FindMatch ||
             type == network::WireMessageType::CancelFindMatch ||
             type == network::WireMessageType::HostSandboxLobby ||
             type == network::WireMessageType::JoinSandboxLobby ||
             type == network::WireMessageType::LeaveSandboxLobby ||
            type == network::WireMessageType::SetSandboxReady ||
             type == network::WireMessageType::StartSandboxMatch)) {
            std::string publicName;
            if (type == network::WireMessageType::HostSandboxLobby ||
                type == network::WireMessageType::JoinSandboxLobby) {
                PublicAccountProfile profile;
                if (!authentication_->publicProfile(
                        *found->second.account, profile, error)) {
                    reject(socket, found->second, 1008, error);
                    return;
                }
                publicName = profile.username.value;
            }
            std::vector<LobbyProtocolDelivery> deliveries;
            if (!lobbyProtocol_.process(
                    *found->second.account, publicName,
                    bytes, deliveries, error)) {
                reject(socket, found->second, 1008, error);
                return;
            }
            handleLobbyDeliveries(deliveries);
            if (auto launch = lobbyProtocol_.takeSandboxLaunch()) {
                if (!launchSandboxMatch(*launch, error))
                    reject(socket, found->second, 1008, error);
            }
            return;
        }
        const bool explicitQuit =
            network::inspectWireMessageType(bytes, type, error) &&
            type == network::WireMessageType::Quit;
        if (!found->second.endpoint->sendBytes(bytes, error)) {
            reject(socket, found->second, 1008, error);
            return;
        }
        if (explicitQuit && found->second.account.has_value() &&
            found->second.assignedMatch.has_value()) {
            PublicAccountProfile profile;
            client::AccountCosmeticLoadout loadout;
            if (!authentication_->publicProfile(
                    *found->second.account, profile, error) ||
                !authentication_->cosmeticLoadout(
                    *found->second.account, loadout, error)) {
                reject(socket, found->second, 1008, error);
                return;
            }
            const AccountIdentity account = *found->second.account;
            found->second.reconnectable = false;
            updateAssignedReservation(found->second, false);
            found->second.active = false;
            clients_.erase(found);
            authenticatedPreMatch_.emplace(
                &socket, PreMatchClient{
                    account, std::move(profile), std::move(loadout)});
            reportTrophyError();
            drainAll();
            return;
        }
        if (explicitQuit) {
            found->second.reconnectable = false;
            updateAssignedReservation(found->second, false);
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
        if (type == network::WireMessageType::UpdateCosmeticLoadout) {
            network::CosmeticLoadoutUpdateRequest request;
            AccountIdentity requestedAccount;
            network::WireBytes response;
            if (!network::decodeCosmeticLoadoutUpdateRequest(
                    bytes, request, error) ||
                !authentication_->resolveSession(
                    AuthSessionToken{request.sessionToken}, requestedAccount, error) ||
                requestedAccount != account ||
                !authenticationProtocol_.processCosmeticUpdate(
                    bytes, response, error)) {
                socket.close(1008, error.empty()
                    ? "Cosmetic session does not match connection." : error);
                return;
            }
            network::CosmeticLoadoutUpdateResponse decoded;
            if (!network::decodeCosmeticLoadoutUpdateResponse(
                    response, decoded, error)) {
                socket.close(1008, error);
                return;
            }
            if (const auto* success =
                    std::get_if<network::CosmeticLoadoutUpdateSuccess>(
                        &decoded.payload)) {
                auto found = authenticatedPreMatch_.find(&socket);
                if (found != authenticatedPreMatch_.end())
                    found->second.cosmeticLoadout = success->loadout;
            }
            const std::string payload(
                reinterpret_cast<const char*>(response.data()), response.size());
            (void)socket.sendBinary(payload);
            return;
        }
        if (type == network::WireMessageType::LeaderboardPageRequest) {
            network::ClientCommand command;
            if (!network::decodeClientCommand(bytes, command, error)) {
                socket.close(1008, error);
                return;
            }
            const auto* request =
                std::get_if<network::LeaderboardPageRequest>(&command.payload);
            if (request == nullptr || publicLeaderboard_ == nullptr) {
                socket.close(1008, "Public leaderboard is not configured.");
                return;
            }
            std::vector<PublicTrophyLeaderboardEntry> entries;
            if (!publicLeaderboard_->leaderboardPage(
                    request->offset, request->limit, entries, error)) {
                socket.close(1011, error.empty()
                    ? "Unable to read public leaderboard." : error);
                return;
            }
            network::LeaderboardPageResponse response;
            response.offset = request->offset;
            response.entries = std::move(entries);
            network::WireBytes responseBytes;
            if (!network::encodeWire(response, responseBytes, error)) {
                socket.close(1011, error);
                return;
            }
            const std::string payload(
                reinterpret_cast<const char*>(responseBytes.data()),
                responseBytes.size());
            (void)socket.sendBinary(payload);
            return;
        }
        if (type != network::WireMessageType::HostLobby &&
            type != network::WireMessageType::JoinLobby &&
            type != network::WireMessageType::CancelHostedLobby &&
            type != network::WireMessageType::FindMatch &&
            type != network::WireMessageType::CancelFindMatch &&
            type != network::WireMessageType::HostSandboxLobby &&
            type != network::WireMessageType::JoinSandboxLobby &&
            type != network::WireMessageType::LeaveSandboxLobby &&
            type != network::WireMessageType::SetSandboxReady &&
            type != network::WireMessageType::StartSandboxMatch) {
            socket.close(1008, "Unexpected pre-match message type");
            return;
        }
        std::vector<LobbyProtocolDelivery> deliveries;
        const auto authenticated = authenticatedPreMatch_.find(&socket);
        const std::string_view publicName = authenticated == authenticatedPreMatch_.end()
            ? std::string_view{} : authenticated->second.profile.username.value;
        if (!lobbyProtocol_.process(
                account, publicName, bytes, deliveries, error)) {
            socket.close(1008, error);
            return;
        }
        handleLobbyDeliveries(deliveries);
        if (auto launch = lobbyProtocol_.takeSandboxLaunch()) {
            if (!launchSandboxMatch(*launch, error)) socket.close(1008, error);
        }
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
            {PlayerId{1}, hostConnection->second.profile.username.value,
             hostConnection->second.cosmeticLoadout.callingCardId,
             hostConnection->second.cosmeticLoadout.emblemId},
            {PlayerId{2}, guestConnection->second.profile.username.value,
             guestConnection->second.cosmeticLoadout.callingCardId,
             guestConnection->second.cosmeticLoadout.emblemId},
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
                                            matchId, true, true});
        clients_.emplace(guestSocket, Client{PlayerId{2}, std::move(p2), *guest,
                                             matchId, true, true});
        const std::uint64_t graceMs = match->disconnectGraceMs();
        assignedMatches_.emplace(matchId,
            AssignedMatch{
                std::move(match),
                {
                    {*host, {PlayerId{1}, graceMs, true}},
                    {*guest, {PlayerId{2}, graceMs, true}},
                },
                false});
        drainAll();
        return true;
    }

    bool launchSandboxMatch(
        SandboxMatchAssignment assignment, std::string& error) {
        std::map<PlayerId, decltype(authenticatedPreMatch_)::iterator> connections;
        for (const auto& [player, account] : assignment.humans) {
            auto connection = std::find_if(authenticatedPreMatch_.begin(),
                authenticatedPreMatch_.end(), [&](const auto& entry) {
                    return entry.second.account == account;
                });
            if (connection == authenticatedPreMatch_.end()) {
                error = "A Sandbox lobby member disconnected before launch.";
                return false;
            }
            connections.emplace(player, connection);
        }
        std::vector<client::PublicPlayerProfile> profiles;
        profiles.reserve(assignment.config.hunterCount);
        for (std::size_t slot = 1; slot <= assignment.config.hunterCount; ++slot) {
            const PlayerId player = static_cast<PlayerId>(slot);
            const auto human = connections.find(player);
            if (human != connections.end()) {
                profiles.push_back({player, human->second->second.profile.username.value,
                    human->second->second.cosmeticLoadout.callingCardId,
                    human->second->second.cosmeticLoadout.emblemId});
            } else {
                profiles.push_back({player, "BASILISK AI " + std::to_string(slot),
                    {"arrow-right-black"}, {"circle-black"}});
            }
        }
        assignment.config.mapSeed = MapSeed{random_()};
        assignment.config.matchSeed = MatchSeed{random_()};
        assignment.config.aiSeed = client::ai::AiSeed{random_()};
        std::vector<client::ai::AiConfig> aiConfigs;
        for (const PlayerId player : assignment.aiPlayers) {
            const client::ai::AiSeed seed{
                assignment.config.aiSeed ^ static_cast<std::uint64_t>(player)};
            aiConfigs.push_back({assignment.config.aiDifficulty,
                client::ai::resolveBehavior(assignment.config.aiBehavior, seed),
                player, seed});
        }
        auto match = AuthoritativeInMemoryMatch::createSandbox(
            assignment.config, std::move(profiles), std::move(aiConfigs), error);
        if (match == nullptr) return false;
        const std::string matchId = "sandbox-" +
            std::to_string(++nextMatchId_) + "-" + std::to_string(random_());
        std::map<AccountIdentity, AssignedMatch::Participant> participants;
        struct ConnectedHuman {
            ix::WebSocket* socket;
            AccountIdentity account;
            PlayerId player;
            std::shared_ptr<InMemoryMatchEndpoint> endpoint;
        };
        std::vector<ConnectedHuman> connected;
        for (const auto& [player, account] : assignment.humans) {
            auto endpoint = match->connect(player, error);
            if (endpoint == nullptr) return false;
            auto connection = connections.at(player);
            connected.push_back({connection->first, account, player, endpoint});
            participants.emplace(account, AssignedMatch::Participant{
                player, match->disconnectGraceMs(), true});
        }
        for (const auto& human : connected)
            authenticatedPreMatch_.erase(human.socket);
        for (auto& human : connected)
            clients_.emplace(human.socket, Client{human.player,
                std::move(human.endpoint), human.account, matchId, true, true});
        assignedMatches_.emplace(matchId, AssignedMatch{
            std::move(match), std::move(participants), false});
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

        const auto sendResponse = [&socket](const network::WireBytes& bytes) {
            const std::string payload(
                reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return socket.sendBinary(payload).success;
        };
        const auto rejectAccountInUse = [&] {
            network::WireBytes rejection;
            std::string encodeError;
            if (!network::encodeWire(network::AuthenticationResponse{
                    network::kProtocolVersion,
                    network::AuthenticationFailure{
                        "Account is already connected"}},
                    rejection, encodeError)) {
                socket.close(1008, encodeError);
                pendingAuthentication_.erase(&socket);
                return;
            }
            (void)sendResponse(rejection);
        };

        network::AuthenticationResponse decoded;
        if (!network::decodeAuthenticationResponse(response, decoded, error)) return;
        const auto* success = std::get_if<network::AuthenticationSuccess>(
            &decoded.payload);
        if (success == nullptr) {
            (void)sendResponse(response);
            return;
        }
        AccountIdentity account;
        if (!authentication_->resolveSession(
                AuthSessionToken{success->sessionToken}, account, error)) return;
        if (publicProfiles_ != nullptr) {
            const PublicProfileStoreResult profileResult =
                publicProfiles_->storeProfile(account, success->profile, error);
            if (profileResult != PublicProfileStoreResult::Stored &&
                profileResult != PublicProfileStoreResult::AlreadyStored) {
                if (error.empty()) {
                    error = profileResult == PublicProfileStoreResult::DuplicateUsername
                        ? "Username is already in use."
                        : "Unable to persist the authenticated public profile.";
                }
                socket.close(1008, error);
                pendingAuthentication_.erase(&socket);
                return;
            }
        }
        for (const auto& [existingSocket, existing] : authenticatedPreMatch_) {
            if (existing.account == account && existingSocket != &socket) {
                rejectAccountInUse();
                return;
            }
        }
        for (const auto& [existingSocket, existing] : clients_) {
            if (existing.active && existing.account == account &&
                existingSocket != &socket) {
                rejectAccountInUse();
                return;
            }
        }
        for (auto& [matchId, assigned] : assignedMatches_) {
            const auto participant = assigned.participants.find(account);
            if (participant == assigned.participants.end() ||
                participant->second.connected) continue;
            auto endpoint = assigned.match->reconnect(
                participant->second.player, error);
            if (endpoint == nullptr) {
                assigned.participants.erase(participant);
                socket.close(1008, error);
                pendingAuthentication_.erase(&socket);
                return;
            }
            if (!sendResponse(response)) {
                endpoint->disconnect();
                participant->second.connected = false;
                participant->second.graceRemainingMs =
                    assigned.match->disconnectGraceMs();
                return;
            }
            participant->second.connected = true;
            participant->second.graceRemainingMs =
                assigned.match->disconnectGraceMs();
            pendingAuthentication_.erase(&socket);
            clients_.emplace(&socket, Client{
                participant->second.player,
                std::move(endpoint),
                account,
                matchId,
                true,
                true});
            drainAll();
            return;
        }
        const auto bound = authenticatedAccounts_.find(account);
        if (bound == authenticatedAccounts_.end()) {
            if (!sendResponse(response)) return;
            pendingAuthentication_.erase(&socket);
            authenticatedPreMatch_.emplace(
                &socket, PreMatchClient{
                    account, success->profile, success->cosmeticLoadout});
            return;
        }
        if (usedPlayers_.contains(bound->second)) {
            rejectAccountInUse();
            return;
        }
        auto endpoint = match_->connect(bound->second, error);
        if (endpoint == nullptr) {
            socket.close(1008, error);
            pendingAuthentication_.erase(&socket);
            return;
        }
        if (!sendResponse(response)) return;
        pendingAuthentication_.erase(&socket);
        clients_.emplace(
            &socket, Client{bound->second, std::move(endpoint), account,
                            std::nullopt, false, true});
        usedPlayers_.insert(bound->second);
        drainAll();
    }

    void reject(
        ix::WebSocket& socket,
        Client& client,
        std::uint16_t code,
        const std::string& reason) {
        disconnectClient(client, false);
        drainAll();
        socket.close(code, reason);
    }

    void disconnect(ix::WebSocket& socket) {
        pendingAuthentication_.erase(&socket);
        const auto preMatch = authenticatedPreMatch_.find(&socket);
        if (preMatch != authenticatedPreMatch_.end()) {
            std::vector<LobbyProtocolDelivery> sandboxDeliveries;
            lobbyProtocol_.disconnect(preMatch->second.account, sandboxDeliveries);
            handleLobbyDeliveries(sandboxDeliveries);
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
            std::vector<LobbyProtocolDelivery> sandboxDeliveries;
            lobbyProtocol_.disconnect(*found->second.account, sandboxDeliveries);
            handleLobbyDeliveries(sandboxDeliveries);
            std::string ignored;
            (void)lobbies_.cancelFindMatch(*found->second.account, ignored);
        }
        disconnectClient(found->second, found->second.reconnectable);
        clients_.erase(found);
        drainAll();
    }

    void updateAssignedReservation(Client& client, bool preserve) {
        if (!client.assignedMatch.has_value() || !client.account.has_value()) return;
        const auto match = assignedMatches_.find(*client.assignedMatch);
        if (match == assignedMatches_.end()) return;
        const auto participant = match->second.participants.find(*client.account);
        if (participant == match->second.participants.end()) return;
        if (preserve) {
            participant->second.connected = false;
            participant->second.graceRemainingMs =
                match->second.match->disconnectGraceMs();
        } else {
            match->second.participants.erase(participant);
            if (match->second.participants.empty()) assignedMatches_.erase(match);
        }
    }

    void disconnectClient(Client& client, bool preserveReservation) {
        if (!client.active) return;
        client.active = false;
        if (preserveReservation) {
            client.endpoint->disconnect();
            ++processedDisconnectCount_;
        } else if (client.endpoint->send(network::ClientCommand{
                       network::kProtocolVersion,
                       network::QuitCommand{client.player},
                   })) {
            ++processedDisconnectCount_;
        }
        reportTrophyError();
        updateAssignedReservation(client, preserveReservation);
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
    std::shared_ptr<PublicAccountProfileStore> publicProfiles_;
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
    std::shared_ptr<PublicAccountProfileStore> publicProfiles;
    std::optional<TrophyScoringContext> trophyScoring;
    std::string trophyDatabasePath = config.trophyDatabasePath;
    if (config.trophies.has_value() &&
        !config.trophies->sqliteDatabasePath.empty()) {
        if (!trophyDatabasePath.empty() &&
            trophyDatabasePath != config.trophies->sqliteDatabasePath) {
            error = "Server-wide and fixed-match trophy databases must match.";
            return nullptr;
        }
        trophyDatabasePath = config.trophies->sqliteDatabasePath;
    }
    if (!trophyDatabasePath.empty()) {
        auto persistence = SQLiteTrophyPersistence::open(
            trophyDatabasePath, error);
        if (persistence == nullptr) {
            error = "Unable to open trophy database: " + error;
            return nullptr;
        }
        trophyLedger = std::make_shared<TrophyLedger>(persistence);
        publicProfiles = persistence;
        publicLeaderboard = std::make_shared<PublicTrophyReadModel>(
            trophyLedger, publicProfiles);
    }
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
        if (trophies.p1PublicProfile.has_value() && publicProfiles == nullptr) {
            error = "Public profiles require SQLite trophy persistence.";
            return nullptr;
        }
        if (trophyLedger == nullptr) {
            trophyLedger = std::make_shared<TrophyLedger>();
        }
        if (publicProfiles != nullptr) {
            const auto persistProfile = [&](PlayerSlot slot,
                                            const AccountIdentity& account,
                                            const PublicAccountProfile& profile) {
                const PublicProfileStoreResult result =
                    publicProfiles->storeProfile(account, profile, error);
                if (result == PublicProfileStoreResult::Stored ||
                    result == PublicProfileStoreResult::AlreadyStored)
                    return true;
                const char* player = slot == PlayerSlot::P1 ? "P1" : "P2";
                if (result == PublicProfileStoreResult::DuplicateUsername) {
                    error = std::string{"Username '"} +
                        profile.username.value + "' for " + player +
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
                 !persistProfile(PlayerSlot::P2, trophies.p2Account,
                    *trophies.p2PublicProfile))) return nullptr;
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
    if (config.bindAddress.empty()) {
        error = "WebSocket server bind address must be non-empty.";
        return nullptr;
    }
    if (config.webSocketPingIntervalSeconds <= 0) {
        error = "WebSocket server ping interval must be positive.";
        return nullptr;
    }
#if defined(_WIN32)
    auto networkRuntime = NativeNetworkRuntime::acquire(error);
    if (networkRuntime == nullptr) return nullptr;
#endif
    auto impl = std::make_unique<Impl>(
        std::move(match), std::move(trophyLedger), std::move(publicLeaderboard),
        std::move(publicProfiles),
        config.port, std::move(config.bindAddress),
        config.webSocketPingIntervalSeconds,
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
