#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <sqlite3.h>

#include "AuthoritativeInMemoryMatch.hpp"
#include "AccountAuth.hpp"
#include "LocalWebSocketMatchServer.hpp"
#include "SQLiteTrophyPersistence.hpp"
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

std::unique_ptr<LocalWebSocketMatchServer> startServer(
    std::optional<LocalServerTrophyConfig> trophies = std::nullopt) {
    std::string error;
    for (int attempt = 0; attempt < 10; ++attempt) {
        LocalWebSocketServerConfig config;
        config.port = static_cast<std::uint16_t>(ix::getFreePort());
        config.p1Token = "test-p1";
        config.p2Token = "test-p2";
        config.profiles = profiles();
        config.trophies = trophies;
        auto server = LocalWebSocketMatchServer::start(std::move(config), error);
        if (server != nullptr) return server;
    }
    std::fprintf(stderr, "Unable to start loopback server: %s\n", error.c_str());
    assert(false && "Unable to reserve a loopback WebSocket test port");
    return nullptr;
}

class TemporaryTrophyDatabase {
public:
    TemporaryTrophyDatabase() {
        const auto suffix = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("basilisk-server-trophies-" + std::to_string(suffix) + ".sqlite3");
    }
    ~TemporaryTrophyDatabase() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
    }
    [[nodiscard]] std::string path() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

std::string url(const LocalWebSocketMatchServer& server) {
    return "ws://127.0.0.1:" + std::to_string(server.port());
}

