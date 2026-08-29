#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <concepts>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "ActionCommands.hpp"
#include "AuthoritativeInMemoryMatch.hpp"
#include "NetworkWireCodec.hpp"
#include "SQLiteTrophyPersistence.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/systems/MatchCoordinator.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;
using namespace basilisk::game::server;
namespace network = basilisk::game::network;

namespace {

template <typename T>
concept HasPrivateAccount = requires(T value) { value.account; };

template <typename T>
concept HasAccountId = requires(T value) { value.accountId; };

static_assert(!HasPrivateAccount<PublicAccountProfile>);
static_assert(!HasAccountId<PublicAccountProfile>);
static_assert(!HasPrivateAccount<PublicTrophyLeaderboardEntry>);
static_assert(!HasAccountId<PublicTrophyLeaderboardEntry>);

const std::map<PlayerId, AccountIdentity> accounts{
    {PlayerId{1}, AccountIdentity{"durable-account-one"}},
    {PlayerId{2}, AccountIdentity{"durable-account-two"}},
};

std::vector<client::PublicPlayerProfile> profiles() {
    return {
        {PlayerId{1}, "Mara", client::CallingCardId{"card-one"},
         client::EmblemId{"emblem-one"}},
        {PlayerId{2}, "Elias", client::CallingCardId{"card-two"},
         client::EmblemId{"emblem-two"}},
    };
}

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

void publicProfilesPersistAndEnforceStableUniqueUsernames() {
    TemporaryDatabase database;
    const AccountIdentity firstAccount{"private-account-one"};
    const PublicAccountProfile firstProfile{
        Username{"hunter-one"},
    };
    {
        const auto persistence = open(database);
        std::string error;
        assert(persistence->storeProfile(
            firstAccount, firstProfile, error) ==
            PublicProfileStoreResult::Stored);
        assert(persistence->storeProfile(
            firstAccount, firstProfile, error) ==
            PublicProfileStoreResult::AlreadyStored);
        assert(persistence->storeProfile(
            firstAccount,
            PublicAccountProfile{Username{"changed"}},
            error) == PublicProfileStoreResult::AccountConflict);
        assert(persistence->storeProfile(
            AccountIdentity{"private-account-two"},
            PublicAccountProfile{firstProfile.username},
            error) == PublicProfileStoreResult::DuplicateUsername);
    }

    const auto reopened = open(database);
    std::string error;
    std::optional<PublicAccountProfile> loaded;
    assert(reopened->profileForAccount(firstAccount, loaded, error));
    assert(loaded == firstProfile);
    assert(reopened->profileForAccount(
        AccountIdentity{"missing-private-account"}, loaded, error));
    assert(!loaded.has_value());
}

void publicLeaderboardCombinesProfilesWithLedgerTotals() {
    TemporaryDatabase database;
    const auto persistence = open(database);
    std::string error;
    const std::vector rows{
        entry("public-board", "private-missing", TrophyReason::Win, 8),
        entry("public-board", "private-zulu", TrophyReason::Win, 5),
        entry("public-board", "private-alpha", TrophyReason::Win, 5),
        entry("public-board", "private-low", TrophyReason::Loss, 0),
    };
    assert(persistence->appendMatch(
        TrophyMatchId{"public-board"}, rows, error) ==
        TrophyAppendResult::Appended);
    assert(persistence->storeProfile(
        AccountIdentity{"private-zulu"},
        PublicAccountProfile{Username{"zulu"}},
        error) == PublicProfileStoreResult::Stored);
    assert(persistence->storeProfile(
        AccountIdentity{"private-alpha"},
        PublicAccountProfile{Username{"alpha"}},
        error) == PublicProfileStoreResult::Stored);
    assert(persistence->storeProfile(
        AccountIdentity{"private-low"},
        PublicAccountProfile{Username{"low"}},
        error) == PublicProfileStoreResult::Stored);

    auto ledger = std::make_shared<TrophyLedger>(persistence);
    PublicTrophyReadModel readModel{ledger, persistence};
    std::vector<PublicTrophyLeaderboardEntry> leaderboard;
    assert(readModel.leaderboardPage(0, 10, leaderboard, error));
    const std::vector<PublicTrophyLeaderboardEntry> expected{
        {1, Username{"alpha"}, 5},
        {1, Username{"zulu"}, 5},
        {3, Username{"low"}, 0},
    };
    assert(leaderboard == expected);

    // Missing profiles are intentionally omitted, and private identifiers
    // cannot appear in the public model even when they have the highest total.
    assert(std::ranges::none_of(leaderboard, [](const auto& entry) {
        return entry.username.value.starts_with("private-");
    }));

    std::vector<PublicTrophyLeaderboardEntry> page;
    assert(readModel.leaderboardPage(1, 1, page, error));
    assert(page == std::vector<PublicTrophyLeaderboardEntry>{expected[1]});
    assert(readModel.leaderboardPage(0, 2, page, error));
    assert((page == std::vector<PublicTrophyLeaderboardEntry>{
        expected[0], expected[1]}));
}

void publicLeaderboardReadsFreshPersistedTotalsWithoutCaching() {
    TemporaryDatabase database;
    auto persistence = open(database);
    std::string error;
    const AccountIdentity alpha{"private-alpha-fresh"};
    const AccountIdentity bravo{"private-bravo-fresh"};
    assert(persistence->storeProfile(
        alpha, PublicAccountProfile{Username{"alpha-fresh"}}, error) ==
        PublicProfileStoreResult::Stored);
    assert(persistence->storeProfile(
        bravo, PublicAccountProfile{Username{"bravo-fresh"}}, error) ==
        PublicProfileStoreResult::Stored);

    auto ledger = std::make_shared<TrophyLedger>(persistence);
    PublicTrophyReadModel readModel{ledger, persistence};
    std::vector<PublicTrophyLeaderboardEntry> page;
    assert(readModel.leaderboardPage(0, 10, page, error));
    assert(page.empty());

    const MatchResult alphaWin{
        MatchStatus::Completed, MatchOutcome::BasiliskKilled, PlayerId{1}};
    const std::map<PlayerId, AccountIdentity> firstAccounts{
        {PlayerId{1}, alpha}, {PlayerId{2}, bravo}};
    assert(ledger->scoreMatch(
        TrophyMatchId{"fresh-alpha-win"}, firstAccounts, alphaWin, {}) ==
        TrophyScoreResult::Scored);
    assert(readModel.leaderboardPage(0, 10, page, error));
    assert((page == std::vector<PublicTrophyLeaderboardEntry>{
        {1, Username{"alpha-fresh"}, 2},
        {2, Username{"bravo-fresh"}, -1},
    }));

    const MatchResult bravoWin{
        MatchStatus::Completed, MatchOutcome::EscapedWithSigil, PlayerId{2}};
    assert(ledger->scoreMatch(
        TrophyMatchId{"fresh-bravo-win"}, firstAccounts, bravoWin, {}) ==
        TrophyScoreResult::Scored);
    assert(readModel.leaderboardPage(0, 10, page, error));
    assert((page == std::vector<PublicTrophyLeaderboardEntry>{
        {1, Username{"bravo-fresh"}, 2},
        {2, Username{"alpha-fresh"}, 1},
    }));

    // Duplicate terminal processing leaves both persisted totals and ordering
    // unchanged.
    assert(ledger->scoreMatch(
        TrophyMatchId{"fresh-bravo-win"}, firstAccounts, bravoWin, {}) ==
        TrophyScoreResult::AlreadyScored);
    std::vector<PublicTrophyLeaderboardEntry> duplicatePage;
    assert(readModel.leaderboardPage(0, 10, duplicatePage, error));
    assert(duplicatePage == page);

    // Internal callers handle out-of-range pagination safely; the wire codec
    // separately rejects a zero page size.
    assert(readModel.leaderboardPage(
        std::numeric_limits<std::size_t>::max(), 10, duplicatePage, error));
    assert(duplicatePage.empty());
    assert(readModel.leaderboardPage(0, 0, duplicatePage, error));
    assert(duplicatePage.empty());

    persistence.reset();
    auto reopenedPersistence = open(database);
    auto reopenedLedger = std::make_shared<TrophyLedger>(reopenedPersistence);
    PublicTrophyReadModel reopened{
        reopenedLedger, reopenedPersistence};
    assert(reopened.leaderboardPage(0, 10, duplicatePage, error));
    assert(duplicatePage == page);
}

void concurrentCompletionsRemainIsolatedAndIdempotent() {
    TemporaryDatabase database;
    auto persistence = open(database);
    std::string error;
    const AccountIdentity alpha{"concurrent-alpha"};
    const AccountIdentity bravo{"concurrent-bravo"};
    const AccountIdentity charlie{"concurrent-charlie"};
    const AccountIdentity delta{"concurrent-delta"};
    for (const auto& [account, username] : std::vector{
             std::pair{alpha, Username{"alpha-concurrent"}},
             std::pair{bravo, Username{"bravo-concurrent"}},
             std::pair{charlie, Username{"charlie-concurrent"}},
             std::pair{delta, Username{"delta-concurrent"}},
         }) {
        assert(persistence->storeProfile(
            account, PublicAccountProfile{username}, error) ==
            PublicProfileStoreResult::Stored);
    }

    auto ledger = std::make_shared<TrophyLedger>(persistence);
    const std::map<PlayerId, AccountIdentity> firstAccounts{
        {PlayerId{1}, alpha}, {PlayerId{2}, bravo}};
    const std::map<PlayerId, AccountIdentity> secondAccounts{
        {PlayerId{1}, charlie}, {PlayerId{2}, delta}};
    const MatchResult firstResult{
        MatchStatus::Completed, MatchOutcome::BasiliskKilled, PlayerId{1}};
    const MatchResult secondResult{
        MatchStatus::Completed, MatchOutcome::EscapedWithSigil, PlayerId{2}};
    std::atomic<bool> begin{false};
    TrophyScoreResult firstScore = TrophyScoreResult::PersistenceError;
    TrophyScoreResult secondScore = TrophyScoreResult::PersistenceError;
    std::thread first([&] {
        while (!begin.load(std::memory_order_acquire)) std::this_thread::yield();
        firstScore = ledger->scoreMatch(
            TrophyMatchId{"concurrent-first"}, firstAccounts, firstResult, {});
    });
    std::thread second([&] {
        while (!begin.load(std::memory_order_acquire)) std::this_thread::yield();
        secondScore = ledger->scoreMatch(
            TrophyMatchId{"concurrent-second"}, secondAccounts, secondResult, {});
    });
    begin.store(true, std::memory_order_release);
    first.join();
    second.join();
    assert(firstScore == TrophyScoreResult::Scored);
    assert(secondScore == TrophyScoreResult::Scored);

    // Two racing terminal handlers for the same match still claim it once.
    std::atomic<bool> replayBegin{false};
    TrophyScoreResult replayOne = TrophyScoreResult::PersistenceError;
    TrophyScoreResult replayTwo = TrophyScoreResult::PersistenceError;
    const auto replay = [&](TrophyScoreResult& result) {
        while (!replayBegin.load(std::memory_order_acquire))
            std::this_thread::yield();
        result = ledger->scoreMatch(
            TrophyMatchId{"concurrent-replay"}, firstAccounts, firstResult, {});
    };
    std::thread replayFirst(replay, std::ref(replayOne));
    std::thread replaySecond(replay, std::ref(replayTwo));
    replayBegin.store(true, std::memory_order_release);
    replayFirst.join();
    replaySecond.join();
    assert((replayOne == TrophyScoreResult::Scored &&
            replayTwo == TrophyScoreResult::AlreadyScored) ||
           (replayTwo == TrophyScoreResult::Scored &&
            replayOne == TrophyScoreResult::AlreadyScored));

    PublicTrophyReadModel readModel{ledger, persistence};
    std::vector<PublicTrophyLeaderboardEntry> page;
    assert(readModel.leaderboardPage(0, 10, page, error));
    const std::vector<PublicTrophyLeaderboardEntry> expected{
        {1, Username{"alpha-concurrent"}, 4},
        {2, Username{"delta-concurrent"}, 3},
        {3, Username{"charlie-concurrent"}, -1},
        {4, Username{"bravo-concurrent"}, -2},
    };
    assert(page == expected);

    persistence.reset();
    ledger.reset();
    auto reopenedPersistence = open(database);
    auto reopenedLedger = std::make_shared<TrophyLedger>(reopenedPersistence);
    PublicTrophyReadModel reopened{reopenedLedger, reopenedPersistence};
    std::vector<PublicTrophyLeaderboardEntry> reopenedPage;
    assert(reopened.leaderboardPage(0, 10, reopenedPage, error));
    assert(reopenedPage == expected);
}

void sandboxCompletionDoesNotAffectPersistedLeaderboard() {
    TemporaryDatabase database;
    auto persistence = open(database);
    std::string error;
    for (const auto& [player, account] : accounts) {
        assert(persistence->storeProfile(
            account,
            PublicAccountProfile{Username{
                player == PlayerId{1} ? "sandbox-alpha" : "sandbox-bravo"}},
            error) == PublicProfileStoreResult::Stored);
    }
    const std::vector seeded{
        entry("eligible-before-sandbox", accounts.at(PlayerId{1}).value,
              TrophyReason::Win, 2),
        entry("eligible-before-sandbox", accounts.at(PlayerId{2}).value,
              TrophyReason::Loss, -1),
    };
    assert(persistence->appendMatch(
        TrophyMatchId{"eligible-before-sandbox"}, seeded, error) ==
        TrophyAppendResult::Appended);
    auto ledger = std::make_shared<TrophyLedger>(persistence);
    PublicTrophyReadModel readModel{ledger, persistence};
    std::vector<PublicTrophyLeaderboardEntry> before;
    assert(readModel.leaderboardPage(0, 10, before, error));

    auto config = client::defaultSandboxSessionConfig(2);
    config.humanPlayerCount = 2;
    config.mapSeed = MapSeed{20260816};
    config.matchSeed = MatchSeed{424242};
    auto sandbox = AuthoritativeInMemoryMatch::createSandbox(
        config, profiles(), {}, error);
    assert(sandbox != nullptr && error.empty());
    auto p1 = sandbox->connect(PlayerId{1}, error);
    auto p2 = sandbox->connect(PlayerId{2}, error);
    assert(p1 != nullptr && p2 != nullptr);
    assert(p1->send(network::ClientCommand{
        network::kProtocolVersion, network::QuitCommand{PlayerId{1}}}));
    assert(p2->send(network::ClientCommand{
        network::kProtocolVersion, network::QuitCommand{PlayerId{2}}}));

    std::vector<PublicTrophyLeaderboardEntry> after;
    assert(readModel.leaderboardPage(0, 10, after, error));
    assert(after == before);
}

void unfinishedAuthoritativeMatchDoesNotClaimItsId() {
    TemporaryDatabase database;
    auto ledger = std::make_shared<TrophyLedger>(open(database));
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error,
        TrophyScoringContext{
            TrophyMatchId{"unfinished-match"}, accounts, ledger});
    assert(host != nullptr && error.empty());
    auto p1 = host->connect(PlayerId{1}, error);
    auto p2 = host->connect(PlayerId{2}, error);
    assert(p1 != nullptr && p2 != nullptr);

