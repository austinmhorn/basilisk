#include "AccountAuth.hpp"

#include <argon2.h>
#include <sqlite3.h>

#include <array>
#include <chrono>
#include <cerrno>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <Security/SecRandom.h>
#else
#include <sys/random.h>
#endif

namespace basilisk::game::server {
namespace {

constexpr std::uint32_t kArgon2Iterations{3};
constexpr std::uint32_t kArgon2MemoryKiB{64U * 1024U};
constexpr std::uint32_t kArgon2Parallelism{1};
constexpr std::size_t kSaltBytes{16};
constexpr std::size_t kHashBytes{32};
constexpr std::size_t kAccountRandomBytes{16};
constexpr std::size_t kTokenRandomBytes{32};

constexpr std::string_view schema = R"sql(
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS accounts (
    account_id TEXT PRIMARY KEY NOT NULL,
    login_identity TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS auth_sessions (
    session_token TEXT PRIMARY KEY NOT NULL,
    account_id TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,
    FOREIGN KEY (account_id) REFERENCES accounts(account_id)
);
CREATE INDEX IF NOT EXISTS auth_sessions_expiry ON auth_sessions(expires_at);
CREATE TABLE IF NOT EXISTS public_account_profiles (
    account_id TEXT PRIMARY KEY NOT NULL,
    public_handle TEXT UNIQUE NOT NULL,
    display_name TEXT NOT NULL,
    FOREIGN KEY (account_id) REFERENCES accounts(account_id)
);
)sql";

class Statement {
public:
    Statement(sqlite3* database, const char* sql, std::string& error) {
        if (sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr) !=
            SQLITE_OK) error = sqlite3_errmsg(database);
    }
    ~Statement() { if (statement_ != nullptr) sqlite3_finalize(statement_); }
    [[nodiscard]] sqlite3_stmt* get() const noexcept { return statement_; }
private:
    sqlite3_stmt* statement_{};
};

bool execute(sqlite3* database, std::string_view sql, std::string& error) {
    char* message = nullptr;
    const int result = sqlite3_exec(
        database, std::string{sql}.c_str(), nullptr, nullptr, &message);
    if (result == SQLITE_OK) {
        error.clear();
        return true;
    }
    error = message != nullptr ? message : sqlite3_errmsg(database);
    sqlite3_free(message);
    return false;
}

bool bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(
        statement, index, value.data(), static_cast<int>(value.size()),
        SQLITE_TRANSIENT) == SQLITE_OK;
}

