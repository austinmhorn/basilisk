#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "AuthoritativeInMemoryMatch.hpp"
#include "NetworkGameSessionAdapter.hpp"
#include "NetworkWireCodec.hpp"
#include "PublicAccountProfiles.hpp"

using namespace basilisk;
using namespace basilisk::game;
using namespace basilisk::game::network;
using namespace basilisk::game::server;

namespace {

std::vector<client::PublicPlayerProfile> profiles() {
    return {
        client::PublicPlayerProfile{
            PlayerId{1},
            "Mara Voss",
            client::CallingCardId{"ember-field"},
            client::EmblemId{"wayfinder"}},
        client::PublicPlayerProfile{
            PlayerId{2},
            "Elias Thorn",
            client::CallingCardId{"blue-ward"},
            client::EmblemId{"ward"}},
    };
}

struct ConnectedClient {
    std::shared_ptr<InMemoryMatchEndpoint> endpoint;
    std::unique_ptr<NetworkGameSessionAdapter> session;

    void ingestUpdates() {
        while (auto frame = endpoint->takeNextServerFrame()) {
            ServerUpdate update;
            std::string error;
            assert(decodeServerUpdate(*frame, update, error));
            assert(session->ingest(std::move(update), error));
        }
    }
};

ConnectedClient connectClient(
    AuthoritativeInMemoryMatch& host,
    PlayerId player) {

    std::string error;
    auto endpoint = host.connect(player, error);
    assert(endpoint != nullptr);
    assert(error.empty());
    const auto frame = endpoint->takeNextServerFrame();
    assert(frame.has_value());
    ServerBootstrap bootstrap;
    assert(decodeServerBootstrap(*frame, bootstrap, error));
    assert(bootstrap.viewContext.localPlayer == player);
    assert(bootstrap.initialSnapshot.player == player);
    auto session = NetworkGameSessionAdapter::create(
        std::move(bootstrap), endpoint, error);
    assert(session != nullptr);
    assert(error.empty());
    return {std::move(endpoint), std::move(session)};
}

ConnectedClient reconnectClient(
    AuthoritativeInMemoryMatch& host,
    PlayerId player) {

    std::string error;
    auto endpoint = host.reconnect(player, error);
    assert(endpoint != nullptr && error.empty());
    const auto frame = endpoint->takeNextServerFrame();
    assert(frame.has_value());
    ServerBootstrap bootstrap;
    assert(decodeServerBootstrap(*frame, bootstrap, error));
    assert(bootstrap.viewContext.localPlayer == player);
    auto session = NetworkGameSessionAdapter::create(
        std::move(bootstrap), endpoint, error);
    assert(session != nullptr && error.empty());
    return {std::move(endpoint), std::move(session)};
}

const AvailableAction& actionOfType(
    const PlayerRoundSnapshot& snapshot,
    ActionType type) {

    const auto found = std::find_if(
        snapshot.availableActions.begin(),
        snapshot.availableActions.end(),
        [type](const AvailableAction& action) { return action.type == type; });
    assert(found != snapshot.availableActions.end());
    return *found;
}

bool containsCave(const PlayerMapView& map, CaveId cave) {
    return std::any_of(map.caves.begin(), map.caves.end(),
        [cave](const DiscoveredCaveView& candidate) {
            return candidate.cave == cave;
        });
}

void twoClientsAdvanceOneAuthoritativeRound() {
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error);
    assert(host != nullptr);
    ConnectedClient p1 = connectClient(*host, PlayerId{1});
    ConnectedClient p2 = connectClient(*host, PlayerId{2});

    const PlayerRoundSnapshot* first =
        p1.session->controller().displayedSnapshot();
    const PlayerRoundSnapshot* second =
        p2.session->controller().displayedSnapshot();
    assert(first != nullptr && second != nullptr);
    assert(first->round == RoundNumber{1});
    assert(second->round == RoundNumber{1});
    assert(p1.session->controller().matchMode() == client::MatchMode::Online);
    assert(p2.session->controller().matchMode() == client::MatchMode::Online);
    assert(first->player == PlayerId{1});
    assert(second->player == PlayerId{2});
    const auto& p1Players = p1.session->controller().matchMetadata().players;
    const auto& p2Players = p2.session->controller().matchMetadata().players;
    assert(p1Players.size() == 2 && p2Players.size() == 2);
    for (std::size_t index = 0; index < p1Players.size(); ++index) {
        assert(p1Players[index].player == p2Players[index].player);
        assert(p1Players[index].slot == p2Players[index].slot);
    }

