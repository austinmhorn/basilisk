#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "SQLiteTrophyPersistence.hpp"

using namespace basilisk;
using namespace basilisk::game::server;

namespace {

class TemporaryDatabase {
public:
    TemporaryDatabase() {
        const auto suffix = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("basilisk-trophies-" + std::to_string(suffix) + ".sqlite3");
    }

    ~TemporaryDatabase() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::shared_ptr<SQLiteTrophyPersistence> open(
    const TemporaryDatabase& database) {

    std::string error;
    auto persistence = SQLiteTrophyPersistence::open(
        database.path().string(), error);
    assert(persistence != nullptr);
    assert(error.empty());
    return persistence;
}

std::vector<TrophyLedgerEntry> load(const TrophyPersistence& persistence) {
    std::vector<TrophyLedgerEntry> entries;
    std::string error;
    assert(persistence.loadEntries(entries, error));
    assert(error.empty());
    return entries;
}

std::int64_t total(
    const TrophyPersistence& persistence,
    std::string account) {

    std::int64_t result = 0;
    std::string error;
    assert(persistence.trophyTotal(AccountIdentity{std::move(account)}, result, error));
    assert(error.empty());
    return result;
}

TrophyLedgerEntry entry(
    std::string match,
    std::string account,
    TrophyReason reason,
    int delta) {

    return {
        TrophyMatchId{std::move(match)},
        AccountIdentity{std::move(account)},
        reason,
        delta,
    };
}

void entriesAndTotalsSurviveReload() {
    TemporaryDatabase database;
    {
        const auto persistence = open(database);
        const std::vector entries{
            entry("match-one", "account-a", TrophyReason::Win, 2),
            entry("match-one", "account-a", TrophyReason::PlayerKill, 1),
            entry("match-one", "account-b", TrophyReason::Loss, -1),
        };
        std::string error;
        assert(persistence->appendMatch(
            TrophyMatchId{"match-one"}, entries, error) ==
            TrophyAppendResult::Appended);
        assert(error.empty());
    }

    const auto persistence = open(database);
    const auto entries = load(*persistence);
    assert(entries.size() == 3);
    assert(total(*persistence, "account-a") == 3);
    assert(total(*persistence, "account-b") == -1);
    assert(total(*persistence, "unknown-account") == 0);

    const auto summed = std::accumulate(
        entries.begin(), entries.end(), std::int64_t{0},
        [](std::int64_t result, const TrophyLedgerEntry& row) {
            return result + (row.account == AccountIdentity{"account-a"}
                ? row.delta
                : 0);
        });
    assert(total(*persistence, "account-a") == summed);
}

void duplicateMatchIsRejectedAcrossReload() {
    TemporaryDatabase database;
    {
        auto ledger = TrophyLedger(open(database));
        const std::map<PlayerId, AccountIdentity> accounts{
            {PlayerId{1}, AccountIdentity{"account-a"}},
            {PlayerId{2}, AccountIdentity{"account-b"}},
        };
        const MatchResult result{
            MatchStatus::Completed,
            MatchOutcome::BasiliskKilled,
            PlayerId{1},
        };
        assert(ledger.scoreMatch(
            TrophyMatchId{"match-once"}, accounts, result, {}) ==
            TrophyScoreResult::Scored);
    }

    auto ledger = TrophyLedger(open(database));
    const std::map<PlayerId, AccountIdentity> accounts{
        {PlayerId{1}, AccountIdentity{"account-a"}},
        {PlayerId{2}, AccountIdentity{"account-b"}},
    };
    const MatchResult result{
        MatchStatus::Completed,
        MatchOutcome::BasiliskKilled,
        PlayerId{1},
    };
    assert(ledger.scoreMatch(
        TrophyMatchId{"match-once"}, accounts, result, {}) ==
        TrophyScoreResult::AlreadyScored);
    assert(total(*open(database), "account-a") == 2);
    assert(total(*open(database), "account-b") == -1);
}

void leaderboardUsesTotalsAndDeterministicTies() {
    TemporaryDatabase database;
    const auto persistence = open(database);
    std::string error;
    const std::vector first{
        entry("match-alpha", "account-charlie", TrophyReason::Win, 5),
        entry("match-alpha", "account-alpha", TrophyReason::Win, 5),
        entry("match-alpha", "account-low", TrophyReason::Loss, -1),
    };
    assert(persistence->appendMatch(
        TrophyMatchId{"match-alpha"}, first, error) ==
        TrophyAppendResult::Appended);
    const std::vector second{
        entry("match-beta", "account-bravo", TrophyReason::Win, 3),
        entry("match-beta", "account-bravo", TrophyReason::PlayerKill, 2),
        entry("match-beta", "account-low", TrophyReason::PlayerKill, 1),
    };
    assert(persistence->appendMatch(
        TrophyMatchId{"match-beta"}, second, error) ==
        TrophyAppendResult::Appended);

    std::vector<TrophyLeaderboardEntry> leaderboard;
    assert(persistence->leaderboard(leaderboard, error));
    assert(error.empty());
    const std::vector<TrophyLeaderboardEntry> expected{
        {AccountIdentity{"account-alpha"}, 5},
        {AccountIdentity{"account-bravo"}, 5},
        {AccountIdentity{"account-charlie"}, 5},
        {AccountIdentity{"account-low"}, 0},
    };
    assert(leaderboard == expected);
}

} // namespace

int main() {
    entriesAndTotalsSurviveReload();
    duplicateMatchIsRejectedAcrossReload();
    leaderboardUsesTotalsAndDeterministicTies();
}
