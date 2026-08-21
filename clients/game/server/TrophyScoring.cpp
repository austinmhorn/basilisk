#include "TrophyScoring.hpp"

#include <algorithm>
#include <numeric>
#include <set>
#include <utility>

namespace basilisk::game::server {
namespace {

class InMemoryTrophyPersistence final : public TrophyPersistence {
public:
    TrophyAppendResult appendMatch(
        const TrophyMatchId& match,
        std::span<const TrophyLedgerEntry> entries,
        std::string& error) override {

        if (!scoredMatches_.insert(match).second) {
            error.clear();
            return TrophyAppendResult::DuplicateMatch;
        }
        entries_.insert(entries_.end(), entries.begin(), entries.end());
        error.clear();
        return TrophyAppendResult::Appended;
    }

    bool loadEntries(
        std::vector<TrophyLedgerEntry>& entries,
        std::string& error) const override {

        entries = entries_;
        error.clear();
        return true;
    }

    bool trophyTotal(
        const AccountIdentity& account,
        std::int64_t& total,
        std::string& error) const override {

        total = std::accumulate(
            entries_.begin(), entries_.end(), std::int64_t{0},
            [&](std::int64_t sum, const TrophyLedgerEntry& entry) {
                return sum + (entry.account == account ? entry.delta : 0);
            });
        error.clear();
        return true;
    }

    bool leaderboard(
        std::vector<TrophyLeaderboardEntry>& entries,
        std::string& error) const override {

        std::map<AccountIdentity, std::int64_t> totals;
        for (const TrophyLedgerEntry& entry : entries_)
            totals[entry.account] += entry.delta;
        entries.clear();
        for (const auto& [account, total] : totals)
            entries.push_back({account, total});
        std::ranges::sort(entries, [](const auto& left, const auto& right) {
            if (left.total != right.total) return left.total > right.total;
            return left.account < right.account;
        });
        error.clear();
        return true;
    }

private:
    std::set<TrophyMatchId> scoredMatches_;
    std::vector<TrophyLedgerEntry> entries_;
};

} // namespace

std::shared_ptr<TrophyPersistence> makeInMemoryTrophyPersistence() {
    return std::make_shared<InMemoryTrophyPersistence>();
}

TrophyLedger::TrophyLedger()
    : TrophyLedger(makeInMemoryTrophyPersistence()) {}

TrophyLedger::TrophyLedger(std::shared_ptr<TrophyPersistence> persistence)
    : persistence_(std::move(persistence)) {}

TrophyScoreResult TrophyLedger::scoreMatch(
    const TrophyMatchId& match,
    const std::map<PlayerId, AccountIdentity>& accounts,
    const MatchResult& result,
    std::span<const GameEvent> authoritativeEvents,
    std::string* scoringError) {

    if (scoringError != nullptr) scoringError->clear();
    if (result.status != MatchStatus::Completed)
        return TrophyScoreResult::NotTerminal;
    std::vector<TrophyLedgerEntry> entries;

    // A draw or another terminal result without one winner awards nothing,
    // but its match ID is still persisted to make scoring idempotent.
    if (result.winner.has_value()) {
        const auto winner = accounts.find(*result.winner);
        if (winner != accounts.end()) {
            entries.push_back({match, winner->second, TrophyReason::Win, 2});
            for (const auto& [player, account] : accounts) {
                if (player != *result.winner)
                    entries.push_back({match, account, TrophyReason::Loss, -1});
            }

            std::set<std::pair<PlayerId, PlayerId>> creditedKills;
            for (const GameEvent& event : authoritativeEvents) {
                if (event.type != GameEventType::PlayerKilled ||
                    !event.actor.has_value() ||
                    !event.targetPlayer.has_value() ||
                    event.actor == event.targetPlayer ||
                    !creditedKills.emplace(
                        *event.actor, *event.targetPlayer).second) {
                    continue;
                }
                const auto killer = accounts.find(*event.actor);
                if (killer != accounts.end()) {
                    entries.push_back(
                        {match, killer->second, TrophyReason::PlayerKill, 1});
                }
            }

            if (result.outcome == MatchOutcome::EscapedWithSigil) {
                entries.push_back(
                    {match, winner->second, TrophyReason::Extraction, 1});
            }
        }
    }

    std::string error;
    switch (persistence_->appendMatch(match, entries, error)) {
    case TrophyAppendResult::Appended:
        return TrophyScoreResult::Scored;
    case TrophyAppendResult::DuplicateMatch:
        return TrophyScoreResult::AlreadyScored;
    case TrophyAppendResult::Error:
        if (scoringError != nullptr) *scoringError = std::move(error);
        return TrophyScoreResult::PersistenceError;
    }
    return TrophyScoreResult::PersistenceError;
}

bool TrophyLedger::loadEntries(
    std::vector<TrophyLedgerEntry>& entries,
    std::string& error) const {

    return persistence_->loadEntries(entries, error);
}

bool TrophyLedger::trophyTotal(
    const AccountIdentity& account,
    std::int64_t& total,
    std::string& error) const {

    return persistence_->trophyTotal(account, total, error);
}

bool TrophyLedger::leaderboard(
    std::vector<TrophyLeaderboardEntry>& entries,
    std::string& error) const {

    return persistence_->leaderboard(entries, error);
}

} // namespace basilisk::game::server