    // Each bootstrap is independently projected. The rival's starting CaveId
    // is not promoted into the other player's discovered geometry or map.
    assert(first->currentCave != second->currentCave);
    assert(!containsCave(first->map, second->currentCave));
    assert(!containsCave(second->map, first->currentCave));
    assert(!p1.session->controller().displayedMapGeometry()
                ->discoveredCaves.contains(second->currentCave));
    assert(!p2.session->controller().displayedMapGeometry()
                ->discoveredCaves.contains(first->currentCave));

    const AvailableAction p1Search = actionOfType(*first, ActionType::Search);
    const AvailableAction p2Search = actionOfType(*second, ActionType::Search);
    assert(p1.session->controller().submitAndLock(p1Search));
    assert(host->authoritativeRound() == RoundNumber{1});
    assert(host->resolvedRoundCount() == 0);
    assert(!p1.endpoint->takeNextServerFrame().has_value());

    assert(p2.session->controller().submitAndLock(p2Search));
    assert(host->authoritativeRound() == RoundNumber{2});
    assert(host->resolvedRoundCount() == 1);
    p1.ingestUpdates();
    p2.ingestUpdates();

    first = p1.session->controller().displayedSnapshot();
    second = p2.session->controller().displayedSnapshot();
    assert(first != nullptr && second != nullptr);
    assert(first->round == RoundNumber{2});
    assert(second->round == RoundNumber{2});
    assert(first->player == PlayerId{1});
    assert(second->player == PlayerId{2});
    assert(host->authoritativeRound() == RoundNumber{2});
    assert(host->resolvedRoundCount() == 1);
}

void noOpTimeTickPreservesResolvedRoundObservations() {
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{1}, MatchSeed{424242}, profiles(), error);
    assert(host != nullptr);
    ConnectedClient p1 = connectClient(*host, PlayerId{1});
    ConnectedClient p2 = connectClient(*host, PlayerId{2});

    bool foundJackalDamage = false;
    for (int turn = 0; turn < 100 && !foundJackalDamage; ++turn) {
        for (ConnectedClient* client : {&p1, &p2}) {
            const PlayerRoundSnapshot* snapshot =
                client->session->controller().displayedSnapshot();
            if (snapshot->alive && !snapshot->availableActions.empty())
                assert(client->session->controller().submitAndLock(
                    snapshot->availableActions.front()));
        }
        p1.ingestUpdates();
        p2.ingestUpdates();
        for (ConnectedClient* client : {&p1, &p2}) {
            const auto& observations =
                client->session->controller().displayedSnapshot()->observations;
            const bool knockout = std::any_of(observations.begin(), observations.end(),
                [](const PlayerObservation& observation) {
                    return observation.type == ObservationType::JackalKnockedOutYou;
                });
            const bool damage = std::any_of(observations.begin(), observations.end(),
                [](const PlayerObservation& observation) {
                    return observation.type == ObservationType::YouWereDamaged;
                });
            if (!knockout || !damage) continue;
            host->advanceTime(100);
            client->ingestUpdates();
            const auto& afterTick =
                client->session->controller().displayedSnapshot()->observations;
            assert(std::any_of(afterTick.begin(), afterTick.end(),
                [](const PlayerObservation& observation) {
                    return observation.type == ObservationType::JackalKnockedOutYou;
                }));
            assert(std::any_of(afterTick.begin(), afterTick.end(),
                [](const PlayerObservation& observation) {
                    return observation.type == ObservationType::YouWereDamaged;
                }));
            foundJackalDamage = true;
            break;
        }
    }
    assert(foundJackalDamage);
}