std::int64_t systemTimeSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool secureRandom(std::span<unsigned char> bytes) {
#if defined(_WIN32)
    return BCryptGenRandom(
        nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    return SecRandomCopyBytes(
        kSecRandomDefault, bytes.size(), bytes.data()) == errSecSuccess;
#else
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t result = getrandom(
            bytes.data() + offset, bytes.size() - offset, 0);
        if (result > 0) offset += static_cast<std::size_t>(result);
        else if (result < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
#endif
}

std::string hex(std::span<const unsigned char> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result[index * 2] = digits[bytes[index] >> 4U];
        result[index * 2 + 1] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

bool validCredentialsInput(
    const LoginIdentity& login,
    const std::string& password) {
    return !login.value.empty() && login.value.size() <= 254 &&
           password.size() >= 8 && password.size() <= 1024;
}

bool passwordHash(
    const std::string& password,
    std::string& encoded,
    std::string& error) {
    std::array<unsigned char, kSaltBytes> salt{};
    if (!secureRandom(salt)) {
        error = "Unable to obtain secure randomness for password hashing.";
        return false;
    }
    const std::size_t encodedLength = argon2_encodedlen(
        kArgon2Iterations, kArgon2MemoryKiB, kArgon2Parallelism,
        kSaltBytes, kHashBytes, Argon2_id);
    encoded.assign(encodedLength, '\0');
    const int result = argon2id_hash_encoded(
        kArgon2Iterations, kArgon2MemoryKiB, kArgon2Parallelism,
        password.data(), password.size(), salt.data(), salt.size(),
        kHashBytes, encoded.data(), encoded.size());
    if (result != ARGON2_OK) {
        error = std::string{"Unable to hash password: "} +
            argon2_error_message(result);
        return false;
    }
    encoded.resize(std::char_traits<char>::length(encoded.c_str()));
    return true;
}

template <std::size_t Size>
bool randomIdentifier(std::string_view prefix, std::string& value) {
    std::array<unsigned char, Size> random{};
    if (!secureRandom(random)) return false;
    value = std::string{prefix} + hex(random);
    return true;
}

} // namespace

SQLiteAccountAuth::SQLiteAccountAuth(sqlite3* database, Clock clock)
    : database_(database), clock_(std::move(clock)) {}

SQLiteAccountAuth::~SQLiteAccountAuth() {
    if (database_ != nullptr) sqlite3_close(database_);
}

std::shared_ptr<SQLiteAccountAuth> SQLiteAccountAuth::open(
    const std::string& databasePath,
    std::string& error,
    Clock clock) {
    if (databasePath.empty()) {
        error = "Account database path must not be empty.";
        return nullptr;
    }
    sqlite3* database = nullptr;
    if (sqlite3_open_v2(databasePath.c_str(), &database,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr) != SQLITE_OK) {
        error = database == nullptr ? "Unable to open account database."
                                    : sqlite3_errmsg(database);
        if (database != nullptr) sqlite3_close(database);
        return nullptr;
    }
    if (!execute(database, schema, error)) {
        sqlite3_close(database);
        return nullptr;
    }
    if (!clock) clock = systemTimeSeconds;
    return std::shared_ptr<SQLiteAccountAuth>(
        new SQLiteAccountAuth(database, std::move(clock)));
}

CreateAccountResult SQLiteAccountAuth::createAccount(
    const LoginIdentity& login,
    const std::string& password,
    AccountIdentity& account,
    std::string& error) {
    if (!validCredentialsInput(login, password)) {
        error = "Login must be non-empty and password must contain 8-1024 bytes.";
        return CreateAccountResult::InvalidInput;
    }
    std::string encoded;
    if (!passwordHash(password, encoded, error))
        return CreateAccountResult::Error;
    AccountIdentity created;
    if (!randomIdentifier<kAccountRandomBytes>("acct_", created.value)) {
        error = "Unable to obtain secure randomness for account creation.";
        return CreateAccountResult::Error;
    }
    Statement insert(database_,
        "INSERT INTO accounts(account_id, login_identity, password_hash, "
        "created_at) VALUES(?, ?, ?, ?)", error);
    if (insert.get() == nullptr ||
        !bindText(insert.get(), 1, created.value) ||
        !bindText(insert.get(), 2, login.value) ||
        !bindText(insert.get(), 3, encoded) ||
        sqlite3_bind_int64(insert.get(), 4, clock_()) != SQLITE_OK) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return CreateAccountResult::Error;
    }
    const int result = sqlite3_step(insert.get());
    if (result == SQLITE_CONSTRAINT) {
        error = "Login identity is already registered.";
        return CreateAccountResult::DuplicateLogin;
    }
    if (result != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return CreateAccountResult::Error;
    }
    account = std::move(created);
    error.clear();
    return CreateAccountResult::Created;
}

CreateAccountResult SQLiteAccountAuth::createAccount(
    const LoginIdentity& login,
    const std::string& password,
    const PublicAccountProfile& profile,
    AccountIdentity& account,
    std::string& error) {
    if (profile.handle.value.empty() || profile.displayName.empty()) {
        error = "Public handle and display name must not be empty.";
        return CreateAccountResult::InvalidInput;
    }
    if (!execute(database_, "BEGIN IMMEDIATE", error))
        return CreateAccountResult::Error;
    AccountIdentity created;
    const CreateAccountResult createdResult = createAccount(
        login, password, created, error);
    if (createdResult != CreateAccountResult::Created) {
        std::string ignored;
        (void)execute(database_, "ROLLBACK", ignored);
        return createdResult;
    }
    Statement insert(database_,
        "INSERT INTO public_account_profiles(account_id, public_handle, "
        "display_name) VALUES(?, ?, ?)", error);
    if (insert.get() == nullptr ||
        !bindText(insert.get(), 1, created.value) ||
        !bindText(insert.get(), 2, profile.handle.value) ||
        !bindText(insert.get(), 3, profile.displayName) ||
        sqlite3_step(insert.get()) != SQLITE_DONE) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        std::string ignored;
        (void)execute(database_, "ROLLBACK", ignored);
        if (error.find("UNIQUE constraint failed: public_account_profiles.public_handle") !=
            std::string::npos)
            error = "Public handle is already registered.";
        return CreateAccountResult::Error;
    }
    if (!execute(database_, "COMMIT", error)) {
        std::string ignored;
        (void)execute(database_, "ROLLBACK", ignored);
        return CreateAccountResult::Error;
    }
    account = std::move(created);
    return CreateAccountResult::Created;
}

