#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "PublicAccountProfiles.hpp"
#include "TrophyScoring.hpp"

struct sqlite3;

namespace basilisk::game::server {

class SQLiteTrophyPersistence final
    : public TrophyPersistence,
      public PublicAccountProfileStore {
public:
    [[nodiscard]] static std::shared_ptr<SQLiteTrophyPersistence> open(
        const std::string& databasePath,
        std::string& error);

    ~SQLiteTrophyPersistence() override;
    SQLiteTrophyPersistence(const SQLiteTrophyPersistence&) = delete;
    SQLiteTrophyPersistence& operator=(const SQLiteTrophyPersistence&) = delete;

    [[nodiscard]] TrophyAppendResult appendMatch(
        const TrophyMatchId& match,
        std::span<const TrophyLedgerEntry> entries,
        std::string& error) override;
    [[nodiscard]] bool loadEntries(
        std::vector<TrophyLedgerEntry>& entries,
        std::string& error) const override;
    [[nodiscard]] bool trophyTotal(
        const AccountIdentity& account,
        std::int64_t& total,
        std::string& error) const override;
    [[nodiscard]] bool leaderboard(
        std::vector<TrophyLeaderboardEntry>& entries,
        std::string& error) const override;
    [[nodiscard]] PublicProfileStoreResult storeProfile(
        const AccountIdentity& account,
        const PublicAccountProfile& profile,
        std::string& error) override;
    [[nodiscard]] bool profileForAccount(
        const AccountIdentity& account,
        std::optional<PublicAccountProfile>& profile,
        std::string& error) const override;

private:
    explicit SQLiteTrophyPersistence(sqlite3* database);

    sqlite3* database_{};
    mutable std::mutex mutex_;
};

} // namespace basilisk::game::server