void spoofedMalformedAndForgedCommandsAreRejected() {
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error);
    assert(host != nullptr);
    ConnectedClient p1 = connectClient(*host, PlayerId{1});

    PlayerAction spoofed;
    spoofed.player = PlayerId{2};
    spoofed.type = ActionType::Search;
    ClientCommand command{kProtocolVersion,
        SubmitActionCommand{RoundNumber{1}, spoofed}};
    WireBytes bytes;
    assert(encodeWire(command, bytes, error));
    assert(!p1.endpoint->sendBytes(bytes, error));
    assert(!error.empty());

    command.payload = LockActionCommand{RoundNumber{1}, PlayerId{2}};
    assert(encodeWire(command, bytes, error));
    assert(!p1.endpoint->sendBytes(bytes, error));

    PlayerAction forged;
    forged.player = PlayerId{1};
    forged.type = ActionType::Move;
    forged.targetCave = CaveId{9999};
    command.payload = SubmitActionCommand{RoundNumber{1}, forged};
    assert(encodeWire(command, bytes, error));
    assert(!p1.endpoint->sendBytes(bytes, error));

    bytes = {0x42, 0x53, 0x4b};
    assert(!p1.endpoint->sendBytes(bytes, error));

    command = ClientCommand{kProtocolVersion, QuitCommand{PlayerId{1}}};
    assert(encodeWire(command, bytes, error));
    bytes[8] = 0xff;
    assert(!p1.endpoint->sendBytes(bytes, error));

    assert(host->authoritativeRound() == RoundNumber{1});
    assert(host->resolvedRoundCount() == 0);
    assert(!p1.endpoint->takeNextServerFrame().has_value());
}

void actionCommandsAreBoundToTheAuthoritativeRound() {
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error);
    assert(host != nullptr);
    ConnectedClient p1 = connectClient(*host, PlayerId{1});
    ConnectedClient p2 = connectClient(*host, PlayerId{2});
    const PlayerRoundSnapshot before =
        *p1.session->controller().displayedSnapshot();
    const PlayerAction p1Search = makePlayerAction(
        actionOfType(before, ActionType::Search), PlayerId{1});

    const auto send = [&](ConnectedClient& client, ClientCommandPayload payload) {
        WireBytes bytes;
        std::string commandError;
        assert(encodeWire(ClientCommand{kProtocolVersion, std::move(payload)},
            bytes, commandError));
        return client.endpoint->sendBytes(bytes, commandError);
    };

    assert(!send(p1, SubmitActionCommand{RoundNumber{0}, p1Search}));
    assert(!send(p1, SubmitActionCommand{RoundNumber{2}, p1Search}));
    assert(!send(p1, LockActionCommand{RoundNumber{0}, PlayerId{1}}));
    assert(!send(p1, LockActionCommand{RoundNumber{2}, PlayerId{1}}));
    assert(host->authoritativeRound() == RoundNumber{1});
    assert(host->resolvedRoundCount() == 0);

    // No rejected command staged or locked an action: a current-round lock
    // without a current-round submit still fails.
    assert(!send(p1, LockActionCommand{RoundNumber{1}, PlayerId{1}}));
    assert(send(p1, SubmitActionCommand{RoundNumber{1}, p1Search}));
    assert(send(p1, LockActionCommand{RoundNumber{1}, PlayerId{1}}));
    assert(host->authoritativeRound() == RoundNumber{1});

    const PlayerRoundSnapshot p2Before =
        *p2.session->controller().displayedSnapshot();
    const PlayerAction p2Search = makePlayerAction(
        actionOfType(p2Before, ActionType::Search), PlayerId{2});
    assert(send(p2, SubmitActionCommand{RoundNumber{1}, p2Search}));
    assert(send(p2, LockActionCommand{RoundNumber{1}, PlayerId{2}}));
    assert(host->authoritativeRound() == RoundNumber{2});
    assert(host->resolvedRoundCount() == 1);
    p1.ingestUpdates();
    p2.ingestUpdates();
    const PlayerRoundSnapshot after =
        *p1.session->controller().displayedSnapshot();

    // A delayed round-one action whose shape remains legal cannot stage an
    // action in round two, nor can a future round command do so.
    assert(!send(p1, SubmitActionCommand{RoundNumber{1}, p1Search}));
    assert(!send(p1, LockActionCommand{RoundNumber{1}, PlayerId{1}}));
    assert(!send(p1, SubmitActionCommand{RoundNumber{3}, p1Search}));
    assert(!send(p1, LockActionCommand{RoundNumber{3}, PlayerId{1}}));
    assert(host->authoritativeRound() == RoundNumber{2});
    assert(host->resolvedRoundCount() == 1);
    assert(p1.session->controller().displayedSnapshot()->round == after.round);
    assert(p1.session->controller().displayedSnapshot()->currentCave ==
        after.currentCave);
    assert(p1.session->controller().displayedSnapshot()->health == after.health);
    assert(!send(p1, LockActionCommand{RoundNumber{2}, PlayerId{1}}));

    // Player identity remains an independent guard when the round is current.
    PlayerAction spoofed = p1Search;
    spoofed.player = PlayerId{2};
    assert(!send(p1, SubmitActionCommand{RoundNumber{2}, spoofed}));
}

