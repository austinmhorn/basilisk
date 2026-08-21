#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <map>
#include <numeric>
#include <queue>
#include <string>
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
    auto ledger = std::make_shared<TrophyLedger>(open(database));
    constexpr MapSeed mapSeed{20260816};
    constexpr MatchSeed matchSeed{424242};
    std::string error;
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
                network::SubmitActionCommand{action}}));
            assert(client->endpoint->send(network::ClientCommand{
                network::kProtocolVersion,
                network::LockActionCommand{player->id}}));
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
    const DirectClient& winnerClient = *mirror.result.winner == PlayerId{1}
        ? p1
        : p2;
    const DirectClient& loserClient = *mirror.result.winner == PlayerId{1}
        ? p2
        : p1;
    assert(winnerClient.trophyTotal == total(*open(database), winner.value));
    assert(loserClient.trophyTotal == -1);

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
    unfinishedAuthoritativeMatchDoesNotClaimItsId();
    authoritativeZeroEntryDrawIsClaimedOnce();
    persistenceFailureDoesNotCorruptTerminalGameplayState();
    realAuthoritativeWinnerPersistsAwardsForDurableAccounts();
}
