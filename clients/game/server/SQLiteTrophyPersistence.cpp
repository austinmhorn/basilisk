#include "SQLiteTrophyPersistence.hpp"

#include <sqlite3.h>

#include <string_view>

namespace basilisk::game::server {
namespace {

constexpr std::string_view kSchema = R"SQL(
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS trophy_scored_matches (
    match_id TEXT PRIMARY KEY NOT NULL
);
CREATE TABLE IF NOT EXISTS trophy_ledger (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id TEXT NOT NULL,
    match_id TEXT NOT NULL,
    reason TEXT NOT NULL CHECK (
        reason IN ('Win', 'Loss', 'PlayerKill', 'Extraction')
    ),
    delta INTEGER NOT NULL,
    FOREIGN KEY (match_id) REFERENCES trophy_scored_matches(match_id)
);
CREATE INDEX IF NOT EXISTS trophy_ledger_account
    ON trophy_ledger(account_id);
CREATE TABLE IF NOT EXISTS public_account_profiles (
    account_id TEXT PRIMARY KEY NOT NULL,
    public_handle TEXT UNIQUE NOT NULL,
    display_name TEXT NOT NULL,
    CHECK (length(public_handle) > 0),
    CHECK (length(display_name) > 0)
);
CREATE TRIGGER IF NOT EXISTS trophy_ledger_no_update
BEFORE UPDATE ON trophy_ledger BEGIN
    SELECT RAISE(ABORT, 'trophy ledger rows are immutable');
END;
CREATE TRIGGER IF NOT EXISTS trophy_ledger_no_delete
BEFORE DELETE ON trophy_ledger BEGIN
    SELECT RAISE(ABORT, 'trophy ledger rows are immutable');
END;
CREATE TRIGGER IF NOT EXISTS trophy_matches_no_update
BEFORE UPDATE ON trophy_scored_matches BEGIN
    SELECT RAISE(ABORT, 'scored match rows are immutable');
END;
CREATE TRIGGER IF NOT EXISTS trophy_matches_no_delete
BEFORE DELETE ON trophy_scored_matches BEGIN
    SELECT RAISE(ABORT, 'scored match rows are immutable');
END;
)SQL";

class Statement {
public:
    Statement(sqlite3* database, const char* sql, std::string& error) {
        const int result = sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) error = sqlite3_errmsg(database);
        else error.clear();
    }

    ~Statement() {
        if (statement_ != nullptr) sqlite3_finalize(statement_);
    }

    [[nodiscard]] sqlite3_stmt* get() const noexcept { return statement_; }

private:
    sqlite3_stmt* statement_{};
};

bool execute(sqlite3* database, std::string_view sql, std::string& error) {
    char* message = nullptr;
    const int result = sqlite3_exec(
        database, std::string(sql).c_str(), nullptr, nullptr, &message);
    if (result == SQLITE_OK) {
        error.clear();
        return true;
    }
    error = message != nullptr ? message : sqlite3_errmsg(database);
    sqlite3_free(message);
    return false;
}

const char* reasonName(TrophyReason reason) {
    switch (reason) {
    case TrophyReason::Win: return "Win";
    case TrophyReason::Loss: return "Loss";
    case TrophyReason::PlayerKill: return "PlayerKill";
    case TrophyReason::Extraction: return "Extraction";
    }
    return "";
}

bool parseReason(const unsigned char* text, TrophyReason& reason) {
    if (text == nullptr) return false;
    const std::string_view value{reinterpret_cast<const char*>(text)};
    if (value == "Win") reason = TrophyReason::Win;
    else if (value == "Loss") reason = TrophyReason::Loss;
    else if (value == "PlayerKill") reason = TrophyReason::PlayerKill;
    else if (value == "Extraction") reason = TrophyReason::Extraction;
    else return false;
    return true;
}

bool bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(
        statement, index, value.c_str(),
        static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}

void rollback(sqlite3* database) {
    char* message = nullptr;
    (void)sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, &message);
    sqlite3_free(message);
}

} // namespace

SQLiteTrophyPersistence::SQLiteTrophyPersistence(sqlite3* database)
    : database_(database) {}

SQLiteTrophyPersistence::~SQLiteTrophyPersistence() {
    if (database_ != nullptr) sqlite3_close(database_);
}