void explicitQuitEliminatesImmediatelyAndSurvivorContinues() {
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error);
    assert(host != nullptr);
    ConnectedClient p1 = connectClient(*host, PlayerId{1});
    ConnectedClient p2 = connectClient(*host, PlayerId{2});

    assert(p2.session->controller().quit());
    p1.ingestUpdates();
    p2.ingestUpdates();
    assert(!p2.session->controller().displayedSnapshot()->alive);
    assert(p2.session->controller().viewContext().mode ==
           client::ClientViewMode::Defeated);
    assert(p2.session->controller().viewContext().spectatablePlayer ==
           PlayerId{1});

    assert(p2.session->controller().watchRemainingHunter());
    p2.ingestUpdates();
    assert(p2.session->controller().viewContext().mode ==
           client::ClientViewMode::Spectating);
    assert(p2.session->controller().viewContext().localPlayer == PlayerId{2});
    assert(p2.session->controller().viewContext().viewedPlayer == PlayerId{1});
    assert(p2.session->controller().displayedSnapshot()->player == PlayerId{1});
    assert(!p2.session->controller().canSubmitActions());

    const RoundNumber priorRound =
        p1.session->controller().displayedSnapshot()->round;
    const AvailableAction survivorSearch = actionOfType(
        *p1.session->controller().displayedSnapshot(), ActionType::Search);
    assert(p1.session->controller().submitAndLock(survivorSearch));
    assert(host->authoritativeRound() == priorRound + 1);
    p1.ingestUpdates();
    p2.ingestUpdates();
    assert(p1.session->controller().displayedSnapshot()->round ==
           priorRound + 1);
    assert(p2.session->controller().displayedSnapshot()->round ==
           priorRound + 1);
}

void reconnectPreservesRoundStateAndRejectsStaleEndpoints() {
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error);
    assert(host != nullptr);
    ConnectedClient p1 = connectClient(*host, PlayerId{1});
    ConnectedClient p2 = connectClient(*host, PlayerId{2});

    const auto send = [](ConnectedClient& client, ClientCommandPayload payload) {
        return client.endpoint->send(
            ClientCommand{kProtocolVersion, std::move(payload)});
    };
    const PlayerAction p1Search = makePlayerAction(
        actionOfType(*p1.session->controller().displayedSnapshot(),
            ActionType::Search), PlayerId{1});
    assert(send(p1, SubmitActionCommand{RoundNumber{1}, p1Search}));
    auto staleEndpoint = p1.endpoint;
    p1.endpoint->disconnect();
    ConnectedClient reclaimed = reconnectClient(*host, PlayerId{1});
    assert(reclaimed.session->controller().viewContext().localPlayer == PlayerId{1});
    assert(reclaimed.session->controller().displayedSnapshot()->round ==
        RoundNumber{1});
    assert(!staleEndpoint->send(ClientCommand{kProtocolVersion,
        LockActionCommand{RoundNumber{1}, PlayerId{1}}}));
    staleEndpoint->disconnect();
    assert(send(reclaimed,
        LockActionCommand{RoundNumber{1}, PlayerId{1}}));

    const PlayerAction p2Search = makePlayerAction(
        actionOfType(*p2.session->controller().displayedSnapshot(),
            ActionType::Search), PlayerId{2});
    assert(send(p2, SubmitActionCommand{RoundNumber{1}, p2Search}));
    assert(send(p2, LockActionCommand{RoundNumber{1}, PlayerId{2}}));
    assert(host->authoritativeRound() == RoundNumber{2});

    // A locked player may disconnect while the other hunter completes the
    // round; the reclaimed bootstrap starts at the new authoritative round.
    reclaimed.ingestUpdates();
    p2.ingestUpdates();
    const PlayerAction nextP1Search = makePlayerAction(
        actionOfType(*reclaimed.session->controller().displayedSnapshot(),
            ActionType::Search), PlayerId{1});
    assert(send(reclaimed,
        SubmitActionCommand{RoundNumber{2}, nextP1Search}));
    assert(send(reclaimed,
        LockActionCommand{RoundNumber{2}, PlayerId{1}}));
    reclaimed.endpoint->disconnect();
    const PlayerAction nextP2Search = makePlayerAction(
        actionOfType(*p2.session->controller().displayedSnapshot(),
            ActionType::Search), PlayerId{2});
    assert(send(p2, SubmitActionCommand{RoundNumber{2}, nextP2Search}));
    assert(send(p2, LockActionCommand{RoundNumber{2}, PlayerId{2}}));
    assert(host->authoritativeRound() == RoundNumber{3});
    ConnectedClient advanced = reconnectClient(*host, PlayerId{1});
    assert(advanced.session->controller().displayedSnapshot()->round ==
        RoundNumber{3});

    // Explicit Quit remains a forfeit and is not converted into reconnect
    // grace by the endpoint lifecycle.
    assert(advanced.session->controller().quit());
    advanced.ingestUpdates();
    assert(!advanced.session->controller().displayedSnapshot()->alive);
    advanced.endpoint->disconnect();
    assert(host->reconnect(PlayerId{1}, error) == nullptr);
}