    assert(load(*open(database)).empty());
    assert(ledger->scoreMatch(
        TrophyMatchId{"unfinished-match"}, accounts,
        MatchResult{MatchStatus::Completed, MatchOutcome::Draw, std::nullopt},
        {}) == TrophyScoreResult::Scored);
}

void authoritativeZeroEntryDrawIsClaimedOnce() {
    TemporaryDatabase database;
    auto ledger = std::make_shared<TrophyLedger>(open(database));
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error,
        TrophyScoringContext{
            TrophyMatchId{"authoritative-draw"}, accounts, ledger});
    assert(host != nullptr && error.empty());
    auto p1 = host->connect(PlayerId{1}, error);
    auto p2 = host->connect(PlayerId{2}, error);
    assert(p1 != nullptr && p2 != nullptr);
    assert(p1->send(network::ClientCommand{
        network::kProtocolVersion, network::QuitCommand{PlayerId{1}}}));
    assert(p2->send(network::ClientCommand{
        network::kProtocolVersion, network::QuitCommand{PlayerId{2}}}));
    host->advanceTime(30'000);

    assert(!host->trophyScoringError().has_value());
    assert(load(*open(database)).empty());
    TrophyLedger reopened(open(database));
    assert(reopened.scoreMatch(
        TrophyMatchId{"authoritative-draw"}, accounts,
        MatchResult{MatchStatus::Completed, MatchOutcome::Draw, std::nullopt},
        {}) == TrophyScoreResult::AlreadyScored);
}

