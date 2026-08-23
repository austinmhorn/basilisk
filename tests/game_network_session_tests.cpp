#include <cassert>
#include <concepts>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "NetworkGameSessionAdapter.hpp"
#include "NetworkEndpointConfig.hpp"
#include "NetworkProtocol.hpp"
#include "NetworkWireCodec.hpp"

using namespace basilisk;
using namespace basilisk::game;
using namespace basilisk::game::network;

namespace {

void configurableNetworkEndpointsPreserveDefaultsAndCustomValues() {
    NetworkEndpointConfig config;
    assert(config.bindAddress == "127.0.0.1");
    assert(config.serverPort == 8765);
    assert(config.connectUrl == "ws://127.0.0.1:8765");
    std::string error;
    assert(applyNetworkEndpointOption(
        "--bind", "0.0.0.0", config, error));
    assert(config.bindAddress == "0.0.0.0");
    assert(applyNetworkEndpointOption("--port", "9443", config, error));
    assert(config.serverPort == 9443);
    assert(!applyNetworkEndpointOption("--port", "0", config, error));
    assert(!applyNetworkEndpointOption("--port", "65536", config, error));
    assert(applyNetworkEndpointOption(
        "--connect", "ws://192.168.1.42:8765", config, error));
    assert(config.connectUrl == "ws://192.168.1.42:8765");
    assert(applyNetworkEndpointOption(
        "--connect", "wss://example.com/game", config, error));
    assert(config.connectUrl == "wss://example.com/game");
    assert(!applyNetworkEndpointOption("--connect", "", config, error));
}

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

template <typename T>
concept HasAccountId = requires(T value) { value.accountId; };

template <typename T>
concept HasAuthToken = requires(T value) { value.authToken; };

template <typename T>
concept HasLedgerRows = requires(T value) { value.ledgerRows; };

template <typename T>
concept HasTrophyMatchId = requires(T value) { value.trophyMatchId; };

static_assert(kProtocolVersion == 3);
static_assert(std::variant_size_v<ClientCommandPayload> == 5);
static_assert(!HasWorld<ServerBootstrap>);
static_assert(!HasAuthoritativeState<ServerBootstrap>);
static_assert(!HasEvents<ServerBootstrap>);
static_assert(!HasMapSeed<ServerBootstrap>);
static_assert(!HasMatchSeed<ServerBootstrap>);
static_assert(!HasHiddenTopology<ServerBootstrap>);
static_assert(!HasDebugTruth<ServerBootstrap>);
static_assert(!HasAccountId<ServerBootstrap>);
static_assert(!HasAuthToken<ServerBootstrap>);
static_assert(!HasLedgerRows<ServerBootstrap>);
static_assert(!HasTrophyMatchId<ServerBootstrap>);
static_assert(!HasWorld<ServerUpdate>);
static_assert(!HasAuthoritativeState<ServerUpdate>);
static_assert(!HasEvents<ServerUpdate>);
static_assert(!HasMapSeed<ServerUpdate>);
static_assert(!HasMatchSeed<ServerUpdate>);
static_assert(!HasHiddenTopology<ServerUpdate>);
static_assert(!HasDebugTruth<ServerUpdate>);
static_assert(!HasAccountId<ServerUpdate>);
static_assert(!HasAuthToken<ServerUpdate>);
static_assert(!HasLedgerRows<ServerUpdate>);
static_assert(!HasTrophyMatchId<ServerUpdate>);
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

// Test-only byte transport: production remains transport and framing agnostic.
class InMemoryByteTransport final : public ClientTransport {
public:
    std::vector<WireBytes> frames;
    std::vector<ClientCommand> decodedCommands;