void authenticatedServerReturnsOnlyPublicLeaderboardFields() {
    const auto persistence = makeInMemoryTrophyPersistence();
    const auto ledger = std::make_shared<TrophyLedger>(persistence);
    std::string error;
    const TrophyLedgerEntry first{
        TrophyMatchId{"leaderboard-match"},
        AccountIdentity{"private-account-one"},
        TrophyReason::Win,
        9,
    };
    const TrophyLedgerEntry second{
        TrophyMatchId{"leaderboard-match"},
        AccountIdentity{"private-account-two"},
        TrophyReason::Loss,
        3,
    };
    const std::vector<TrophyLedgerEntry> entries{first, second};
    assert(persistence->appendMatch(
        TrophyMatchId{"leaderboard-match"}, entries, error) ==
        TrophyAppendResult::Appended);

    const auto profileStore = makeInMemoryPublicAccountProfileStore();
    assert(profileStore->storeProfile(
        first.account,
        PublicAccountProfile{Username{"mara"}},
        error) == PublicProfileStoreResult::Stored);
    assert(profileStore->storeProfile(
        second.account,
        PublicAccountProfile{Username{"elias"}},
        error) == PublicProfileStoreResult::Stored);
    auto readModel = std::make_shared<PublicTrophyReadModel>(
        ledger, profileStore);
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error,
        std::nullopt, readModel);
    assert(host != nullptr);
    auto endpoint = host->connect(PlayerId{1}, error);
    assert(endpoint != nullptr);
    assert(endpoint->takeNextServerFrame().has_value()); // Bootstrap.

    assert(endpoint->send(ClientCommand{
        kProtocolVersion,
        LeaderboardPageRequest{0, 10},
    }));
    const auto frame = endpoint->takeNextServerFrame();
    assert(frame.has_value());
    LeaderboardPageResponse response;
    assert(decodeLeaderboardPageResponse(*frame, response, error));
    const std::vector<PublicTrophyLeaderboardEntry> expected{
        {1, Username{"mara"}, 9},
        {2, Username{"elias"}, 3},
    };
    assert(response.offset == 0);
    assert(response.entries == expected);
    for (const auto& entry : response.entries) {
        assert(entry.username.value.find("private-account") == std::string::npos);
    }
}