class FailingPersistence final : public TrophyPersistence {
public:
    TrophyAppendResult appendMatch(
        const TrophyMatchId&,
        std::span<const TrophyLedgerEntry>,
        std::string& error) override {
        error = "simulated database failure";
        return TrophyAppendResult::Error;
    }
    bool loadEntries(
        std::vector<TrophyLedgerEntry>&,
        std::string& error) const override {
        error = "simulated database failure";
        return false;
    }
    bool trophyTotal(
        const AccountIdentity&,
        std::int64_t&,
        std::string& error) const override {
        error = "simulated database failure";
        return false;
    }
    bool leaderboard(
        std::vector<TrophyLeaderboardEntry>&,
        std::string& error) const override {
        error = "simulated database failure";
        return false;
    }
};

class FailOncePersistence final : public TrophyPersistence {
public:
    TrophyAppendResult appendMatch(
        const TrophyMatchId& match,
        std::span<const TrophyLedgerEntry> entries,
        std::string& error) override {
        ++appendAttempts;
        if (appendAttempts == 1) {
            error = "simulated transient database failure";
            return TrophyAppendResult::Error;
        }
        return delegate_->appendMatch(match, entries, error);
    }

    bool loadEntries(
        std::vector<TrophyLedgerEntry>& entries,
        std::string& error) const override {
        return delegate_->loadEntries(entries, error);
    }

