#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

#include "AccountAuth.hpp"
#include "AccountAuthProtocol.hpp"
#include "NetworkWireCodec.hpp"

using namespace basilisk::game::server;

namespace {

template <typename T>
concept HasEmailMember = requires(T value) { value.email; };

static_assert(!HasEmailMember<basilisk::game::PublicAccountProfile>);
static_assert(!HasEmailMember<basilisk::game::PublicTrophyLeaderboardEntry>);
static_assert(!HasEmailMember<basilisk::client::PublicPlayerProfile>);

class TemporaryDatabase {
public:
    TemporaryDatabase() {
        path_ = std::filesystem::temp_directory_path() /
            ("basilisk-auth-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".sqlite3");
    }
    ~TemporaryDatabase() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
    }
    [[nodiscard]] std::string path() const { return path_.string(); }
private:
    std::filesystem::path path_;
};

} // namespace

int main() {
    TemporaryDatabase database;
    std::int64_t now = 1'000'000;
    std::string error;
    auto auth = SQLiteAccountAuth::open(
        database.path(), error, [&now] { return now; });
    assert(auth != nullptr && error.empty());

    AccountIdentity created;
    assert(auth->createAccount(
        EmailAddress{"mara@example.test"}, "correct horse battery staple",
        created, error) == CreateAccountResult::Created);
    assert(created.value.starts_with("acct_"));

    AccountIdentity duplicate;
    assert(auth->createAccount(
        EmailAddress{"mara@example.test"}, "another strong password",
        duplicate, error) == CreateAccountResult::DuplicateEmail);

    auth.reset();
    auth = SQLiteAccountAuth::open(
        database.path(), error, [&now] { return now; });
    assert(auth != nullptr && error.empty());

    AuthSessionToken rejected;
    assert(!auth->authenticate(
        EmailAddress{"mara@example.test"}, "incorrect password",
        rejected, error));
    assert(error == "Invalid email or password.");

    AuthSessionToken session;
    assert(auth->authenticate(
        EmailAddress{"mara@example.test"}, "correct horse battery staple",
        session, error));
    assert(session.value.starts_with("session_"));
    assert(session.value != created.value);

    AccountIdentity resolved;
    assert(auth->resolveSession(session, resolved, error));
    assert(resolved == created);

    basilisk::client::AccountCosmeticLoadout loadout;
    assert(auth->cosmeticLoadout(created, loadout, error));
    assert(loadout == basilisk::client::AccountCosmeticLoadout{});
    const basilisk::client::AccountCosmeticLoadout equipped{
        basilisk::client::CallingCardId{"honeycomb-flag-green"},
        basilisk::client::EmblemId{"circle-green"}};
    // Unsupported IDs never replace the stored loadout.
    basilisk::client::AccountCosmeticLoadout confirmed;
    assert(!auth->updateCosmeticLoadout(session, equipped, confirmed, error));
    const basilisk::client::AccountCosmeticLoadout supported{
        basilisk::client::CallingCardId{"honeycomb-flag-black"},
        basilisk::client::EmblemId{"circle-green"}};
    assert(auth->updateCosmeticLoadout(
        session, supported, confirmed, error));
    assert(confirmed == supported);
    auth.reset();
    auth = SQLiteAccountAuth::open(
        database.path(), error, [&now] { return now; });
    assert(auth != nullptr && auth->cosmeticLoadout(created, loadout, error));
    assert(loadout == supported);

    const std::int64_t sessionCreatedAt = now;
    now += 16 * 60;
    assert(auth->resolveSession(session, resolved, error));
    assert(resolved == created);

    now = sessionCreatedAt + SQLiteAccountAuth::defaultSessionLifetime.count();
    assert(!auth->resolveSession(session, resolved, error));
    assert(error == "Invalid or expired authentication session.");

    now = 1'000'000;
    AccountAuthProtocol protocol{auth};
    using namespace basilisk::game::network;
    const basilisk::game::PublicAccountProfile profile{
        basilisk::game::Username{"mara-voss"}};
    WireBytes requestBytes;
    WireBytes responseBytes;
    AuthenticationResponse response;
    assert(encodeWire(AuthenticationRequest{
        kProtocolVersion,
        CreateAccountRequest{
            "mara2@example.test", "another correct horse battery staple",
            profile.username.value}}, requestBytes, error));
    assert(protocol.process(requestBytes, responseBytes, error));
    assert(decodeAuthenticationResponse(responseBytes, response, error));
    const auto* createdResponse =
        std::get_if<AuthenticationSuccess>(&response.payload);
    assert(createdResponse != nullptr);
    assert(createdResponse->profile == profile);
    assert(createdResponse->cosmeticLoadout ==
           basilisk::client::AccountCosmeticLoadout{});
    const std::string createdSession = createdResponse->sessionToken;

    assert(encodeWire(AuthenticationRequest{
        kProtocolVersion,
        LoginRequest{"mara2@example.test",
                     "another correct horse battery staple"}},
        requestBytes, error));
    assert(protocol.process(requestBytes, responseBytes, error));
    assert(decodeAuthenticationResponse(responseBytes, response, error));
    const auto* loginResponse =
        std::get_if<AuthenticationSuccess>(&response.payload);
    assert(loginResponse != nullptr);
    assert(loginResponse->profile == profile);
    assert(loginResponse->cosmeticLoadout ==
           basilisk::client::AccountCosmeticLoadout{});

    const basilisk::client::AccountCosmeticLoadout protocolLoadout{
        basilisk::client::CallingCardId{"diamonds-flag-white"},
        basilisk::client::EmblemId{"rounded-square-green"}};
    assert(encodeWire(CosmeticLoadoutUpdateRequest{
        kProtocolVersion, createdSession, protocolLoadout},
        requestBytes, error));
    assert(protocol.processCosmeticUpdate(requestBytes, responseBytes, error));
    CosmeticLoadoutUpdateResponse cosmeticResponse;
    assert(decodeCosmeticLoadoutUpdateResponse(
        responseBytes, cosmeticResponse, error));
    const auto* cosmeticSuccess = std::get_if<CosmeticLoadoutUpdateSuccess>(
        &cosmeticResponse.payload);
    assert(cosmeticSuccess != nullptr &&
           cosmeticSuccess->loadout == protocolLoadout);

    AccountIdentity duplicateUsername;
    assert(auth->createAccount(
        EmailAddress{"other@example.test"}, "another secure password",
        profile, duplicateUsername, error) ==
        CreateAccountResult::DuplicateUsername);

    assert(encodeWire(AuthenticationRequest{
        kProtocolVersion, AuthenticateSessionRequest{createdSession}},
        requestBytes, error));
    assert(protocol.process(requestBytes, responseBytes, error));
    assert(decodeAuthenticationResponse(responseBytes, response, error));
    const auto* authenticated =
        std::get_if<AuthenticationSuccess>(&response.payload);
    assert(authenticated != nullptr);
    assert(authenticated->sessionToken == createdSession);
    assert(authenticated->profile == profile);
    assert(authenticated->cosmeticLoadout == protocolLoadout);

    assert(encodeWire(AuthenticationRequest{
        kProtocolVersion,
        LoginRequest{"mara2@example.test", "wrong password"}},
        requestBytes, error));
    assert(protocol.process(requestBytes, responseBytes, error));
    assert(decodeAuthenticationResponse(responseBytes, response, error));
    assert(std::holds_alternative<AuthenticationFailure>(response.payload));

    assert(encodeWire(AuthenticationRequest{
        kProtocolVersion, LogoutRequest{createdSession}}, requestBytes, error));
    assert(protocol.process(requestBytes, responseBytes, error));
    assert(decodeAuthenticationResponse(responseBytes, response, error));
    assert(std::holds_alternative<LogoutSuccess>(response.payload));
    assert(!auth->resolveSession(
        AuthSessionToken{createdSession}, resolved, error));

    assert(encodeWire(AuthenticationRequest{
        kProtocolVersion,
        LoginRequest{"mara2@example.test",
                     "another correct horse battery staple"}},
        requestBytes, error));
    assert(protocol.process(requestBytes, responseBytes, error));
    assert(decodeAuthenticationResponse(responseBytes, response, error));
    const auto* relogged = std::get_if<AuthenticationSuccess>(&response.payload);
    assert(relogged != nullptr && relogged->cosmeticLoadout == protocolLoadout);
}