std::shared_ptr<SQLiteTrophyPersistence> SQLiteTrophyPersistence::open(
    const std::string& databasePath,
    std::string& error) {

    sqlite3* database = nullptr;
    const int result = sqlite3_open_v2(
        databasePath.c_str(), &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (result != SQLITE_OK) {
        error = database != nullptr
            ? sqlite3_errmsg(database)
            : "Unable to allocate SQLite database handle.";
        if (database != nullptr) sqlite3_close(database);
        return nullptr;
    }
    auto persistence = std::shared_ptr<SQLiteTrophyPersistence>(
        new SQLiteTrophyPersistence(database));
    if (!execute(database, kSchema, error)) return nullptr;
    error.clear();
    return persistence;
}

TrophyAppendResult SQLiteTrophyPersistence::appendMatch(
    const TrophyMatchId& match,
    std::span<const TrophyLedgerEntry> entries,
    std::string& error) {

    std::lock_guard lock(mutex_);
    if (match.value.empty()) {
        error = "Trophy match ID must not be empty.";
        return TrophyAppendResult::Error;
    }
    for (const TrophyLedgerEntry& entry : entries) {
        if (entry.match != match || entry.account.value.empty()) {
            error = "Trophy ledger rows must match the claimed match and account.";
            return TrophyAppendResult::Error;
        }
    }
    if (!execute(database_, "BEGIN IMMEDIATE", error))
        return TrophyAppendResult::Error;

    Statement matchInsert(
        database_,
        "INSERT INTO trophy_scored_matches(match_id) VALUES(?)",
        error);
    if (matchInsert.get() == nullptr ||
        !bindText(matchInsert.get(), 1, match.value)) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        rollback(database_);
        return TrophyAppendResult::Error;
    }
    const int matchResult = sqlite3_step(matchInsert.get());
    if (matchResult == SQLITE_CONSTRAINT) {
        rollback(database_);
        error.clear();
        return TrophyAppendResult::DuplicateMatch;
    }
    if (matchResult != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        rollback(database_);
        return TrophyAppendResult::Error;
    }

    Statement entryInsert(
        database_,
        "INSERT INTO trophy_ledger(account_id, match_id, reason, delta) "
        "VALUES(?, ?, ?, ?)",
        error);
    if (entryInsert.get() == nullptr) {
        rollback(database_);
        return TrophyAppendResult::Error;
    }
    for (const TrophyLedgerEntry& entry : entries) {
        sqlite3_reset(entryInsert.get());
        sqlite3_clear_bindings(entryInsert.get());
        const std::string reason{reasonName(entry.reason)};
        if (!bindText(entryInsert.get(), 1, entry.account.value) ||
            !bindText(entryInsert.get(), 2, match.value) ||
            !bindText(entryInsert.get(), 3, reason) ||
            sqlite3_bind_int(entryInsert.get(), 4, entry.delta) != SQLITE_OK ||
            sqlite3_step(entryInsert.get()) != SQLITE_DONE) {
            error = sqlite3_errmsg(database_);
            rollback(database_);
            return TrophyAppendResult::Error;
        }
    }
    if (!execute(database_, "COMMIT", error)) {
        rollback(database_);
        return TrophyAppendResult::Error;
    }
    error.clear();
    return TrophyAppendResult::Appended;
}

bool SQLiteTrophyPersistence::loadEntries(
    std::vector<TrophyLedgerEntry>& entries,
    std::string& error) const {

    std::lock_guard lock(mutex_);
    Statement query(
        database_,
        "SELECT match_id, account_id, reason, delta "
        "FROM trophy_ledger ORDER BY id",
        error);
    if (query.get() == nullptr) return false;
    entries.clear();
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(query.get())) == SQLITE_ROW) {
        TrophyReason reason;
        if (!parseReason(sqlite3_column_text(query.get(), 2), reason)) {
            error = "SQLite trophy ledger contains an unknown reason.";
            return false;
        }
        entries.push_back({
            TrophyMatchId{reinterpret_cast<const char*>(
                sqlite3_column_text(query.get(), 0))},
            AccountIdentity{reinterpret_cast<const char*>(
                sqlite3_column_text(query.get(), 1))},
            reason,
            sqlite3_column_int(query.get(), 3),
        });
    }
    if (result != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return false;
    }
    error.clear();
    return true;
}