    bool trophyTotal(
        const AccountIdentity& account,
        std::int64_t& result,
        std::string& error) const override {
        return delegate_->trophyTotal(account, result, error);
    }

    bool leaderboard(
        std::vector<TrophyLeaderboardEntry>& entries,
        std::string& error) const override {
        return delegate_->leaderboard(entries, error);
    }

    int appendAttempts{};

private:
    std::shared_ptr<TrophyPersistence> delegate_{
        makeInMemoryTrophyPersistence()};
};

void persistenceFailureDoesNotCorruptTerminalGameplayState() {
    auto ledger = std::make_shared<TrophyLedger>(
        std::make_shared<FailingPersistence>());
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error,
        TrophyScoringContext{
            TrophyMatchId{"failed-persistence"}, accounts, ledger});
    assert(host != nullptr && error.empty());
    auto p1 = host->connect(PlayerId{1}, error);
    auto p2 = host->connect(PlayerId{2}, error);
    assert(p1 != nullptr && p2 != nullptr);
    (void)p1->takeNextServerFrame();
    (void)p2->takeNextServerFrame();
    assert(p1->send(network::ClientCommand{
        network::kProtocolVersion, network::QuitCommand{PlayerId{1}}}));
    assert(p2->send(network::ClientCommand{
        network::kProtocolVersion, network::QuitCommand{PlayerId{2}}}));
    host->advanceTime(30'000);

    const auto scoringError = host->trophyScoringError();
    assert(scoringError.has_value());
    assert(scoringError->find("simulated database failure") !=
           std::string::npos);
    network::ServerUpdate update;
    bool receivedUpdate = false;
    while (auto updateFrame = p1->takeNextServerFrame()) {
        assert(network::decodeServerUpdate(*updateFrame, update, error));
        receivedUpdate = true;
    }
    assert(receivedUpdate);
    assert(update.snapshot.matchStatus == MatchStatus::Completed);
    assert(update.snapshot.matchOutcome == MatchOutcome::Draw);
}

