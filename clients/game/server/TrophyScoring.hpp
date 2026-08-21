#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "basilisk/Event.hpp"
#include "basilisk/MatchResult.hpp"

namespace basilisk::game::server {

struct TrophyMatchId {
    std::string value;

    auto operator<=>(const TrophyMatchId&) const = default;
};

struct AccountIdentity {
    std::string value;

    auto operator<=>(const AccountIdentity&) const = default;
};

enum class TrophyReason {
    Win,
    Loss,
    PlayerKill,
    Extraction,
};

struct TrophyLedgerEntry {
    TrophyMatchId match;
    AccountIdentity account;
    TrophyReason reason{};
    int delta{};

    bool operator==(const TrophyLedgerEntry&) const = default;
};

enum class TrophyScoreResult {
    Scored,
    NotTerminal,
    AlreadyScored,
    PersistenceError,
};

struct TrophyLeaderboardEntry {
    AccountIdentity account;
    std::int64_t total{};

    bool operator==(const TrophyLeaderboardEntry&) const = default;
};

enum class TrophyAppendResult {
    Appended,
    DuplicateMatch,
    Error,
};

// Server-side persistence seam. Implementations atomically claim a match ID
// before appending its immutable rows and derive all totals from those rows.
class TrophyPersistence {
public:
    virtual ~TrophyPersistence() = default;

    [[nodiscard]] virtual TrophyAppendResult appendMatch(
        const TrophyMatchId& match,
        std::span<const TrophyLedgerEntry> entries,
        std::string& error) = 0;
    [[nodiscard]] virtual bool loadEntries(
        std::vector<TrophyLedgerEntry>& entries,
        std::string& error) const = 0;
    [[nodiscard]] virtual bool trophyTotal(
        const AccountIdentity& account,
        std::int64_t& total,
        std::string& error) const = 0;
    [[nodiscard]] virtual bool leaderboard(
        std::vector<TrophyLeaderboardEntry>& entries,
        std::string& error) const = 0;
};

[[nodiscard]] std::shared_ptr<TrophyPersistence>
makeInMemoryTrophyPersistence();

// Authoritative scoring remains independent of the persistence implementation.
// Callers supply durable account identities rather than transient PlayerIds.
class TrophyLedger {
public:
    TrophyLedger();
    explicit TrophyLedger(std::shared_ptr<TrophyPersistence> persistence);

    [[nodiscard]] TrophyScoreResult scoreMatch(
        const TrophyMatchId& match,
        const std::map<PlayerId, AccountIdentity>& accounts,
        const MatchResult& result,
        std::span<const GameEvent> authoritativeEvents);

    [[nodiscard]] bool loadEntries(
        std::vector<TrophyLedgerEntry>& entries,
        std::string& error) const;
    [[nodiscard]] bool trophyTotal(
        const AccountIdentity& account,
        std::int64_t& total,
        std::string& error) const;
    [[nodiscard]] bool leaderboard(
        std::vector<TrophyLeaderboardEntry>& entries,
        std::string& error) const;

private:
    std::shared_ptr<TrophyPersistence> persistence_;
};

struct TrophyScoringContext {
    TrophyMatchId match;
    std::map<PlayerId, AccountIdentity> accounts;
    std::shared_ptr<TrophyLedger> ledger;
};

} // namespace basilisk::game::server
