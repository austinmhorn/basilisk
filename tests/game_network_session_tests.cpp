#include <cassert>
#include <concepts>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "NetworkGameSessionAdapter.hpp"
#include "NetworkProtocol.hpp"

using namespace basilisk;
using namespace basilisk::game;
using namespace basilisk::game::network;

namespace {

template <typename T>
concept HasWorld = requires(T value) { value.world; };

template <typename T>
concept HasAuthoritativeState = requires(T value) { value.matchState; };

template <typename T>
concept HasEvents = requires(T value) { value.events; };

template <typename T>
concept HasMapSeed = requires(T value) { value.mapSeed; };

template <typename T>
concept HasMatchSeed = requires(T value) { value.matchSeed; };

template <typename T>
concept HasHiddenTopology = requires(T value) { value.topology; };

template <typename T>
concept HasDebugTruth = requires(T value) { value.debugTruth; };

static_assert(kProtocolVersion == 1);
static_assert(std::variant_size_v<ClientCommandPayload> == 4);
static_assert(!HasWorld<ServerBootstrap>);
static_assert(!HasAuthoritativeState<ServerBootstrap>);
static_assert(!HasEvents<ServerBootstrap>);
static_assert(!HasMapSeed<ServerBootstrap>);
static_assert(!HasMatchSeed<ServerBootstrap>);
static_assert(!HasHiddenTopology<ServerBootstrap>);
static_assert(!HasDebugTruth<ServerBootstrap>);
static_assert(!HasWorld<ServerUpdate>);
static_assert(!HasAuthoritativeState<ServerUpdate>);
static_assert(!HasEvents<ServerUpdate>);
static_assert(!HasMapSeed<ServerUpdate>);
static_assert(!HasMatchSeed<ServerUpdate>);
static_assert(!HasHiddenTopology<ServerUpdate>);
static_assert(!HasDebugTruth<ServerUpdate>);
static_assert(!HasWorld<ClientCommand>);
static_assert(!HasAuthoritativeState<ClientCommand>);
static_assert(!HasEvents<ClientCommand>);
static_assert(!HasMapSeed<ClientCommand>);
static_assert(!HasMatchSeed<ClientCommand>);
static_assert(!HasHiddenTopology<ClientCommand>);
static_assert(!HasDebugTruth<ClientCommand>);

class FakeTransport final : public ClientTransport {
public:
    bool succeeds{true};
    std::vector<ClientCommand> commands;