void transientPersistenceFailureRetriesAndFinalizesOnce() {
    auto persistence = std::make_shared<FailOncePersistence>();
    auto ledger = std::make_shared<TrophyLedger>(persistence);
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error,
        TrophyScoringContext{
            TrophyMatchId{"retry-terminal-draw"}, accounts, ledger});
    assert(host != nullptr && error.empty());
    auto p1 = host->connect(PlayerId{1}, error);
    auto p2 = host->connect(PlayerId{2}, error);
    assert(p1 != nullptr && p2 != nullptr);
    assert(p1->send(network::ClientCommand{
        network::kProtocolVersion, network::QuitCommand{PlayerId{1}}}));
    assert(p2->send(network::ClientCommand{
        network::kProtocolVersion, network::QuitCommand{PlayerId{2}}}));

    assert(persistence->appendAttempts == 1);
    assert(host->trophyScoringError().has_value());
    host->advanceTime(1);
    assert(persistence->appendAttempts == 2);
    assert(!host->trophyScoringError().has_value());

    // Once SQLite/in-memory persistence has durably claimed the MatchId,
    // terminal ticks cannot append or award it again.
    host->advanceTime(1);
    host->advanceTime(30'000);
    assert(persistence->appendAttempts == 2);
    assert(ledger->scoreMatch(
        TrophyMatchId{"retry-terminal-draw"}, accounts,
        MatchResult{MatchStatus::Completed, MatchOutcome::Draw, std::nullopt},
        {}) == TrophyScoreResult::AlreadyScored);
}