void onlineSandboxTerminalStateReachesEveryHuman() {
    auto config = client::defaultSandboxSessionConfig(3);
    config.humanPlayerCount = 3;
    config.caveCount = 30;
    config.jackalCount = 0;
    config.mapSeed = MapSeed{3030};

    std::vector<client::PublicPlayerProfile> sandboxProfiles;
    for (std::uint32_t slot = 1; slot <= 3; ++slot) {
        sandboxProfiles.push_back({
            PlayerId{slot},
            "Hunter " + std::to_string(slot),
            {"arrow-right-black"},
            {"circle-black"},
        });
    }

    std::string error;
    auto host = AuthoritativeInMemoryMatch::createSandbox(
        config, sandboxProfiles, {}, error);
    assert(host != nullptr && error.empty());
    ConnectedClient p1 = connectClient(*host, PlayerId{1});
    ConnectedClient p2 = connectClient(*host, PlayerId{2});
    ConnectedClient p3 = connectClient(*host, PlayerId{3});

    assert(p1.session->controller().quit());
    p1.ingestUpdates();
    p2.ingestUpdates();
    p3.ingestUpdates();
    assert(p1.session->controller().displayedSnapshot()->matchStatus ==
           MatchStatus::Active);

    assert(p2.session->controller().quit());
    p1.ingestUpdates();
    p2.ingestUpdates();
    p3.ingestUpdates();
    assert(p3.session->controller().displayedSnapshot()->matchStatus ==
           MatchStatus::Active);

    assert(p3.session->controller().quit());
    p1.ingestUpdates();
    p2.ingestUpdates();
    p3.ingestUpdates();

    for (const ConnectedClient* client : {&p1, &p2, &p3}) {
        const auto* snapshot = client->session->controller().displayedSnapshot();
        assert(snapshot != nullptr);
        assert(snapshot->matchStatus == MatchStatus::Completed);
        assert(snapshot->matchOutcome == MatchOutcome::Draw);
        assert(!snapshot->winner.has_value());
    }
}

void slowOnlineSandboxHumanExpiresWithoutBlockingRound() {
    auto config = client::defaultSandboxSessionConfig(3);
    config.humanPlayerCount = 3;
    config.caveCount = 30;
    config.jackalCount = 0;
    config.mapSeed = MapSeed{3303};

    std::vector<client::PublicPlayerProfile> sandboxProfiles;
    for (std::uint32_t slot = 1; slot <= 3; ++slot) {
        sandboxProfiles.push_back({
            PlayerId{slot},
            "Hunter " + std::to_string(slot),
            {"arrow-right-black"},
            {"circle-black"},
        });
    }

    std::string error;
    auto host = AuthoritativeInMemoryMatch::createSandbox(
        config, sandboxProfiles, {}, error);
    assert(host != nullptr && error.empty());
    ConnectedClient p1 = connectClient(*host, PlayerId{1});
    ConnectedClient p2 = connectClient(*host, PlayerId{2});
    ConnectedClient p3 = connectClient(*host, PlayerId{3});

    const RoundNumber round = host->authoritativeRound();
    assert(p1.session->controller().submitAndLock(
        actionOfType(*p1.session->controller().displayedSnapshot(),
            ActionType::Search)));
    assert(p2.session->controller().submitAndLock(
        actionOfType(*p2.session->controller().displayedSnapshot(),
            ActionType::Search)));

    host->advanceTime(300000);
    p1.ingestUpdates();
    p2.ingestUpdates();
    p3.ingestUpdates();

    assert(host->authoritativeRound() == round + 1);
    assert(host->resolvedRoundCount() == 1);
    assert(p1.session->controller().displayedSnapshot()->round == round + 1);
    assert(p2.session->controller().displayedSnapshot()->round == round + 1);
    assert(p3.session->controller().viewContext().mode ==
           client::ClientViewMode::Defeated);
    assert(!p3.session->controller().displayedSnapshot()->alive);
}