    [[nodiscard]] bool send(const ClientCommand& command) override {
        commands.push_back(command);
        return succeeds;
    }
};

PlayerFixedMapGeometry geometryFor(CaveId cave, double x) {
    PlayerFixedMapGeometry geometry;
    geometry.fullBounds = LogicalBounds{-10.0, -8.0, 10.0, 8.0, true};
    geometry.discoveredCaves.emplace(cave, LogicalPoint{x, 0.0});
    return geometry;
}

PlayerRoundSnapshot snapshotFor(
    PlayerId player,
    RoundNumber round,
    CaveId cave) {

    PlayerRoundSnapshot snapshot;
    snapshot.player = player;
    snapshot.round = round;
    snapshot.health = 80;
    snapshot.maxHealth = 100;
    snapshot.arrows = 3;
    snapshot.maxArrows = 5;
    snapshot.alive = true;
    snapshot.currentCave = cave;
    snapshot.map.currentCave = cave;
    snapshot.map.caves = {DiscoveredCaveView{cave, {}}};
    snapshot.availableActions = {AvailableAction{ActionType::Search}};
    return snapshot;
}

ServerBootstrap bootstrapFor(PlayerId localPlayer = PlayerId{1}) {
    ServerBootstrap bootstrap;
    bootstrap.matchMetadata.totalCaves = 40;
    bootstrap.matchMetadata.players = {
        PublicPlayerSlot{PlayerId{1}, PlayerSlot::P1},
        PublicPlayerSlot{PlayerId{2}, PlayerSlot::P2},
    };
    bootstrap.profiles = {
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
    bootstrap.viewContext = client::ClientViewContext{
        localPlayer,
        localPlayer,
        client::ClientViewMode::Playing,
        std::nullopt,
    };
    bootstrap.initialSnapshot =
        snapshotFor(localPlayer, RoundNumber{1}, CaveId{7});
    bootstrap.initialMapGeometry = geometryFor(CaveId{7}, -2.0);
    return bootstrap;
}

std::unique_ptr<NetworkGameSessionAdapter> makeAdapter(
    const std::shared_ptr<FakeTransport>& transport) {

    std::string error;
    auto adapter = NetworkGameSessionAdapter::create(
        bootstrapFor(), transport, error);
    assert(adapter != nullptr);
    assert(error.empty());
    return adapter;
}

void bootstrapCreatesUsableController() {
    auto transport = std::make_shared<FakeTransport>();
    auto adapter = makeAdapter(transport);
    const ClientSessionController& controller = adapter->controller();

    assert(controller.matchMetadata().totalCaves == 40);
    assert(controller.profiles().size() == 2);
    assert(controller.viewContext().localPlayer == PlayerId{1});
    assert(controller.displayedSnapshot() != nullptr);
    assert(controller.displayedSnapshot()->player == PlayerId{1});
    assert(controller.displayedSnapshot()->currentCave == CaveId{7});
    assert(controller.displayedMapGeometry() != nullptr);
    assert(controller.displayedMapGeometry()->discoveredCaves.at(CaveId{7}) ==
           (LogicalPoint{-2.0, 0.0}));
    assert(controller.canSubmitActions());
    assert(transport->commands.empty());
}

void outboundCommandsReachTransportExactly() {
    auto transport = std::make_shared<FakeTransport>();
    auto adapter = makeAdapter(transport);
    ClientSessionController& controller = adapter->controller();

    AvailableAction move;
    move.type = ActionType::Move;
    move.targetCave = CaveId{12};
    assert(controller.submitAndLock(move));
    assert(transport->commands.size() == 2);
    for (const ClientCommand& command : transport->commands) {
        assert(command.protocolVersion == kProtocolVersion);
    }
    const auto* submitted = std::get_if<SubmitActionCommand>(
        &transport->commands[0].payload);
    assert(submitted != nullptr);
    assert(submitted->action.player == PlayerId{1});
    assert(submitted->action.type == ActionType::Move);
    assert(submitted->action.targetCave == CaveId{12});
    const auto* locked =
        std::get_if<LockActionCommand>(&transport->commands[1].payload);
    assert(locked != nullptr && locked->player == PlayerId{1});

    std::string error;
    assert(adapter->ingest(
        ServerUpdate{
            kProtocolVersion,
            snapshotFor(PlayerId{2}, RoundNumber{2}, CaveId{16}),
            geometryFor(CaveId{16}, 3.0),
            std::nullopt,
        },
        error));
    PlayerRoundSnapshot defeated =
        snapshotFor(PlayerId{1}, RoundNumber{2}, CaveId{7});
    defeated.alive = false;
    defeated.availableActions.clear();
    const client::ClientViewContext defeatedView{
        PlayerId{1},
        PlayerId{1},
        client::ClientViewMode::Defeated,
        PlayerId{2},
    };
    assert(adapter->ingest(
        ServerUpdate{
            kProtocolVersion,
            std::move(defeated),
            geometryFor(CaveId{7}, -2.0),
            defeatedView,
        },
        error));

    assert(controller.watchRemainingHunter());
    assert(controller.viewContext().mode == client::ClientViewMode::Spectating);
    assert(controller.viewContext().localPlayer == PlayerId{1});
    assert(controller.viewContext().viewedPlayer == PlayerId{2});
    assert(!controller.canSubmitActions());
    assert(controller.quit());
    assert(transport->commands.size() == 4);

    const auto* watch = std::get_if<WatchRemainingHunterCommand>(
        &transport->commands[2].payload);
    assert(watch != nullptr);
    assert(watch->localPlayer == PlayerId{1});
    assert(watch->viewedPlayer == PlayerId{2});
    const auto* quit =
        std::get_if<QuitCommand>(&transport->commands[3].payload);
    assert(quit != nullptr && quit->player == PlayerId{1});
}

void inboundUpdatesReachController() {
    auto transport = std::make_shared<FakeTransport>();
    auto adapter = makeAdapter(transport);
    std::string error;

    PlayerRoundSnapshot update =
        snapshotFor(PlayerId{1}, RoundNumber{3}, CaveId{12});
    update.health = 55;
    const client::ClientViewContext context{
        PlayerId{1},
        PlayerId{1},
        client::ClientViewMode::Playing,
        std::nullopt,
    };
    assert(adapter->ingest(
        ServerUpdate{
            kProtocolVersion,
            std::move(update),
            geometryFor(CaveId{12}, 4.0),
            context,
        },
        error));
    assert(error.empty());
    assert(adapter->controller().displayedSnapshot()->round == RoundNumber{3});
    assert(adapter->controller().displayedSnapshot()->health == 55);
    assert(adapter->controller().displayedMapGeometry()
               ->discoveredCaves.at(CaveId{12}) ==
           (LogicalPoint{4.0, 0.0}));
    assert(adapter->controller().viewContext().mode ==
           client::ClientViewMode::Playing);
    assert(transport->commands.empty());
}

void protocolMismatchIsRejectedCleanly() {
    auto transport = std::make_shared<FakeTransport>();
    ServerBootstrap bootstrap = bootstrapFor();
    bootstrap.protocolVersion = kProtocolVersion + 1;
    std::string error;
    auto rejected = NetworkGameSessionAdapter::create(
        std::move(bootstrap), transport, error);
    assert(rejected == nullptr);
    assert(!error.empty());
    assert(transport->commands.empty());

    auto adapter = makeAdapter(transport);
    const RoundNumber before =
        adapter->controller().displayedSnapshot()->round;
    ServerUpdate update{
        kProtocolVersion + 1,
        snapshotFor(PlayerId{1}, RoundNumber{9}, CaveId{12}),
        geometryFor(CaveId{12}, 4.0),
        std::nullopt,
    };
    assert(!adapter->ingest(std::move(update), error));
    assert(!error.empty());
    assert(adapter->controller().displayedSnapshot()->round == before);
}

} // namespace

int main() {
    bootstrapCreatesUsableController();
    outboundCommandsReachTransportExactly();
    inboundUpdatesReachController();
    protocolMismatchIsRejectedCleanly();
    return 0;
}