struct DirectClient {
    std::shared_ptr<InMemoryMatchEndpoint> endpoint;
    PlayerRoundSnapshot snapshot;
    std::int64_t trophyTotal{};

    void ingest() {
        std::string error;
        while (auto frame = endpoint->takeNextServerFrame()) {
            network::ServerUpdate update;
            assert(network::decodeServerUpdate(*frame, update, error));
            snapshot = std::move(update.snapshot);
            trophyTotal = update.trophyTotal;
        }
    }
};

DirectClient connectDirect(
    AuthoritativeInMemoryMatch& host,
    PlayerId player) {

    std::string error;
    auto endpoint = host.connect(player, error);
    assert(endpoint != nullptr && error.empty());
    auto frame = endpoint->takeNextServerFrame();
    assert(frame.has_value());
    network::ServerBootstrap bootstrap;
    assert(network::decodeServerBootstrap(*frame, bootstrap, error));
    return {
        std::move(endpoint),
        std::move(bootstrap.initialSnapshot),
        bootstrap.trophyTotal,
    };
}

std::vector<CaveId> shortestPhysicalPath(
    const MatchState& state,
    CaveId source,
    CaveId destination) {

    std::queue<CaveId> pending;
    std::map<CaveId, CaveId> parent;
    pending.push(source);
    parent.emplace(source, source);
    while (!pending.empty() && !parent.contains(destination)) {
        const CaveId cave = pending.front();
        pending.pop();
        for (const CaveId next : state.world.cave(cave).connections) {
            if (parent.emplace(next, cave).second) pending.push(next);
        }
    }
    if (!parent.contains(destination)) return {};
    std::vector<CaveId> path;
    for (CaveId cave = destination;; cave = parent.at(cave)) {
        path.push_back(cave);
        if (cave == source) break;
    }
    std::ranges::reverse(path);
    return path;
}

