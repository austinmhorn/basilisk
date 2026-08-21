#include "TrophyScoring.hpp"

#include <utility>

namespace basilisk::game::server {

TrophyScoreResult TrophyLedger::scoreMatch(
    const TrophyMatchId& match,
    const std::map<PlayerId, AccountIdentity>& accounts,
    const MatchResult& result,
    std::span<const GameEvent> authoritativeEvents) {

    if (result.status != MatchStatus::Completed)
        return TrophyScoreResult::NotTerminal;
    if (scoredMatches_.contains(match))
        return TrophyScoreResult::AlreadyScored;

    scoredMatches_.insert(match);

    // A draw or another terminal result without one winner awards nothing.
    if (!result.winner.has_value()) return TrophyScoreResult::Scored;

    const auto winner = accounts.find(*result.winner);
    if (winner == accounts.end()) return TrophyScoreResult::Scored;

    entries_.push_back({match, winner->second, TrophyReason::Win, 2});
    for (const auto& [player, account] : accounts) {
        if (player != *result.winner)
            entries_.push_back({match, account, TrophyReason::Loss, -1});
    }

    std::set<std::pair<PlayerId, PlayerId>> creditedKills;
    for (const GameEvent& event : authoritativeEvents) {
        if (event.type != GameEventType::PlayerKilled ||
            !event.actor.has_value() || !event.targetPlayer.has_value() ||
            event.actor == event.targetPlayer ||
            !creditedKills.emplace(*event.actor, *event.targetPlayer).second) {
            continue;
        }
        const auto killer = accounts.find(*event.actor);
        if (killer != accounts.end()) {
            entries_.push_back(
                {match, killer->second, TrophyReason::PlayerKill, 1});
        }
    }

    if (result.outcome == MatchOutcome::EscapedWithSigil) {
        entries_.push_back(
            {match, winner->second, TrophyReason::Extraction, 1});
    }
    return TrophyScoreResult::Scored;
}

} // namespace basilisk::game::server