void onlineSandboxSpectatorAdvancesWhenWatchedHunterDies() {
    auto config = client::defaultSandboxSessionConfig(3);
    config.humanPlayerCount = 3;
    config.caveCount = 30;
    config.jackalCount = 0;
    config.mapSeed = MapSeed{3003};

    std::vector<client::PublicPlayerProfile> sandboxProfiles;
    for (std::uint32_t slot = 1; slot <= 3; ++slot) {
        sandboxProfiles.push_back({
            PlayerId{slot},
            "Hunter " + std::to_string(slot),
            {"arrow-right-black"},
            {"circle-black"},
        });
    }

    std::string error;
    auto host = AuthoritativeInMemoryMatch::createSandbox(
        config, sandboxProfiles, {}, error);
    assert(host != nullptr && error.empty());

    ConnectedClient p1 = connectClient(*host, PlayerId{1});
    ConnectedClient p2 = connectClient(*host, PlayerId{2});
    ConnectedClient p3 = connectClient(*host, PlayerId{3});

    assert(p1.session->controller().quit());
    p1.ingestUpdates();
    p2.ingestUpdates();
    p3.ingestUpdates();
    assert(p1.session->controller().viewContext().mode ==
           client::ClientViewMode::Defeated);
    assert(p1.session->controller().viewContext().spectatablePlayer ==
           PlayerId{2});

    assert(p1.session->controller().watchRemainingHunter());
    p1.ingestUpdates();
    assert(p1.session->controller().viewContext().mode ==
           client::ClientViewMode::Spectating);
    assert(p1.session->controller().viewContext().viewedPlayer ==
           PlayerId{2});

    assert(p2.session->controller().quit());
    p1.ingestUpdates();
    p2.ingestUpdates();
    p3.ingestUpdates();
    assert(p1.session->controller().viewContext().mode ==
           client::ClientViewMode::Spectating);
    assert(p1.session->controller().viewContext().viewedPlayer ==
           PlayerId{3});
    assert(p1.session->controller().displayedSnapshot()->player ==
           PlayerId{3});
}

void sandboxLaunchPreservesSlotsAndRunsServerAi() {
    auto config = client::defaultSandboxSessionConfig(6);
    config.humanPlayerCount = 3;
    config.mapSeed = MapSeed{606060};
    std::vector<client::PublicPlayerProfile> sandboxProfiles;
    for (std::uint32_t slot = 1; slot <= 6; ++slot)
        sandboxProfiles.push_back({PlayerId{slot}, "Hunter " + std::to_string(slot),
            {"arrow-right-black"}, {"circle-black"}});
    std::vector<client::ai::AiConfig> aiPlayers;
    for (std::uint32_t slot = 4; slot <= 6; ++slot)
        aiPlayers.push_back({client::ai::AiDifficulty::Medium,
            client::ai::AiBehavior::Balanced, PlayerId{slot},
            client::ai::AiSeed{1000 + slot}});
    std::string error;
    auto host = AuthoritativeInMemoryMatch::createSandbox(
        config, sandboxProfiles, aiPlayers, error);
    assert(host != nullptr);
    ConnectedClient p1 = connectClient(*host, PlayerId{1});
    ConnectedClient p2 = connectClient(*host, PlayerId{2});
    ConnectedClient p3 = connectClient(*host, PlayerId{3});
    for (const ConnectedClient* client : {&p1, &p2, &p3}) {
        assert(client->session->controller().matchMode() ==
               client::MatchMode::Sandbox);
        const auto& players = client->session->controller().matchMetadata().players;
        assert(players.size() == 6);
        for (std::uint32_t slot = 1; slot <= 6; ++slot) {
            assert(players[slot - 1].player == PlayerId{slot});
            assert(players[slot - 1].slot == static_cast<PlayerSlot>(slot - 1));
        }
    }
    for (ConnectedClient* client : {&p1, &p2, &p3}) {
        const auto* snapshot = client->session->controller().displayedSnapshot();
        assert(snapshot != nullptr);
        assert(client->session->controller().submitAndLock(
            actionOfType(*snapshot, ActionType::Search)));
    }
    assert(host->authoritativeRound() == RoundNumber{2});
    assert(host->resolvedRoundCount() == 1);
    p1.ingestUpdates();
    p2.ingestUpdates();
    p3.ingestUpdates();
    assert(p1.session->controller().displayedSnapshot()->round == RoundNumber{2});
}