const AvailableAction& actionToward(
    const PlayerRoundSnapshot& snapshot,
    const MatchState& state,
    ActionType type,
    CaveId destination) {

    const auto& connections = state.world.cave(snapshot.currentCave).connections;
    const auto physical = std::find(
        connections.begin(), connections.end(), destination);
    assert(physical != connections.end());
    const TunnelId tunnel = static_cast<TunnelId>(
        std::distance(connections.begin(), physical) + 1);
    const auto action = std::find_if(
        snapshot.availableActions.begin(), snapshot.availableActions.end(),
        [&](const AvailableAction& candidate) {
            return candidate.type == type &&
                (candidate.targetCave == destination ||
                 candidate.targetTunnel == tunnel);
        });
    assert(action != snapshot.availableActions.end());
    return *action;
}

const AvailableAction& search(const PlayerRoundSnapshot& snapshot) {
    const auto action = std::find_if(
        snapshot.availableActions.begin(), snapshot.availableActions.end(),
        [](const AvailableAction& candidate) {
            return candidate.type == ActionType::Search;
        });
    assert(action != snapshot.availableActions.end());
    return *action;
}

AvailableAction huntingAction(
    const PlayerRoundSnapshot& snapshot,
    const MatchState& state,
    bool designatedHunter) {

    if (!designatedHunter || snapshot.arrows == 0) return search(snapshot);
    const auto path = shortestPhysicalPath(
        state, snapshot.currentCave, state.basilisk.cave);
    assert(!path.empty());
    if (path.size() == 1) {
        const auto move = std::find_if(
            snapshot.availableActions.begin(), snapshot.availableActions.end(),
            [](const AvailableAction& candidate) {
                return candidate.type == ActionType::Move;
            });
        assert(move != snapshot.availableActions.end());
        return *move;
    }
    if (path.size() == 2)
        return actionToward(snapshot, state, ActionType::Shoot, path[1]);
    return actionToward(snapshot, state, ActionType::Move, path[1]);
}