bool SQLiteAccountAuth::authenticate(
    const LoginIdentity& login,
    const std::string& password,
    AuthSessionToken& token,
    std::string& error,
    std::chrono::seconds lifetime) {
    if (!validCredentialsInput(login, password) || lifetime.count() <= 0 ||
        lifetime > defaultSessionLifetime) {
        error = "Invalid login credentials or session lifetime.";
        return false;
    }
    Statement query(database_,
        "SELECT account_id, password_hash FROM accounts WHERE login_identity = ?",
        error);
    if (query.get() == nullptr || !bindText(query.get(), 1, login.value)) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return false;
    }
    const int result = sqlite3_step(query.get());
    if (result != SQLITE_ROW) {
        error = result == SQLITE_DONE ? "Invalid login credentials."
                                      : sqlite3_errmsg(database_);
        return false;
    }
    const std::string account{
        reinterpret_cast<const char*>(sqlite3_column_text(query.get(), 0))};
    const char* encoded = reinterpret_cast<const char*>(
        sqlite3_column_text(query.get(), 1));
    if (argon2id_verify(encoded, password.data(), password.size()) != ARGON2_OK) {
        error = "Invalid login credentials.";
        return false;
    }
    AuthSessionToken created;
    if (!randomIdentifier<kTokenRandomBytes>("session_", created.value)) {
        error = "Unable to obtain secure randomness for session creation.";
        return false;
    }
    const std::int64_t now = clock_();
    Statement insert(database_,
        "INSERT INTO auth_sessions(session_token, account_id, created_at, "
        "expires_at) VALUES(?, ?, ?, ?)", error);
    if (insert.get() == nullptr ||
        !bindText(insert.get(), 1, created.value) ||
        !bindText(insert.get(), 2, account) ||
        sqlite3_bind_int64(insert.get(), 3, now) != SQLITE_OK ||
        sqlite3_bind_int64(insert.get(), 4, now + lifetime.count()) != SQLITE_OK ||
        sqlite3_step(insert.get()) != SQLITE_DONE) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return false;
    }
    token = std::move(created);
    error.clear();
    return true;
}

bool SQLiteAccountAuth::resolveSession(
    const AuthSessionToken& token,
    AccountIdentity& account,
    std::string& error) {
    if (token.value.empty()) {
        error = "Invalid or expired authentication session.";
        return false;
    }
    Statement query(database_,
        "SELECT account_id FROM auth_sessions WHERE session_token = ? AND "
        "expires_at > ?", error);
    if (query.get() == nullptr || !bindText(query.get(), 1, token.value) ||
        sqlite3_bind_int64(query.get(), 2, clock_()) != SQLITE_OK) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return false;
    }
    const int result = sqlite3_step(query.get());
    if (result != SQLITE_ROW) {
        error = result == SQLITE_DONE
            ? "Invalid or expired authentication session."
            : sqlite3_errmsg(database_);
        return false;
    }
    account.value = reinterpret_cast<const char*>(
        sqlite3_column_text(query.get(), 0));
    error.clear();
    return true;
}

bool SQLiteAccountAuth::publicProfile(
    const AccountIdentity& account,
    PublicAccountProfile& profile,
    std::string& error) {
    Statement query(database_,
        "SELECT public_handle, display_name FROM public_account_profiles "
        "WHERE account_id = ?", error);
    if (query.get() == nullptr || !bindText(query.get(), 1, account.value)) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return false;
    }
    const int result = sqlite3_step(query.get());
    if (result != SQLITE_ROW) {
        error = result == SQLITE_DONE ? "Public account profile is unavailable."
                                      : sqlite3_errmsg(database_);
        return false;
    }
    profile.handle.value = reinterpret_cast<const char*>(
        sqlite3_column_text(query.get(), 0));
    profile.displayName = reinterpret_cast<const char*>(
        sqlite3_column_text(query.get(), 1));
    error.clear();
    return true;
}

bool SQLiteAccountAuth::invalidateSession(
    const AuthSessionToken& token,
    std::string& error) {
    Statement remove(database_,
        "DELETE FROM auth_sessions WHERE session_token = ?", error);
    if (remove.get() == nullptr || !bindText(remove.get(), 1, token.value) ||
        sqlite3_step(remove.get()) != SQLITE_DONE) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return false;
    }
    if (sqlite3_changes(database_) != 1) {
        error = "Invalid or expired authentication session.";
        return false;
    }
    error.clear();
    return true;
}

} // namespace basilisk::game::server