void mixedOnlineSandboxRoundsRemainExactAndPlayerSafe() {
    for (std::size_t hunterCount = 2; hunterCount <= 6; ++hunterCount) {
        auto config = client::defaultSandboxSessionConfig(hunterCount);
        config.humanPlayerCount = 2;
        config.caveCount = std::max<std::size_t>(30, hunterCount * 10);
        config.jackalCount = 0;
        config.mapSeed = MapSeed{8100 + hunterCount};
        config.matchSeed = MatchSeed{9100 + hunterCount};

        std::vector<client::PublicPlayerProfile> sandboxProfiles;
        std::vector<client::ai::AiConfig> aiPlayers;
        for (std::size_t slot = 1; slot <= hunterCount; ++slot) {
            sandboxProfiles.push_back({static_cast<PlayerId>(slot),
                "Hunter " + std::to_string(slot), {"arrow-right-black"},
                {"circle-black"}});
            if (slot > config.humanPlayerCount) {
                aiPlayers.push_back({client::ai::AiDifficulty::Medium,
                    client::ai::AiBehavior::Balanced,
                    static_cast<PlayerId>(slot),
                    client::ai::AiSeed{10000 + slot}});
            }
        }

        std::string error;
        auto host = AuthoritativeInMemoryMatch::createSandbox(
            config, sandboxProfiles, aiPlayers, error);
        assert(host != nullptr && error.empty());
        ConnectedClient first = connectClient(*host, PlayerId{1});
        ConnectedClient second = connectClient(*host, PlayerId{2});
        const PlayerRoundSnapshot firstBefore =
            *first.session->controller().displayedSnapshot();
        const PlayerRoundSnapshot secondBefore =
            *second.session->controller().displayedSnapshot();
        assert(firstBefore.player == PlayerId{1});
        assert(secondBefore.player == PlayerId{2});
        assert(firstBefore.currentCave != secondBefore.currentCave);
        assert(!containsCave(firstBefore.map, secondBefore.currentCave));
        assert(!containsCave(secondBefore.map, firstBefore.currentCave));
        for (const auto& observation : firstBefore.observations)
            assert(observation.viewer == PlayerId{1});
        for (const auto& observation : secondBefore.observations)
            assert(observation.viewer == PlayerId{2});

        const PlayerAction firstSearch = makePlayerAction(
            actionOfType(firstBefore, ActionType::Search), PlayerId{1});
        const PlayerAction secondSearch = makePlayerAction(
            actionOfType(secondBefore, ActionType::Search), PlayerId{2});
        assert(first.endpoint->send(ClientCommand{kProtocolVersion,
            SubmitActionCommand{firstBefore.round, firstSearch}}));
        assert(second.endpoint->send(ClientCommand{kProtocolVersion,
            SubmitActionCommand{secondBefore.round, secondSearch}}));
        assert(second.endpoint->send(ClientCommand{kProtocolVersion,
            LockActionCommand{secondBefore.round, PlayerId{2}}}));
        assert(first.endpoint->send(ClientCommand{kProtocolVersion,
            LockActionCommand{firstBefore.round, PlayerId{1}}}));
        assert(host->authoritativeRound() == firstBefore.round + 1);
        assert(host->resolvedRoundCount() == 1);

        first.ingestUpdates();
        second.ingestUpdates();
        const auto* firstAfter =
            first.session->controller().displayedSnapshot();
        const auto* secondAfter =
            second.session->controller().displayedSnapshot();
        assert(firstAfter->round == firstBefore.round + 1);
        assert(secondAfter->round == secondBefore.round + 1);
        for (const auto& observation : firstAfter->observations)
            assert(observation.viewer == PlayerId{1});
        for (const auto& observation : secondAfter->observations)
            assert(observation.viewer == PlayerId{2});

        assert(!first.endpoint->send(ClientCommand{kProtocolVersion,
            SubmitActionCommand{firstBefore.round, firstSearch}}));
        assert(!first.endpoint->send(ClientCommand{kProtocolVersion,
            LockActionCommand{firstBefore.round, PlayerId{1}}}));
        assert(host->authoritativeRound() == firstBefore.round + 1);
        assert(host->resolvedRoundCount() == 1);
    }
}

} // namespace

int main() {
    twoClientsAdvanceOneAuthoritativeRound();
    noOpTimeTickPreservesResolvedRoundObservations();
    spoofedMalformedAndForgedCommandsAreRejected();
    actionCommandsAreBoundToTheAuthoritativeRound();
    explicitQuitEliminatesImmediatelyAndSurvivorContinues();
    reconnectPreservesRoundStateAndRejectsStaleEndpoints();
    authenticatedServerReturnsOnlyPublicLeaderboardFields();
    onlineSandboxTerminalStateReachesEveryHuman();
    slowOnlineSandboxHumanExpiresWithoutBlockingRound();
    onlineSandboxSpectatorAdvancesWhenWatchedHunterDies();
    sandboxLaunchPreservesSlotsAndRunsServerAi();
    mixedOnlineSandboxRoundsRemainExactAndPlayerSafe();
    return 0;
}
