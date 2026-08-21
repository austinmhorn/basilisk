#pragma once

#include <map>
#include <memory>
#include <set>
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
};

// Append-only in-memory ledger. Persistence is intentionally deferred; callers
// supply durable account identities rather than transient match PlayerIds.
class TrophyLedger {
public:
    [[nodiscard]] TrophyScoreResult scoreMatch(
        const TrophyMatchId& match,
        const std::map<PlayerId, AccountIdentity>& accounts,
        const MatchResult& result,
        std::span<const GameEvent> authoritativeEvents);

    [[nodiscard]] std::span<const TrophyLedgerEntry> entries() const noexcept {
        return entries_;
    }

private:
    std::set<TrophyMatchId> scoredMatches_;
    std::vector<TrophyLedgerEntry> entries_;
};

struct TrophyScoringContext {
    TrophyMatchId match;
    std::map<PlayerId, AccountIdentity> accounts;
    std::shared_ptr<TrophyLedger> ledger;
};

} // namespace basilisk::game::server