void realAuthoritativeWinnerPersistsAwardsForDurableAccounts() {
    TemporaryDatabase database;
    auto persistence = open(database);
    std::string error;
    assert(persistence->storeProfile(
        accounts.at(PlayerId{1}), PublicAccountProfile{Username{"mara"}},
        error) == PublicProfileStoreResult::Stored);
    assert(persistence->storeProfile(
        accounts.at(PlayerId{2}), PublicAccountProfile{Username{"elias"}},
        error) == PublicProfileStoreResult::Stored);
    auto ledger = std::make_shared<TrophyLedger>(persistence);
    PublicTrophyReadModel readModel{ledger, persistence};
    constexpr MapSeed mapSeed{20260816};
    constexpr MatchSeed matchSeed{424242};
    auto host = AuthoritativeInMemoryMatch::create(
        mapSeed, matchSeed, profiles(), error,
        TrophyScoringContext{
            TrophyMatchId{"real-authoritative-win"}, accounts, ledger});
    assert(host != nullptr && error.empty());
    DirectClient p1 = connectDirect(*host, PlayerId{1});
    DirectClient p2 = connectDirect(*host, PlayerId{2});

    MatchState mirror = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator mirrorCoordinator(mirror);
    for (int turn = 0;
         turn < 100 && mirror.result.status == MatchStatus::Active;
         ++turn) {
        PlayerId hunter{};
        for (const PlayerState& player : mirror.players) {
            if (player.alive && player.arrows > 0) {
                hunter = player.id;
                break;
            }
        }
        for (DirectClient* client : {&p1, &p2}) {
            const auto player = std::find_if(
                mirror.players.begin(), mirror.players.end(),
                [&](const PlayerState& candidate) {
                    return candidate.id == client->snapshot.player;
                });
            if (player == mirror.players.end() || !player->alive) continue;
            const AvailableAction available = huntingAction(
                client->snapshot, mirror, player->id == hunter);
            const PlayerAction action = basilisk::game::makePlayerAction(
                available, player->id);
            assert(client->endpoint->send(network::ClientCommand{
                network::kProtocolVersion,
                network::SubmitActionCommand{mirror.round, action}}));
            assert(client->endpoint->send(network::ClientCommand{
                network::kProtocolVersion,
                network::LockActionCommand{mirror.round, player->id}}));
            assert(mirrorCoordinator.submitAction(action));
            assert(mirrorCoordinator.lockAction(player->id));
        }
        p1.ingest();
        p2.ingest();
    }
    assert(mirror.result.status == MatchStatus::Completed);
    assert(mirror.result.winner.has_value());
    assert(!host->trophyScoringError().has_value());

    const AccountIdentity winner = accounts.at(*mirror.result.winner);
    const AccountIdentity loser = accounts.at(
        *mirror.result.winner == PlayerId{1} ? PlayerId{2} : PlayerId{1});
    assert(total(*open(database), winner.value) >= 2);
    assert(total(*open(database), loser.value) == -1);
    DirectClient& winnerClient = *mirror.result.winner == PlayerId{1}
        ? p1
        : p2;
    const DirectClient& loserClient = *mirror.result.winner == PlayerId{1}
        ? p2
        : p1;
    assert(winnerClient.trophyTotal == total(*open(database), winner.value));
    assert(loserClient.trophyTotal == -1);

    std::vector<PublicTrophyLeaderboardEntry> terminalLeaderboard;
    assert(readModel.leaderboardPage(0, 10, terminalLeaderboard, error));
    assert(terminalLeaderboard.size() == 2);
    assert(terminalLeaderboard.front().trophyTotal ==
           winnerClient.trophyTotal);
    assert(terminalLeaderboard.back().trophyTotal == -1);

    const std::int64_t winnerTotal = winnerClient.trophyTotal;
    const std::int64_t loserTotal = loserClient.trophyTotal;
    winnerClient.endpoint->disconnect();
    auto terminalReconnect = host->reconnect(*mirror.result.winner, error);
    assert(terminalReconnect != nullptr && error.empty());
    auto terminalBootstrapFrame = terminalReconnect->takeNextServerFrame();
    assert(terminalBootstrapFrame.has_value());
    network::ServerBootstrap terminalBootstrap;
    assert(network::decodeServerBootstrap(
        *terminalBootstrapFrame, terminalBootstrap, error));
    assert(terminalBootstrap.initialSnapshot.matchStatus ==
           MatchStatus::Completed);
    assert(terminalBootstrap.trophyTotal == winnerTotal);

    // Reconnect publication and repeated terminal processing cannot append a
    // second award for either durable account.
    host->advanceTime(1);
    host->advanceTime(30'000);
    assert(total(*open(database), winner.value) == winnerTotal);
    assert(total(*open(database), loser.value) == loserTotal);

    TrophyLedger reopened(open(database));
    assert(reopened.scoreMatch(
        TrophyMatchId{"real-authoritative-win"}, accounts,
        mirror.result, mirrorCoordinator.authoritativeEvents()) ==
        TrophyScoreResult::AlreadyScored);
}

} // namespace

int main() {
    entriesAndTotalsSurviveReload();
    duplicateMatchIsRejectedAcrossReload();
    leaderboardUsesTotalsAndDeterministicTies();
    publicProfilesPersistAndEnforceStableUniqueUsernames();
    publicLeaderboardCombinesProfilesWithLedgerTotals();
    publicLeaderboardReadsFreshPersistedTotalsWithoutCaching();
    concurrentCompletionsRemainIsolatedAndIdempotent();
    sandboxCompletionDoesNotAffectPersistedLeaderboard();
    unfinishedAuthoritativeMatchDoesNotClaimItsId();
    authoritativeZeroEntryDrawIsClaimedOnce();
    persistenceFailureDoesNotCorruptTerminalGameplayState();
    transientPersistenceFailureRetriesAndFinalizesOnce();
    realAuthoritativeWinnerPersistsAwardsForDurableAccounts();
}