int assignedScoredMatchCount(const std::string& databasePath) {
    sqlite3* database = nullptr;
    assert(sqlite3_open_v2(databasePath.c_str(), &database,
        SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    assert(sqlite3_prepare_v2(database,
        "SELECT COUNT(*) FROM trophy_scored_matches "
        "WHERE match_id LIKE 'assigned-%'",
        -1, &statement, nullptr) == SQLITE_OK);
    assert(sqlite3_step(statement) == SQLITE_ROW);
    const int count = sqlite3_column_int(statement, 0);
    assert(sqlite3_finalize(statement) == SQLITE_OK);
    assert(sqlite3_close(database) == SQLITE_OK);
    return count;
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
    (*frame)[8] = 3; // V2's big-endian version header becomes unsupported V3.

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

void serverQueriesUseDurableAccountsAndSQLiteSurvivesRestart() {
    TemporaryTrophyDatabase database;
    std::string error;
    auto persistence = SQLiteTrophyPersistence::open(database.path(), error);
    assert(persistence != nullptr && error.empty());
    const std::vector<TrophyLedgerEntry> seededEntries{
        {TrophyMatchId{"prior-match"}, AccountIdentity{"durable-p1"},
         TrophyReason::Win, 2},
        {TrophyMatchId{"prior-match"}, AccountIdentity{"durable-p2"},
         TrophyReason::Loss, -1},
    };
    assert(persistence->appendMatch(
        TrophyMatchId{"prior-match"}, seededEntries, error) ==
        TrophyAppendResult::Appended);
    persistence.reset();

    const LocalServerTrophyConfig trophies{
        TrophyMatchId{"hosted-unfinished"},
        AccountIdentity{"durable-p1"},
        AccountIdentity{"durable-p2"},
        database.path(),
    };
    {
        auto server = startServer(trophies);
        std::int64_t total = 0;
        assert(server->trophyTotal(AccountIdentity{"durable-p1"}, total, error));
        assert(total == 2);
        assert(server->trophyTotal(AccountIdentity{"test-p1"}, total, error));
        assert(total == 0); // The opaque authentication token is not an account ID.
        std::vector<TrophyLeaderboardEntry> leaderboard;
        assert(server->leaderboard(leaderboard, error));
        assert(leaderboard.size() == 2);
        const TrophyLeaderboardEntry expectedLeader{
            AccountIdentity{"durable-p1"}, 2};
        assert(leaderboard.front() == expectedLeader);

        auto client = WebSocketNetworkSession::connect(
            url(*server), "test-p1", error);
        assert(client != nullptr);
        auto secondClient = WebSocketNetworkSession::connect(
            url(*server), "test-p2", error);
        assert(secondClient != nullptr);
        assert(waitUntil([&] {
            client->pump();
            secondClient->pump();
            return client->controller() != nullptr &&
                secondClient->controller() != nullptr;
        }));
        assert(client->controller()->viewContext().localPlayer == PlayerId{1});
        assert(secondClient->controller()->viewContext().localPlayer == PlayerId{2});
        assert(client->controller()->trophyTotal() == 2);
        assert(secondClient->controller()->trophyTotal() == -1);
        server->stop();
    }

    auto reopened = startServer(LocalServerTrophyConfig{
        TrophyMatchId{"hosted-after-restart"},
        AccountIdentity{"durable-p1"},
        AccountIdentity{"durable-p2"},
        database.path(),
    });
    std::int64_t total = 0;
    assert(reopened->trophyTotal(AccountIdentity{"durable-p1"}, total, error));
    assert(total == 2);
    auto reconnected = WebSocketNetworkSession::connect(
        url(*reopened), "test-p1", error);
    assert(reconnected != nullptr);
    assert(waitUntil([&] {
        reconnected->pump();
        return reconnected->controller() != nullptr;
    }));
    assert(reconnected->controller()->trophyTotal() == 2);
}

void serverPersistsPublicProfilesAndRejectsDuplicateHandles() {
    TemporaryTrophyDatabase database;
    LocalServerTrophyConfig trophies{
        TrophyMatchId{"public-profile-match"},
        AccountIdentity{"durable-profile-p1"},
        AccountIdentity{"durable-profile-p2"},
        database.path(),
        PublicAccountProfile{PublicProfileHandle{"mara"}, "Mara Voss"},
        PublicAccountProfile{PublicProfileHandle{"elias"}, "Elias Thorn"},
    };
    auto server = startServer(trophies);
    server->stop();

    std::string error;
    auto persistence = SQLiteTrophyPersistence::open(database.path(), error);
    assert(persistence != nullptr && error.empty());
    std::optional<PublicAccountProfile> profile;
    assert(persistence->profileForAccount(
        AccountIdentity{"durable-profile-p1"}, profile, error));
    assert(profile == trophies.p1PublicProfile);
    assert(persistence->profileForAccount(
        AccountIdentity{"durable-profile-p2"}, profile, error));
    assert(profile == trophies.p2PublicProfile);
    persistence.reset();

    TemporaryTrophyDatabase conflictDatabase;
    trophies.match = TrophyMatchId{"duplicate-handle-match"};
    trophies.sqliteDatabasePath = conflictDatabase.path();
    trophies.p2PublicProfile = PublicAccountProfile{
        PublicProfileHandle{"mara"}, "Elias Thorn"};
    LocalWebSocketServerConfig config;
    config.port = static_cast<std::uint16_t>(ix::getFreePort());
    config.p1Token = "distinct-token-p1";
    config.p2Token = "distinct-token-p2";
    config.profiles = profiles();
    config.trophies = trophies;
    auto rejected = LocalWebSocketMatchServer::start(std::move(config), error);
    assert(rejected == nullptr);
    assert(error == "Public handle 'mara' for P2 is already in use.");
}

void authenticatedUnboundAccountCanHostLobby() {
    TemporaryTrophyDatabase database;
    std::string error;
    auto auth = SQLiteAccountAuth::open(database.path(), error);
    assert(auth != nullptr && error.empty());
    AccountIdentity account;
    assert(auth->createAccount(
        LoginIdentity{"lobby-host@example.test"}, "correct horse battery staple",
        PublicAccountProfile{PublicProfileHandle{"lobby-host"}, "Lobby Host"},
        account, error) == CreateAccountResult::Created);
    AuthSessionToken token;
    assert(auth->authenticate(
        LoginIdentity{"lobby-host@example.test"},
        "correct horse battery staple", token, error));

    LocalWebSocketServerConfig config;
    config.port = static_cast<std::uint16_t>(ix::getFreePort());
    config.authentication = auth;
    config.profiles = profiles();
    auto server = LocalWebSocketMatchServer::start(std::move(config), error);
    assert(server != nullptr && error.empty());
    auto client = WebSocketNetworkSession::connectForAuthentication(
        url(*server), error);
    assert(client != nullptr);
    assert(waitUntil([&] {
        client->pump();
        return client->state() == NetworkConnectionState::Connected;
    }));
    assert(client->authenticate({network::kProtocolVersion,
        network::AuthenticateSessionRequest{token.value}}));
    assert(waitUntil([&] {
        client->pump();
        return client->authenticationResponse().has_value();
    }));
    assert(client->controller() == nullptr);
    assert(client->requestLobby({network::kProtocolVersion,
        network::HostLobbyRequest{}}));
    assert(waitUntil([&] {
        client->pump();
        return client->lobbyResponse().has_value();
    }));
    assert(std::holds_alternative<network::LobbyHosted>(
        client->lobbyResponse()->payload));
}

void authenticatedAssignmentLaunchesAuthoritativeGameplay() {
    TemporaryTrophyDatabase database;
    std::string error;
    auto auth = SQLiteAccountAuth::open(database.path(), error);
    assert(auth != nullptr && error.empty());
    AccountIdentity hostAccount;
    AccountIdentity guestAccount;
    assert(auth->createAccount(
        LoginIdentity{"assigned-host@example.test"},
        "correct horse battery staple",
        PublicAccountProfile{PublicProfileHandle{"assigned-host"}, "Match Host"},
        hostAccount, error) == CreateAccountResult::Created);
    assert(auth->createAccount(
        LoginIdentity{"assigned-guest@example.test"},
        "correct horse battery staple",
        PublicAccountProfile{PublicProfileHandle{"assigned-guest"}, "Match Guest"},
        guestAccount, error) == CreateAccountResult::Created);
    AuthSessionToken hostToken;
    AuthSessionToken guestToken;
    assert(auth->authenticate(LoginIdentity{"assigned-host@example.test"},
                              "correct horse battery staple", hostToken, error));
    assert(auth->authenticate(LoginIdentity{"assigned-guest@example.test"},
                              "correct horse battery staple", guestToken, error));

    LocalWebSocketServerConfig config;
    config.port = static_cast<std::uint16_t>(ix::getFreePort());
    config.authentication = auth;
    config.profiles = profiles();
    auto server = LocalWebSocketMatchServer::start(std::move(config), error);
    assert(server != nullptr && error.empty());
    auto host = WebSocketNetworkSession::connectForAuthentication(
        url(*server), error);
    auto guest = WebSocketNetworkSession::connectForAuthentication(
        url(*server), error);
    assert(host != nullptr && guest != nullptr);
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->state() == NetworkConnectionState::Connected &&
            guest->state() == NetworkConnectionState::Connected;
    }));
    assert(host->authenticate({network::kProtocolVersion,
        network::AuthenticateSessionRequest{hostToken.value}}));
    assert(guest->authenticate({network::kProtocolVersion,
        network::AuthenticateSessionRequest{guestToken.value}}));
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->authenticationResponse().has_value() &&
            guest->authenticationResponse().has_value();
    }));

    assert(host->requestLobby({network::kProtocolVersion,
        network::HostLobbyRequest{}}));
    assert(waitUntil([&] {
        host->pump();
        return host->lobbyResponse().has_value() &&
            std::holds_alternative<network::LobbyHosted>(
                host->lobbyResponse()->payload);
    }));
    const std::string lobbyCode = std::get<network::LobbyHosted>(
        host->lobbyResponse()->payload).lobbyCode;
    assert(guest->requestLobby({network::kProtocolVersion,
        network::JoinLobbyRequest{lobbyCode}}));
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->controller() != nullptr && guest->controller() != nullptr &&
            host->lobbyResponse().has_value() &&
            guest->lobbyResponse().has_value() &&
            std::holds_alternative<network::LobbyMatchAssigned>(
                host->lobbyResponse()->payload) &&
            std::holds_alternative<network::LobbyMatchAssigned>(
                guest->lobbyResponse()->payload);
    }));
    assert(host->controller()->viewContext().localPlayer == PlayerId{1});
    assert(guest->controller()->viewContext().localPlayer == PlayerId{2});
    const RoundNumber initialRound =
        host->controller()->displayedSnapshot()->round;
    assert(guest->controller()->displayedSnapshot()->round == initialRound);

    assert(host->controller()->submitAndLock(searchAction(*host->controller())));
    assert(guest->controller()->submitAndLock(searchAction(*guest->controller())));
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->controller()->displayedSnapshot()->round > initialRound &&
            guest->controller()->displayedSnapshot()->round ==
                host->controller()->displayedSnapshot()->round;
    }));

    assert(host->controller()->quit());
    host->clearGameplaySession();
    assert(host->controller() == nullptr);
    assert(host->requestLobby({network::kProtocolVersion,
        network::FindMatchRequest{}}));
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->lobbyResponse().has_value() &&
            std::holds_alternative<network::MatchmakingQueued>(
                host->lobbyResponse()->payload);
    }));

    assert(host->controller() == nullptr);
}

