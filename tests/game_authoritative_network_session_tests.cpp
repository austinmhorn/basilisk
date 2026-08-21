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

void spoofedMalformedAndForgedCommandsAreRejected() {
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error);
    assert(host != nullptr);
    ConnectedClient p1 = connectClient(*host, PlayerId{1});

    PlayerAction spoofed;
    spoofed.player = PlayerId{2};
    spoofed.type = ActionType::Search;
    ClientCommand command{kProtocolVersion, SubmitActionCommand{spoofed}};
    WireBytes bytes;
    assert(encodeWire(command, bytes, error));
    assert(!p1.endpoint->sendBytes(bytes, error));
    assert(!error.empty());

    command.payload = LockActionCommand{PlayerId{2}};
    assert(encodeWire(command, bytes, error));
    assert(!p1.endpoint->sendBytes(bytes, error));

    PlayerAction forged;
    forged.player = PlayerId{1};
    forged.type = ActionType::Move;
    forged.targetCave = CaveId{9999};
    command.payload = SubmitActionCommand{forged};
    assert(encodeWire(command, bytes, error));
    assert(!p1.endpoint->sendBytes(bytes, error));

    bytes = {0x42, 0x53, 0x4b};
    assert(!p1.endpoint->sendBytes(bytes, error));

    command = ClientCommand{kProtocolVersion, QuitCommand{PlayerId{1}}};
    assert(encodeWire(command, bytes, error));
    bytes[8] = 0x02;
    assert(!p1.endpoint->sendBytes(bytes, error));

    assert(host->authoritativeRound() == RoundNumber{1});
    assert(host->resolvedRoundCount() == 0);
    assert(!p1.endpoint->takeNextServerFrame().has_value());
}

void quitAndWatchUseExistingLifecycle() {
    std::string error;
    auto host = AuthoritativeInMemoryMatch::create(
        MapSeed{20260816}, MatchSeed{424242}, profiles(), error);
    assert(host != nullptr);
    ConnectedClient p1 = connectClient(*host, PlayerId{1});
    ConnectedClient p2 = connectClient(*host, PlayerId{2});

    assert(p2.session->controller().quit());
    p1.ingestUpdates();
    p2.ingestUpdates();

    host->advanceTime(30'000);
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

    const AvailableAction survivorSearch = actionOfType(
        *p1.session->controller().displayedSnapshot(), ActionType::Search);
    assert(p1.session->controller().submitAndLock(survivorSearch));
    assert(host->authoritativeRound() == RoundNumber{2});
    p1.ingestUpdates();
    p2.ingestUpdates();
    assert(p1.session->controller().displayedSnapshot()->round ==
           RoundNumber{2});
    assert(p2.session->controller().displayedSnapshot()->round ==
           RoundNumber{2});
}

} // namespace

int main() {
    twoClientsAdvanceOneAuthoritativeRound();
    spoofedMalformedAndForgedCommandsAreRejected();
    quitAndWatchUseExistingLifecycle();
    return 0;
}