bool SQLiteTrophyPersistence::trophyTotal(
    const AccountIdentity& account,
    std::int64_t& total,
    std::string& error) const {

    std::lock_guard lock(mutex_);
    Statement query(
        database_,
        "SELECT COALESCE(SUM(delta), 0) FROM trophy_ledger "
        "WHERE account_id = ?",
        error);
    if (query.get() == nullptr ||
        !bindText(query.get(), 1, account.value) ||
        sqlite3_step(query.get()) != SQLITE_ROW) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return false;
    }
    total = sqlite3_column_int64(query.get(), 0);
    error.clear();
    return true;
}

bool SQLiteTrophyPersistence::leaderboard(
    std::vector<TrophyLeaderboardEntry>& entries,
    std::string& error) const {

    std::lock_guard lock(mutex_);
    Statement query(
        database_,
        "SELECT account_id, SUM(delta) AS total FROM trophy_ledger "
        "GROUP BY account_id ORDER BY total DESC, account_id ASC",
        error);
    if (query.get() == nullptr) return false;
    entries.clear();
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(query.get())) == SQLITE_ROW) {
        entries.push_back({
            AccountIdentity{reinterpret_cast<const char*>(
                sqlite3_column_text(query.get(), 0))},
            sqlite3_column_int64(query.get(), 1),
        });
    }
    if (result != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return false;
    }
    error.clear();
    return true;
}

PublicProfileStoreResult SQLiteTrophyPersistence::storeProfile(
    const AccountIdentity& account,
    const PublicAccountProfile& profile,
    std::string& error) {

    std::lock_guard lock(mutex_);
    if (account.value.empty() || profile.handle.value.empty() ||
        profile.displayName.empty()) {
        error = "Account, public handle, and display name must not be empty.";
        return PublicProfileStoreResult::Error;
    }
    Statement existing(
        database_,
        "SELECT public_handle, display_name FROM public_account_profiles "
        "WHERE account_id = ?",
        error);
    if (existing.get() == nullptr ||
        !bindText(existing.get(), 1, account.value)) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return PublicProfileStoreResult::Error;
    }
    const int existingResult = sqlite3_step(existing.get());
    if (existingResult == SQLITE_ROW) {
        const PublicAccountProfile stored{
            PublicProfileHandle{reinterpret_cast<const char*>(
                sqlite3_column_text(existing.get(), 0))},
            reinterpret_cast<const char*>(
                sqlite3_column_text(existing.get(), 1)),
        };
        error.clear();
        return stored == profile
            ? PublicProfileStoreResult::AlreadyStored
            : PublicProfileStoreResult::AccountConflict;
    }
    if (existingResult != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return PublicProfileStoreResult::Error;
    }

    Statement insert(
        database_,
        "INSERT INTO public_account_profiles"
        "(account_id, public_handle, display_name) VALUES(?, ?, ?)",
        error);
    if (insert.get() == nullptr ||
        !bindText(insert.get(), 1, account.value) ||
        !bindText(insert.get(), 2, profile.handle.value) ||
        !bindText(insert.get(), 3, profile.displayName)) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return PublicProfileStoreResult::Error;
    }
    const int result = sqlite3_step(insert.get());
    if (result == SQLITE_CONSTRAINT) {
        error.clear();
        return PublicProfileStoreResult::DuplicateHandle;
    }
    if (result != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return PublicProfileStoreResult::Error;
    }
    error.clear();
    return PublicProfileStoreResult::Stored;
}

bool SQLiteTrophyPersistence::profileForAccount(
    const AccountIdentity& account,
    std::optional<PublicAccountProfile>& profile,
    std::string& error) const {

    std::lock_guard lock(mutex_);
    Statement query(
        database_,
        "SELECT public_handle, display_name FROM public_account_profiles "
        "WHERE account_id = ?",
        error);
    if (query.get() == nullptr ||
        !bindText(query.get(), 1, account.value)) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return false;
    }
    const int result = sqlite3_step(query.get());
    if (result == SQLITE_DONE) {
        profile.reset();
        error.clear();
        return true;
    }
    if (result != SQLITE_ROW) {
        error = sqlite3_errmsg(database_);
        return false;
    }
    profile = PublicAccountProfile{
        PublicProfileHandle{reinterpret_cast<const char*>(
            sqlite3_column_text(query.get(), 0))},
        reinterpret_cast<const char*>(sqlite3_column_text(query.get(), 1)),
    };
    error.clear();
    return true;
}

} // namespace basilisk::game::server