void dynamicAssignedMatchesUsePersistentTrophyStore() {
    TemporaryTrophyDatabase authenticationDatabase;
    TemporaryTrophyDatabase trophyDatabase;
    std::string error;
    auto auth = SQLiteAccountAuth::open(authenticationDatabase.path(), error);
    assert(auth != nullptr && error.empty());
    const PublicAccountProfile hostProfile{
        PublicProfileHandle{"persistent-host"}, "Persistent Host"};
    const PublicAccountProfile guestProfile{
        PublicProfileHandle{"persistent-guest"}, "Persistent Guest"};
    AccountIdentity hostAccount;
    AccountIdentity guestAccount;
    assert(auth->createAccount(
        LoginIdentity{"persistent-host@example.test"},
        "correct horse battery staple", hostProfile, hostAccount, error) ==
        CreateAccountResult::Created);
    assert(auth->createAccount(
        LoginIdentity{"persistent-guest@example.test"},
        "correct horse battery staple", guestProfile, guestAccount, error) ==
        CreateAccountResult::Created);
    AuthSessionToken hostToken;
    AuthSessionToken guestToken;
    assert(auth->authenticate(LoginIdentity{"persistent-host@example.test"},
        "correct horse battery staple", hostToken, error));
    assert(auth->authenticate(LoginIdentity{"persistent-guest@example.test"},
        "correct horse battery staple", guestToken, error));

    auto seededPersistence = SQLiteTrophyPersistence::open(
        trophyDatabase.path(), error);
    assert(seededPersistence != nullptr && error.empty());
    const TrophyMatchId priorMatch{"persistent-prior-match"};
    const std::vector<TrophyLedgerEntry> priorEntries{
        {priorMatch, hostAccount, TrophyReason::Win, 2},
        {priorMatch, guestAccount, TrophyReason::Loss, -1},
    };
    assert(seededPersistence->appendMatch(
        priorMatch, priorEntries, error) == TrophyAppendResult::Appended);
    seededPersistence.reset();

    LocalWebSocketServerConfig config;
    config.port = static_cast<std::uint16_t>(ix::getFreePort());
    config.authentication = auth;
    config.trophyDatabasePath = trophyDatabase.path();
    config.profiles = profiles();
    auto server = LocalWebSocketMatchServer::start(std::move(config), error);
    assert(server != nullptr && error.empty());

    auto host = WebSocketNetworkSession::connectForAuthentication(
        url(*server), error);
    auto guest = WebSocketNetworkSession::connectForAuthentication(
        url(*server), error);
    assert(host != nullptr && guest != nullptr);
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->state() == NetworkConnectionState::Connected &&
            guest->state() == NetworkConnectionState::Connected;
    }));
    assert(host->authenticate({network::kProtocolVersion,
        network::AuthenticateSessionRequest{hostToken.value}}));
    assert(guest->authenticate({network::kProtocolVersion,
        network::AuthenticateSessionRequest{guestToken.value}}));
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->authenticationResponse().has_value() &&
            guest->authenticationResponse().has_value();
    }));
    assert(host->requestLobby({network::kProtocolVersion,
        network::HostLobbyRequest{}}));
    assert(waitUntil([&] {
        host->pump();
        return host->lobbyResponse().has_value() &&
            std::holds_alternative<network::LobbyHosted>(
                host->lobbyResponse()->payload);
    }));
    const std::string lobbyCode = std::get<network::LobbyHosted>(
        host->lobbyResponse()->payload).lobbyCode;
    assert(guest->requestLobby({network::kProtocolVersion,
        network::JoinLobbyRequest{lobbyCode}}));
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->controller() != nullptr && guest->controller() != nullptr;
    }));

    assert(host->requestLeaderboard(0, 10));
    assert(waitUntil([&] {
        host->pump();
        return host->leaderboardPage().has_value();
    }));
    const auto& publicEntries = host->leaderboardPage()->entries;
    assert(publicEntries.size() == 2);
    assert(publicEntries.front().handle == hostProfile.handle);
    assert(publicEntries.front().displayName == hostProfile.displayName);
    assert(publicEntries.front().trophyTotal == 2);

    host.reset();
    guest.reset();
    assert(waitUntil([&] { return server->connectedClientCount() == 0; }));
    server->advanceTime(30000);
    assert(server->trophyScoringError() == std::nullopt);
    server->stop();
    server.reset();
    assert(assignedScoredMatchCount(trophyDatabase.path()) == 1);

    auto reopenedPersistence = SQLiteTrophyPersistence::open(
        trophyDatabase.path(), error);
    assert(reopenedPersistence != nullptr && error.empty());
    std::optional<PublicAccountProfile> persistedProfile;
    assert(reopenedPersistence->profileForAccount(
        hostAccount, persistedProfile, error));
    assert(persistedProfile == hostProfile);
    assert(reopenedPersistence->profileForAccount(
        guestAccount, persistedProfile, error));
    assert(persistedProfile == guestProfile);
    auto reopenedLedger = std::make_shared<TrophyLedger>(reopenedPersistence);
    PublicTrophyReadModel reopenedReadModel(
        reopenedLedger, reopenedPersistence);
    std::vector<PublicTrophyLeaderboardEntry> reopenedLeaderboard;
    assert(reopenedReadModel.leaderboardPage(
        0, 10, reopenedLeaderboard, error));
    assert(reopenedLeaderboard.size() == 2);
    assert(reopenedLeaderboard.front().handle == hostProfile.handle);

    LocalWebSocketServerConfig restartedConfig;
    restartedConfig.port = static_cast<std::uint16_t>(ix::getFreePort());
    restartedConfig.authentication = auth;
    restartedConfig.trophyDatabasePath = trophyDatabase.path();
    restartedConfig.profiles = profiles();
    auto restarted = LocalWebSocketMatchServer::start(
        std::move(restartedConfig), error);
    assert(restarted != nullptr && error.empty());
    std::int64_t total = 0;
    assert(restarted->trophyTotal(hostAccount, total, error));
    assert(total == 2);
}

