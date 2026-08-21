#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

#include "AccountAuth.hpp"

using namespace basilisk::game::server;

namespace {

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
        LoginIdentity{"mara@example.test"}, "correct horse battery staple",
        created, error) == CreateAccountResult::Created);
    assert(created.value.starts_with("acct_"));

    AccountIdentity duplicate;
    assert(auth->createAccount(
        LoginIdentity{"mara@example.test"}, "another strong password",
        duplicate, error) == CreateAccountResult::DuplicateLogin);

    auth.reset();
    auth = SQLiteAccountAuth::open(
        database.path(), error, [&now] { return now; });
    assert(auth != nullptr && error.empty());

    AuthSessionToken rejected;
    assert(!auth->authenticate(
        LoginIdentity{"mara@example.test"}, "incorrect password",
        rejected, error));
    assert(error == "Invalid login credentials.");

    AuthSessionToken session;
    assert(auth->authenticate(
        LoginIdentity{"mara@example.test"}, "correct horse battery staple",
        session, error));
    assert(session.value.starts_with("session_"));
    assert(session.value != created.value);

    AccountIdentity resolved;
    assert(auth->resolveSession(session, resolved, error));
    assert(resolved == created);

    now += SQLiteAccountAuth::defaultSessionLifetime.count();
    assert(!auth->resolveSession(session, resolved, error));
    assert(error == "Invalid or expired authentication session.");
}