    [[nodiscard]] bool send(const ClientCommand& command) override {
        WireBytes bytes;
        std::string error;
        if (!encodeWire(command, bytes, error)) return false;
        ClientCommand decoded;
        if (!decodeClientCommand(bytes, decoded, error)) return false;
        frames.push_back(std::move(bytes));
        decodedCommands.push_back(std::move(decoded));
        return true;
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

PlayerRoundSnapshot representativeSnapshot() {
    PlayerRoundSnapshot snapshot;
    snapshot.player = PlayerId{1};
    snapshot.round = RoundNumber{27};
    snapshot.health = 70;
    snapshot.maxHealth = 100;
    snapshot.arrows = 3;
    snapshot.maxArrows = 5;
    snapshot.alive = true;
    snapshot.inventory.items = {
        ItemType::HealingDraught,
        ItemType::OldHuntersMap,
        ItemType::SurveyFragment,
    };
    snapshot.inventory.capacity = 4;
    snapshot.currentCave = CaveId{7};
    snapshot.map.currentCave = CaveId{7};
    snapshot.map.caves = {
        DiscoveredCaveView{
            CaveId{7},
            {
                TunnelView{TunnelId{1}, CaveId{12}, false},
                TunnelView{TunnelId{2}, std::nullopt, true},
            }},
        DiscoveredCaveView{
            CaveId{12},
            {TunnelView{TunnelId{1}, CaveId{7}, false}}},
    };
    snapshot.looseArrowPresent = true;
    snapshot.temporarilyRevealedPitCaves = {CaveId{34}};

    AvailableAction moveKnown;
    moveKnown.type = ActionType::Move;
    moveKnown.targetCave = CaveId{12};
    AvailableAction moveUnknown;
    moveUnknown.type = ActionType::Move;
    moveUnknown.targetTunnel = TunnelId{2};
    AvailableAction useSurvey;
    useSurvey.type = ActionType::UseItem;
    useSurvey.targetItem = ItemType::SurveyFragment;
    useSurvey.targetTunnel = TunnelId{2};
    AvailableAction escape;
    escape.type = ActionType::Contextual;
    escape.contextualAction = ContextualActionType::Escape;
    snapshot.availableActions = {
        moveKnown,
        moveUnknown,
        AvailableAction{ActionType::Search},
        useSurvey,
        escape,
    };

    PlayerObservation observation;
    observation.type = ObservationType::OldHuntersMapDistance;
    observation.viewer = PlayerId{1};
    observation.cave = CaveId{19};
    observation.otherPlayer = PlayerId{2};
    observation.amount = -3;
    observation.basiliskBehavior = BasiliskBehavior::Enraged;
    observation.itemType = ItemType::OldHuntersMap;
    observation.tunnel = TunnelId{2};
    snapshot.observations = {observation};
    snapshot.recoverableRivalSigilAvailable = true;
    snapshot.hasHunterSigil = true;
    snapshot.extractionCave = CaveId{34};
    snapshot.matchStatus = MatchStatus::Completed;
    snapshot.matchOutcome = MatchOutcome::EscapedWithSigil;
    snapshot.winner = PlayerId{1};
    return snapshot;
}

PlayerFixedMapGeometry representativeGeometry() {
    PlayerFixedMapGeometry geometry;
    geometry.fullBounds = LogicalBounds{-14.5, -8.25, 15.75, 9.5, true};
    geometry.discoveredCaves.emplace(CaveId{7}, LogicalPoint{-2.5, 1.25});
    geometry.discoveredCaves.emplace(CaveId{12}, LogicalPoint{4.75, -0.5});
    geometry.unknownExitEndpoints.emplace(
        MapExitKey{CaveId{7}, TunnelId{2}}, LogicalPoint{8.5, 3.125});
    geometry.temporarilyRevealedCaves.emplace(
        CaveId{34}, LogicalPoint{8.5, 3.125});
    return geometry;
}

ServerBootstrap representativeBootstrap() {
    ServerBootstrap bootstrap = bootstrapFor();
    bootstrap.initialSnapshot = representativeSnapshot();
    bootstrap.initialMapGeometry = representativeGeometry();
    bootstrap.trophyTotal = -7;
    return bootstrap;
}

template <typename Message, typename Decode>
void assertStableRoundTrip(const Message& source, Decode decode) {
    WireBytes encoded;
    std::string error;
    assert(encodeWire(source, encoded, error));
    assert(error.empty());
    Message decoded;
    assert(decode(encoded, decoded, error));
    assert(error.empty());
    WireBytes reencoded;
    assert(encodeWire(decoded, reencoded, error));
    assert(encoded == reencoded);
}

void allServerFieldsRoundTripExactly() {
    const ServerBootstrap source = representativeBootstrap();
    assertStableRoundTrip(source, decodeServerBootstrap);

    WireBytes bytes;
    std::string error;
    assert(encodeWire(source, bytes, error));
    ServerBootstrap decoded;
    assert(decodeServerBootstrap(bytes, decoded, error));
    assert(decoded.protocolVersion == kProtocolVersion);
    assert(decoded.trophyTotal == -7);
    assert(decoded.matchMetadata.totalCaves == 40);
    assert(decoded.matchMetadata.players.size() == 2);
    assert(decoded.matchMetadata.players[1].slot == PlayerSlot::P2);
    assert(decoded.profiles[0].username == "Mara Voss");
    assert(decoded.profiles[0].callingCardId.value == "ember-field");
    assert(decoded.profiles[1].emblemId.value == "ward");
    assert(decoded.viewContext.mode == client::ClientViewMode::Playing);

    const PlayerRoundSnapshot& snapshot = decoded.initialSnapshot;
    assert(snapshot.player == PlayerId{1});
    assert(snapshot.round == RoundNumber{27});
    assert(snapshot.health == 70 && snapshot.maxHealth == 100);
    assert(snapshot.arrows == 3 && snapshot.maxArrows == 5);
    assert(snapshot.alive && snapshot.looseArrowPresent);
    assert(snapshot.inventory.capacity == 4);
    assert(snapshot.inventory.items == source.initialSnapshot.inventory.items);
    assert(snapshot.currentCave == CaveId{7});
    assert(snapshot.map.caves.size() == 2);
    assert(snapshot.map.caves[0].exits[0].destination == CaveId{12});
    assert(snapshot.map.caves[0].exits[1].id == TunnelId{2});
    assert(!snapshot.map.caves[0].exits[1].destination.has_value());
    assert(snapshot.map.caves[0].exits[1].strongColdDraft);
    assert(snapshot.temporarilyRevealedPitCaves ==
           std::vector<CaveId>{CaveId{34}});
    assert(snapshot.availableActions.size() == 5);
    assert(snapshot.availableActions[0].targetCave == CaveId{12});
    assert(snapshot.availableActions[1].targetTunnel == TunnelId{2});
    assert(snapshot.availableActions[3].targetItem == ItemType::SurveyFragment);
    assert(snapshot.availableActions[4].contextualAction ==
           ContextualActionType::Escape);
    assert(snapshot.observations.size() == 1);
    assert(snapshot.observations[0].cave == CaveId{19});
    assert(snapshot.observations[0].otherPlayer == PlayerId{2});
    assert(snapshot.observations[0].amount == -3);
    assert(snapshot.observations[0].basiliskBehavior ==
           BasiliskBehavior::Enraged);
    assert(snapshot.observations[0].itemType == ItemType::OldHuntersMap);
    assert(snapshot.observations[0].tunnel == TunnelId{2});
    assert(snapshot.recoverableRivalSigilAvailable);
    assert(snapshot.hasHunterSigil);
    assert(snapshot.extractionCave == CaveId{34});
    assert(snapshot.matchStatus == MatchStatus::Completed);
    assert(snapshot.matchOutcome == MatchOutcome::EscapedWithSigil);
    assert(snapshot.winner == PlayerId{1});

    const PlayerFixedMapGeometry& geometry = decoded.initialMapGeometry;
    assert(geometry.fullBounds == source.initialMapGeometry.fullBounds);
    assert(geometry.discoveredCaves == source.initialMapGeometry.discoveredCaves);
    assert(geometry.unknownExitEndpoints ==
           source.initialMapGeometry.unknownExitEndpoints);
    assert(geometry.temporarilyRevealedCaves ==
           source.initialMapGeometry.temporarilyRevealedCaves);

    ServerUpdate update;
    update.snapshot = source.initialSnapshot;
    update.mapGeometry = source.initialMapGeometry;
    update.viewContext = client::ClientViewContext{
        PlayerId{2}, PlayerId{1}, client::ClientViewMode::Spectating,
        std::nullopt};
    update.trophyTotal = 19;
    assertStableRoundTrip(update, decodeServerUpdate);
    WireBytes updateBytes;
    assert(encodeWire(update, updateBytes, error));
    ServerUpdate decodedUpdate;
    assert(decodeServerUpdate(updateBytes, decodedUpdate, error));
    assert(decodedUpdate.trophyTotal == 19);
}

void everyClientCommandRoundTrips() {
    PlayerAction action;
    action.player = PlayerId{7};
    action.type = ActionType::UseItem;
    action.targetCave = CaveId{12};
    action.targetItem = ItemType::SurveyFragment;
    action.contextualAction = ContextualActionType::Escape;
    action.targetTunnel = TunnelId{9};
    const std::vector<ClientCommand> commands{
        ClientCommand{kProtocolVersion, SubmitActionCommand{action}},
        ClientCommand{kProtocolVersion, LockActionCommand{PlayerId{7}}},
        ClientCommand{kProtocolVersion,
            WatchRemainingHunterCommand{PlayerId{7}, PlayerId{11}}},
        ClientCommand{kProtocolVersion, QuitCommand{PlayerId{7}}},
        ClientCommand{kProtocolVersion, LeaderboardPageRequest{20, 10}},
    };
    for (const ClientCommand& command : commands)
        assertStableRoundTrip(command, decodeClientCommand);

    WireBytes bytes;
    std::string error;
    assert(encodeWire(commands.front(), bytes, error));
    ClientCommand decoded;
    assert(decodeClientCommand(bytes, decoded, error));
    const auto& submitted = std::get<SubmitActionCommand>(decoded.payload).action;
    assert(submitted.player == action.player);
    assert(submitted.type == action.type);
    assert(submitted.targetCave == action.targetCave);
    assert(submitted.targetItem == action.targetItem);
    assert(submitted.contextualAction == action.contextualAction);
    assert(submitted.targetTunnel == action.targetTunnel);
}

void publicLeaderboardRequestAndResponseRoundTrip() {
    const ClientCommand request{
        kProtocolVersion,
        LeaderboardPageRequest{25, 10},
    };
    assertStableRoundTrip(request, decodeClientCommand);

    const LeaderboardPageResponse response{
        kProtocolVersion,
        25,
        {
            {1, Username{"mara"}, 42},
            {2, Username{"elias"}, -3},
        },
    };
    assertStableRoundTrip(response, decodeLeaderboardPageResponse);

    WireBytes bytes;
    std::string error;
    assert(encodeWire(response, bytes, error));
    LeaderboardPageResponse decoded;
    assert(decodeLeaderboardPageResponse(bytes, decoded, error));
    assert(decoded.offset == 25);
    assert(decoded.entries == response.entries);

    bytes.push_back(0);
    assert(!decodeLeaderboardPageResponse(bytes, decoded, error));
    assert(!encodeWire(
        ClientCommand{kProtocolVersion, LeaderboardPageRequest{0, 0}},
        bytes, error));
    assert(!encodeWire(
        ClientCommand{kProtocolVersion, LeaderboardPageRequest{
            0, kMaximumLeaderboardPageSize + 1}}, bytes, error));
}

void goldenFixtureIsStable() {
    const ClientCommand quit{
        kProtocolVersion,
        QuitCommand{PlayerId{42}},
    };
    WireBytes bytes;
    std::string error;
    assert(encodeWire(quit, bytes, error));
    const WireBytes expected{
        0x42, 0x53, 0x4b, 0x31,
        0x13,
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x2a,
    };
    assert(bytes == expected);
}

void malformedWireIsRejected() {
    WireBytes valid;
    std::string error;
    assert(encodeWire(
        ClientCommand{kProtocolVersion, QuitCommand{PlayerId{42}}},
        valid,
        error));

    for (std::size_t size = 0; size < valid.size(); ++size) {
        ClientCommand decoded;
        assert(!decodeClientCommand(
            std::span<const std::uint8_t>{valid.data(), size}, decoded, error));
        assert(!error.empty());
    }

    WireBytes malformed = valid;
    malformed.push_back(0xff);
    ClientCommand command;
    assert(!decodeClientCommand(malformed, command, error));

    malformed = valid;
    malformed[4] = 0xff;
    assert(!decodeClientCommand(malformed, command, error));

    malformed = valid;
    malformed[8] = 0x04;
    assert(!decodeClientCommand(malformed, command, error));

    PlayerAction action;
    action.player = PlayerId{1};
    WireBytes submitted;
    assert(encodeWire(
        ClientCommand{kProtocolVersion, SubmitActionCommand{action}},
        submitted,
        error));
    submitted[13] = 0xff;
    assert(!decodeClientCommand(submitted, command, error));
    submitted[13] = static_cast<std::uint8_t>(ActionType::Search);
    submitted[14] = 0x02;
    assert(!decodeClientCommand(submitted, command, error));

    WireBytes bootstrapBytes;
    assert(encodeWire(representativeBootstrap(), bootstrapBytes, error));
    ServerBootstrap bootstrap;

    WireBytes malformedBootstrap = bootstrapBytes;
    malformedBootstrap.pop_back();
    assert(!decodeServerBootstrap(malformedBootstrap, bootstrap, error));
    assert(error.find("Truncated") != std::string::npos);

    malformedBootstrap = bootstrapBytes;
    malformedBootstrap.push_back(0xff);
    assert(!decodeServerBootstrap(malformedBootstrap, bootstrap, error));

    malformedBootstrap = bootstrapBytes;
    malformedBootstrap[8] = 0x04;
    assert(!decodeServerBootstrap(malformedBootstrap, bootstrap, error));

    malformedBootstrap = bootstrapBytes;
    malformedBootstrap[4] = 0xff;
    assert(!decodeServerBootstrap(malformedBootstrap, bootstrap, error));

    bootstrapBytes[13] = 0x00;
    bootstrapBytes[14] = 0x00;
    bootstrapBytes[15] = 0x40;
    bootstrapBytes[16] = 0x01;
    assert(!decodeServerBootstrap(bootstrapBytes, bootstrap, error));

    ServerBootstrap missingRequiredProfile = representativeBootstrap();
    missingRequiredProfile.profiles.pop_back();
    assert(!encodeWire(missingRequiredProfile, bootstrapBytes, error));

    ServerUpdate unsafeGeometry;
    unsafeGeometry.snapshot = representativeSnapshot();
    unsafeGeometry.mapGeometry = representativeGeometry();
    unsafeGeometry.mapGeometry.discoveredCaves.emplace(
        CaveId{999}, LogicalPoint{0.0, 0.0});
    assert(!encodeWire(unsafeGeometry, bootstrapBytes, error));
}

void byteTransportAndDecodedServerDataReachController() {
    WireBytes bootstrapFrame;
    std::string error;
    assert(encodeWire(representativeBootstrap(), bootstrapFrame, error));
    ServerBootstrap decodedBootstrap;
    assert(decodeServerBootstrap(
        bootstrapFrame, decodedBootstrap, error));

    auto transport = std::make_shared<InMemoryByteTransport>();
    auto adapter = NetworkGameSessionAdapter::create(
        std::move(decodedBootstrap), transport, error);
    assert(adapter != nullptr);
    assert(adapter->controller().displayedSnapshot()->round == RoundNumber{27});
    assert(adapter->controller().submitAndLock(
        adapter->controller().displayedSnapshot()->availableActions.front()));
    assert(transport->frames.size() == 2);
    assert(transport->decodedCommands.size() == 2);
    assert(std::holds_alternative<SubmitActionCommand>(
        transport->decodedCommands[0].payload));
    assert(std::holds_alternative<LockActionCommand>(
        transport->decodedCommands[1].payload));

    ServerUpdate sourceUpdate;
    sourceUpdate.snapshot = representativeSnapshot();
    sourceUpdate.snapshot.round = RoundNumber{28};
    sourceUpdate.snapshot.health = 61;
    sourceUpdate.mapGeometry = representativeGeometry();
    sourceUpdate.trophyTotal = 11;
    WireBytes updateFrame;
    assert(encodeWire(sourceUpdate, updateFrame, error));
    ServerUpdate decodedUpdate;
    assert(decodeServerUpdate(updateFrame, decodedUpdate, error));
    assert(adapter->ingest(std::move(decodedUpdate), error));
    assert(adapter->controller().displayedSnapshot()->round == RoundNumber{28});
    assert(adapter->controller().displayedSnapshot()->health == 61);
    assert(adapter->controller().trophyTotal() == 11);
    assert(adapter->controller().displayedMapGeometry()
               ->unknownExitEndpoints.contains(
                   MapExitKey{CaveId{7}, TunnelId{2}}));

    assert(adapter->requestLeaderboard(10, 5));
    assert(transport->decodedCommands.size() == 3);
    const auto* request = std::get_if<LeaderboardPageRequest>(
        &transport->decodedCommands.back().payload);
    assert(request != nullptr && request->offset == 10 && request->limit == 5);
    LeaderboardPageResponse leaderboard{
        kProtocolVersion,
        10,
        {{2, Username{"mara"}, 17}},
    };
    assert(adapter->ingest(std::move(leaderboard), error));
    assert(adapter->leaderboardPage().has_value());
    assert(adapter->leaderboardPage()->entries.front().trophyTotal == 17);
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
    assert(controller.trophyTotal() == 0);
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
            -3,
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
    assert(adapter->controller().trophyTotal() == -3);
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
    configurableNetworkEndpointsPreserveDefaultsAndCustomValues();
    allServerFieldsRoundTripExactly();
    everyClientCommandRoundTrips();
    publicLeaderboardRequestAndResponseRoundTrip();
    goldenFixtureIsStable();
    malformedWireIsRejected();
    byteTransportAndDecodedServerDataReachController();
    bootstrapCreatesUsableController();
    outboundCommandsReachTransportExactly();
    inboundUpdatesReachController();
    protocolMismatchIsRejectedCleanly();
    return 0;
}