void authenticatedPlayerReclaimsAssignedMatchWithinGrace() {
    TemporaryTrophyDatabase database;
    std::string error;
    auto auth = SQLiteAccountAuth::open(database.path(), error);
    assert(auth != nullptr && error.empty());
    AccountIdentity hostAccount;
    AccountIdentity guestAccount;
    assert(auth->createAccount(
        LoginIdentity{"reconnect-host@example.test"},
        "correct horse battery staple",
        PublicAccountProfile{PublicProfileHandle{"reconnect-host"}, "Reconnect Host"},
        hostAccount, error) == CreateAccountResult::Created);
    assert(auth->createAccount(
        LoginIdentity{"reconnect-guest@example.test"},
        "correct horse battery staple",
        PublicAccountProfile{PublicProfileHandle{"reconnect-guest"}, "Reconnect Guest"},
        guestAccount, error) == CreateAccountResult::Created);
    AuthSessionToken hostToken;
    AuthSessionToken guestToken;
    assert(auth->authenticate(LoginIdentity{"reconnect-host@example.test"},
                              "correct horse battery staple", hostToken, error));
    assert(auth->authenticate(LoginIdentity{"reconnect-guest@example.test"},
                              "correct horse battery staple", guestToken, error));

    LocalWebSocketServerConfig config;
    config.port = static_cast<std::uint16_t>(ix::getFreePort());
    config.authentication = auth;
    config.profiles = profiles();
    auto server = LocalWebSocketMatchServer::start(std::move(config), error);
    assert(server != nullptr && error.empty());
    auto host = WebSocketNetworkSession::connectForAuthentication(url(*server), error);
    auto guest = WebSocketNetworkSession::connectForAuthentication(url(*server), error);
    assert(host != nullptr && guest != nullptr);
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->state() == NetworkConnectionState::Connected &&
            guest->state() == NetworkConnectionState::Connected;
    }));
    assert(host->authenticate({network::kProtocolVersion,
        network::AuthenticateSessionRequest{hostToken.value}}));
    assert(guest->authenticate({network::kProtocolVersion,
        network::AuthenticateSessionRequest{guestToken.value}}));
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->authenticationResponse().has_value() &&
            guest->authenticationResponse().has_value();
    }));
    assert(host->requestLobby({network::kProtocolVersion,
        network::HostLobbyRequest{}}));
    assert(waitUntil([&] {
        host->pump();
        return host->lobbyResponse().has_value() &&
            std::holds_alternative<network::LobbyHosted>(
                host->lobbyResponse()->payload);
    }));
    const std::string lobbyCode = std::get<network::LobbyHosted>(
        host->lobbyResponse()->payload).lobbyCode;
    assert(guest->requestLobby({network::kProtocolVersion,
        network::JoinLobbyRequest{lobbyCode}}));
    assert(waitUntil([&] {
        host->pump();
        guest->pump();
        return host->controller() != nullptr && guest->controller() != nullptr;
    }));

    const PlayerRoundSnapshot before = *host->controller()->displayedSnapshot();
    host.reset();
    assert(waitUntil([&] { return server->connectedClientCount() == 1; }));

    auto reclaimed = WebSocketNetworkSession::connectForAuthentication(
        url(*server), error);
    assert(reclaimed != nullptr);
    assert(waitUntil([&] {
        reclaimed->pump();
        return reclaimed->state() == NetworkConnectionState::Connected;
    }));
    assert(reclaimed->authenticate({network::kProtocolVersion,
        network::AuthenticateSessionRequest{hostToken.value}}));
    assert(waitUntil([&] {
        reclaimed->pump();
        guest->pump();
        return reclaimed->controller() != nullptr;
    }));
    const PlayerRoundSnapshot* resumed =
        reclaimed->controller()->displayedSnapshot();
    assert(resumed != nullptr);
    assert(resumed->player == before.player);
    assert(resumed->round == before.round);
    assert(resumed->currentCave == before.currentCave);
    assert(resumed->health == before.health);
    assert(resumed->inventory.items == before.inventory.items);
    assert(resumed->map.caves.size() == before.map.caves.size());

    assert(reclaimed->controller()->submitAndLock(
        searchAction(*reclaimed->controller())));
    assert(guest->controller()->submitAndLock(searchAction(*guest->controller())));
    assert(waitUntil([&] {
        reclaimed->pump();
        guest->pump();
        return reclaimed->controller()->displayedSnapshot()->round > before.round &&
            guest->controller()->displayedSnapshot()->round ==
                reclaimed->controller()->displayedSnapshot()->round;
    }));
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
    serverQueriesUseDurableAccountsAndSQLiteSurvivesRestart();
    serverPersistsPublicProfilesAndRejectsDuplicateHandles();
    authenticatedUnboundAccountCanHostLobby();
    authenticatedAssignmentLaunchesAuthoritativeGameplay();
    dynamicAssignedMatchesUsePersistentTrophyStore();
    authenticatedPlayerReclaimsAssignedMatchWithinGrace();
}
