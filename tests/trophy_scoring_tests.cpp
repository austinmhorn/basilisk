#include <algorithm>
#include <cassert>
#include <map>
#include <numeric>
#include <span>
#include <string>
#include <vector>

#include "TrophyScoring.hpp"
#include "AuthoritativeInMemoryMatch.hpp"

using namespace basilisk;
using namespace basilisk::game;
using namespace basilisk::game::server;

namespace {

const std::map<PlayerId, AccountIdentity> accounts{
    {PlayerId{1}, AccountIdentity{"account-mara"}},
    {PlayerId{2}, AccountIdentity{"account-elias"}},
};

MatchResult completed(MatchOutcome outcome, std::optional<PlayerId> winner) {
    return {MatchStatus::Completed, outcome, winner};
}

int totalFor(
    const TrophyLedger& ledger,
    const AccountIdentity& account) {

    return std::accumulate(
        ledger.entries().begin(), ledger.entries().end(), 0,
        [&](int total, const TrophyLedgerEntry& entry) {
            return total + (entry.account == account ? entry.delta : 0);
        });
}

bool hasEntry(
    const TrophyLedger& ledger,
    const AccountIdentity& account,
    TrophyReason reason,
    int delta) {

    return std::ranges::any_of(
        ledger.entries(),
        [&](const TrophyLedgerEntry& entry) {
            return entry.account == account && entry.reason == reason &&
                   entry.delta == delta;
        });
}

GameEvent playerKill(PlayerId killer, PlayerId victim) {
    return GameEvent{
        GameEventType::PlayerKilled,
        killer,
        victim,
    };
}

void winAndLossScoreOnce() {
    TrophyLedger ledger;
    const auto result = completed(MatchOutcome::BasiliskKilled, PlayerId{1});
    assert(ledger.scoreMatch({"match-win"}, accounts, result, {}) ==
           TrophyScoreResult::Scored);
    assert(totalFor(ledger, accounts.at(PlayerId{1})) == 2);
    assert(totalFor(ledger, accounts.at(PlayerId{2})) == -1);
    assert(hasEntry(
        ledger, accounts.at(PlayerId{1}), TrophyReason::Win, 2));
    assert(hasEntry(
        ledger, accounts.at(PlayerId{2}), TrophyReason::Loss, -1));
}

void killAndWinStack() {
    TrophyLedger ledger;
    const std::vector events{playerKill(PlayerId{1}, PlayerId{2})};
    assert(ledger.scoreMatch(
        {"match-kill-win"}, accounts,
        completed(MatchOutcome::BasiliskKilled, PlayerId{1}), events) ==
        TrophyScoreResult::Scored);
    assert(totalFor(ledger, accounts.at(PlayerId{1})) == 3);
}

void extractionScoresThreeTotal() {
    TrophyLedger ledger;
    assert(ledger.scoreMatch(
        {"match-extraction"}, accounts,
        completed(MatchOutcome::EscapedWithSigil, PlayerId{1}), {}) ==
        TrophyScoreResult::Scored);
    assert(totalFor(ledger, accounts.at(PlayerId{1})) == 3);
    assert(hasEntry(
        ledger, accounts.at(PlayerId{1}), TrophyReason::Extraction, 1));
}

void killAndExtractionStack() {
    TrophyLedger ledger;
    const std::vector events{playerKill(PlayerId{2}, PlayerId{1})};
    assert(ledger.scoreMatch(
        {"match-kill-extraction"}, accounts,
        completed(MatchOutcome::EscapedWithSigil, PlayerId{2}), events) ==
        TrophyScoreResult::Scored);
    assert(totalFor(ledger, accounts.at(PlayerId{2})) == 4);
}

void drawAndUnfinishedScoreNothing() {
    TrophyLedger ledger;
    const std::vector events{playerKill(PlayerId{1}, PlayerId{2})};
    assert(ledger.scoreMatch(
        {"match-active"}, accounts, MatchResult{}, events) ==
        TrophyScoreResult::NotTerminal);
    assert(ledger.entries().empty());
    assert(ledger.scoreMatch(
        {"match-draw"}, accounts,
        completed(MatchOutcome::Draw, std::nullopt), events) ==
        TrophyScoreResult::Scored);
    assert(ledger.entries().empty());
}

void duplicateScoringIsRejectedWithoutDuplicateEntries() {
    TrophyLedger ledger;
    const auto result = completed(MatchOutcome::BasiliskKilled, PlayerId{1});
    assert(ledger.scoreMatch({"match-once"}, accounts, result, {}) ==
           TrophyScoreResult::Scored);
    const std::vector<TrophyLedgerEntry> original(
        ledger.entries().begin(), ledger.entries().end());
    assert(ledger.scoreMatch({"match-once"}, accounts, result, {}) ==
           TrophyScoreResult::AlreadyScored);
    assert(std::ranges::equal(ledger.entries(), original));
}

std::vector<client::PublicPlayerProfile> profiles() {
    return {
        {PlayerId{1}, "Mara", client::CallingCardId{"card-one"},
         client::EmblemId{"emblem-one"}},
        {PlayerId{2}, "Elias", client::CallingCardId{"card-two"},
         client::EmblemId{"emblem-two"}},
    };
}

void authoritativeMatchScoresItsTerminalEventStream() {
    auto ledger = std::make_shared<TrophyLedger>();
    TrophyScoringContext scoring{
        TrophyMatchId{"authoritative-draw"}, accounts, ledger};
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error,
        std::move(scoring));
    assert(host != nullptr && error.empty());
    auto p1 = host->connect(PlayerId{1}, error);
    auto p2 = host->connect(PlayerId{2}, error);
    assert(p1 != nullptr && p2 != nullptr);
    assert(p1->send(network::ClientCommand{
        network::kProtocolVersion,
        network::QuitCommand{PlayerId{1}}}));
    assert(p2->send(network::ClientCommand{
        network::kProtocolVersion,
        network::QuitCommand{PlayerId{2}}}));
    host->advanceTime(30'000);
    assert(ledger->entries().empty());
    assert(ledger->scoreMatch(
        {"authoritative-draw"}, accounts,
        completed(MatchOutcome::Draw, std::nullopt), {}) ==
        TrophyScoreResult::AlreadyScored);
}

} // namespace

int main() {
    winAndLossScoreOnce();
    killAndWinStack();
    extractionScoresThreeTotal();
    killAndExtractionStack();
    drawAndUnfinishedScoreNothing();
    duplicateScoringIsRejectedWithoutDuplicateEntries();
    authoritativeMatchScoresItsTerminalEventStream();
}
