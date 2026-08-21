#include "NetworkWireCodec.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <type_traits>
#include <utility>

namespace basilisk::game::network {
namespace {

constexpr std::uint8_t kMagic[] = {'B', 'S', 'K', '1'};
constexpr std::uint32_t kMaximumCollectionEntries = 16'384;
constexpr std::uint32_t kMaximumStringBytes = 16'384;
constexpr std::size_t kMaximumMessageBytes = 16U * 1024U * 1024U;

static_assert(sizeof(double) == sizeof(std::uint64_t));
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(sizeof(int) <= sizeof(std::int32_t));

class Writer {
public:
    Writer(WireBytes& bytes, std::string& error)
        : bytes_(bytes), error_(error) {
        bytes_.clear();
        error_.clear();
    }

    [[nodiscard]] bool ok() const noexcept { return error_.empty(); }

    bool fail(std::string_view message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    void u8(std::uint8_t value) { bytes_.push_back(value); }

    void boolean(bool value) { u8(value ? 1U : 0U); }

    void u32(std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
            u8(static_cast<std::uint8_t>(value >> shift));
    }

    void i32(int value) {
        u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
    }

    void u64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8)
            u8(static_cast<std::uint8_t>(value >> shift));
    }

    bool floating(double value) {
        if (!std::isfinite(value)) return fail("Non-finite coordinate.");
        u64(std::bit_cast<std::uint64_t>(value));
        return true;
    }

    bool count(std::size_t value) {
        if (value > kMaximumCollectionEntries)
            return fail("Collection exceeds wire limit.");
        u32(static_cast<std::uint32_t>(value));
        return true;
    }

    bool string(const std::string& value) {
        if (value.size() > kMaximumStringBytes)
            return fail("String exceeds wire limit.");
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return true;
    }

    bool finish() {
        if (!ok()) return false;
        if (bytes_.size() > kMaximumMessageBytes)
            return fail("Message exceeds wire limit.");
        return true;
    }

private:
    WireBytes& bytes_;
    std::string& error_;
};

class Reader {
public:
    Reader(std::span<const std::uint8_t> bytes, std::string& error)
        : bytes_(bytes), error_(error) {
        error_.clear();
        if (bytes.size() > kMaximumMessageBytes)
            fail("Message exceeds wire limit.");
    }

    [[nodiscard]] bool ok() const noexcept { return error_.empty(); }
    [[nodiscard]] bool done() const noexcept { return offset_ == bytes_.size(); }

    bool fail(std::string_view message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    bool u8(std::uint8_t& value) {
        if (!require(1)) return false;
        value = bytes_[offset_++];
        return true;
    }

    bool boolean(bool& value) {
        std::uint8_t encoded{};
        if (!u8(encoded)) return false;
        if (encoded > 1) return fail("Invalid boolean value.");
        value = encoded != 0;
        return true;
    }

    bool u32(std::uint32_t& value) {
        if (!require(4)) return false;
        value = 0;
        for (int i = 0; i < 4; ++i)
            value = (value << 8U) | bytes_[offset_++];
        return true;
    }

    bool i32(int& value) {
        std::uint32_t encoded{};
        if (!u32(encoded)) return false;
        const std::int32_t decoded = std::bit_cast<std::int32_t>(encoded);
        if (decoded < std::numeric_limits<int>::min() ||
            decoded > std::numeric_limits<int>::max())
            return fail("Integer is outside host range.");
        value = static_cast<int>(decoded);
        return true;
    }

    bool u64(std::uint64_t& value) {
        if (!require(8)) return false;
        value = 0;
        for (int i = 0; i < 8; ++i)
            value = (value << 8U) | bytes_[offset_++];
        return true;
    }

    bool floating(double& value) {
        std::uint64_t encoded{};
        if (!u64(encoded)) return false;
        value = std::bit_cast<double>(encoded);
        if (!std::isfinite(value)) return fail("Non-finite coordinate.");
        return true;
    }

    bool count(std::uint32_t& value) {
        if (!u32(value)) return false;
        if (value > kMaximumCollectionEntries)
            return fail("Collection exceeds wire limit.");
        return true;
    }

    bool string(std::string& value) {
        std::uint32_t size{};
        if (!u32(size)) return false;
        if (size > kMaximumStringBytes) return fail("String exceeds wire limit.");
        if (!require(size)) return false;
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + offset_), size);
        offset_ += size;
        return true;
    }

private:
    bool require(std::size_t count) {
        if (!ok()) return false;
        if (count > bytes_.size() - offset_)
            return fail("Truncated wire message.");
        return true;
    }

    std::span<const std::uint8_t> bytes_;
    std::string& error_;
    std::size_t offset_{0};
};

template <typename Enum>
void writeEnum(Writer& writer, Enum value) {
    writer.u8(static_cast<std::uint8_t>(value));
}

template <typename Enum>
bool readEnum(Reader& reader, Enum& value, std::uint8_t maximum) {
    std::uint8_t encoded{};
    if (!reader.u8(encoded)) return false;
    if (encoded > maximum) return reader.fail("Unknown enum value.");
    value = static_cast<Enum>(encoded);
    return true;
}

template <typename T, typename WriteValue>
bool writeOptional(Writer& writer, const std::optional<T>& value, WriteValue write) {
    writer.boolean(value.has_value());
    return !value.has_value() || write(*value);
}

template <typename T, typename ReadValue>
bool readOptional(Reader& reader, std::optional<T>& value, ReadValue read) {
    bool present{};
    if (!reader.boolean(present)) return false;
    if (!present) {
        value.reset();
        return true;
    }
    T decoded{};
    if (!read(decoded)) return false;
    value = std::move(decoded);
    return true;
}

template <typename T, typename WriteValue>
bool writeVector(Writer& writer, const std::vector<T>& values, WriteValue write) {
    if (!writer.count(values.size())) return false;
    for (const T& value : values)
        if (!write(value)) return false;
    return true;
}

template <typename T, typename ReadValue>
bool readVector(Reader& reader, std::vector<T>& values, ReadValue read) {
    std::uint32_t count{};
    if (!reader.count(count)) return false;
    values.clear();
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        T value{};
        if (!read(value)) return false;
        values.push_back(std::move(value));
    }
    return true;
}

void writeId(Writer& writer, std::uint32_t value) { writer.u32(value); }
bool readId(Reader& reader, std::uint32_t& value) { return reader.u32(value); }

bool writeItem(Writer& writer, ItemType item) {
    writeEnum(writer, item);
    return true;
}

bool readItem(Reader& reader, ItemType& item) {
    return readEnum(reader, item, static_cast<std::uint8_t>(ItemType::OldHuntersMap));
}

bool writeActionType(Writer& writer, ActionType type) {
    writeEnum(writer, type);
    return true;
}

bool readActionType(Reader& reader, ActionType& type) {
    return readEnum(reader, type, static_cast<std::uint8_t>(ActionType::Contextual));
}

bool writeContextual(Writer& writer, ContextualActionType action) {
    writeEnum(writer, action);
    return true;
}

bool readContextual(Reader& reader, ContextualActionType& action) {
    return readEnum(reader, action, static_cast<std::uint8_t>(ContextualActionType::Escape));
}

bool writeAvailableAction(Writer& writer, const AvailableAction& action) {
    writeActionType(writer, action.type);
    return writeOptional(writer, action.targetCave,
               [&](CaveId value) { writeId(writer, value); return true; }) &&
           writeOptional(writer, action.targetTunnel,
               [&](TunnelId value) { writeId(writer, value); return true; }) &&
           writeOptional(writer, action.targetItem,
               [&](ItemType value) { return writeItem(writer, value); }) &&
           writeOptional(writer, action.contextualAction,
               [&](ContextualActionType value) { return writeContextual(writer, value); });
}

bool readAvailableAction(Reader& reader, AvailableAction& action) {
    return readActionType(reader, action.type) &&
           readOptional(reader, action.targetCave,
               [&](CaveId& value) { return readId(reader, value); }) &&
           readOptional(reader, action.targetTunnel,
               [&](TunnelId& value) { return readId(reader, value); }) &&
           readOptional(reader, action.targetItem,
               [&](ItemType& value) { return readItem(reader, value); }) &&
           readOptional(reader, action.contextualAction,
               [&](ContextualActionType& value) { return readContextual(reader, value); });
}

bool writePlayerAction(Writer& writer, const PlayerAction& action) {
    writeId(writer, action.player);
    writeActionType(writer, action.type);
    return writeOptional(writer, action.targetCave,
               [&](CaveId value) { writeId(writer, value); return true; }) &&
           writeOptional(writer, action.targetItem,
               [&](ItemType value) { return writeItem(writer, value); }) &&
           writeOptional(writer, action.contextualAction,
               [&](ContextualActionType value) { return writeContextual(writer, value); }) &&
           writeOptional(writer, action.targetTunnel,
               [&](TunnelId value) { writeId(writer, value); return true; });
}

bool readPlayerAction(Reader& reader, PlayerAction& action) {
    return readId(reader, action.player) &&
           readActionType(reader, action.type) &&
           readOptional(reader, action.targetCave,
               [&](CaveId& value) { return readId(reader, value); }) &&
           readOptional(reader, action.targetItem,
               [&](ItemType& value) { return readItem(reader, value); }) &&
           readOptional(reader, action.contextualAction,
               [&](ContextualActionType& value) { return readContextual(reader, value); }) &&
           readOptional(reader, action.targetTunnel,
               [&](TunnelId& value) { return readId(reader, value); });
}

bool writeTunnel(Writer& writer, const TunnelView& tunnel) {
    writeId(writer, tunnel.id);
    return writeOptional(writer, tunnel.destination,
               [&](CaveId value) { writeId(writer, value); return true; }) &&
           (writer.boolean(tunnel.strongColdDraft), true);
}

bool readTunnel(Reader& reader, TunnelView& tunnel) {
    return readId(reader, tunnel.id) &&
           readOptional(reader, tunnel.destination,
               [&](CaveId& value) { return readId(reader, value); }) &&
           reader.boolean(tunnel.strongColdDraft);
}

bool writeCave(Writer& writer, const DiscoveredCaveView& cave) {
    writeId(writer, cave.cave);
    return writeVector(writer, cave.exits,
        [&](const TunnelView& tunnel) { return writeTunnel(writer, tunnel); });
}

bool readCave(Reader& reader, DiscoveredCaveView& cave) {
    return readId(reader, cave.cave) &&
           readVector(reader, cave.exits,
               [&](TunnelView& tunnel) { return readTunnel(reader, tunnel); });
}

bool writeMap(Writer& writer, const PlayerMapView& map) {
    writeId(writer, map.currentCave);
    return writeVector(writer, map.caves,
        [&](const DiscoveredCaveView& cave) { return writeCave(writer, cave); });
}

bool readMap(Reader& reader, PlayerMapView& map) {
    return readId(reader, map.currentCave) &&
           readVector(reader, map.caves,
               [&](DiscoveredCaveView& cave) { return readCave(reader, cave); });
}

bool writeObservation(Writer& writer, const PlayerObservation& observation) {
    writeEnum(writer, observation.type);
    writeId(writer, observation.viewer);
    return writeOptional(writer, observation.cave,
               [&](CaveId value) { writeId(writer, value); return true; }) &&
           writeOptional(writer, observation.otherPlayer,
               [&](PlayerId value) { writeId(writer, value); return true; }) &&
           (writer.i32(observation.amount), true) &&
           writeOptional(writer, observation.basiliskBehavior,
               [&](BasiliskBehavior value) { writeEnum(writer, value); return true; }) &&
           writeOptional(writer, observation.itemType,
               [&](ItemType value) { return writeItem(writer, value); }) &&
           writeOptional(writer, observation.tunnel,
               [&](TunnelId value) { writeId(writer, value); return true; });
}

bool readObservation(Reader& reader, PlayerObservation& observation) {
    return readEnum(reader, observation.type,
               static_cast<std::uint8_t>(ObservationType::MatchDrawn)) &&
           readId(reader, observation.viewer) &&
           readOptional(reader, observation.cave,
               [&](CaveId& value) { return readId(reader, value); }) &&
           readOptional(reader, observation.otherPlayer,
               [&](PlayerId& value) { return readId(reader, value); }) &&
           reader.i32(observation.amount) &&
           readOptional(reader, observation.basiliskBehavior,
               [&](BasiliskBehavior& value) {
                   return readEnum(reader, value,
                       static_cast<std::uint8_t>(BasiliskBehavior::Enraged));
               }) &&
           readOptional(reader, observation.itemType,
               [&](ItemType& value) { return readItem(reader, value); }) &&
           readOptional(reader, observation.tunnel,
               [&](TunnelId& value) { return readId(reader, value); });
}

bool writeSnapshot(Writer& writer, const PlayerRoundSnapshot& snapshot) {
    writeId(writer, snapshot.player);
    writeId(writer, snapshot.round);
    writer.i32(snapshot.health);
    writer.i32(snapshot.maxHealth);
    writer.i32(snapshot.arrows);
    writer.i32(snapshot.maxArrows);
    writer.boolean(snapshot.alive);
    if (!writeVector(writer, snapshot.inventory.items,
            [&](ItemType item) { return writeItem(writer, item); })) return false;
    if (!writer.count(snapshot.inventory.capacity)) return false;
    writeId(writer, snapshot.currentCave);
    if (!writeMap(writer, snapshot.map)) return false;
    writer.boolean(snapshot.looseArrowPresent);
    if (!writeVector(writer, snapshot.temporarilyRevealedPitCaves,
            [&](CaveId cave) { writeId(writer, cave); return true; }) ||
        !writeVector(writer, snapshot.availableActions,
            [&](const AvailableAction& action) {
                return writeAvailableAction(writer, action);
            }) ||
        !writeVector(writer, snapshot.observations,
            [&](const PlayerObservation& observation) {
                return writeObservation(writer, observation);
            })) return false;
    writer.boolean(snapshot.recoverableRivalSigilAvailable);
    writer.boolean(snapshot.hasHunterSigil);
    if (!writeOptional(writer, snapshot.extractionCave,
            [&](CaveId cave) { writeId(writer, cave); return true; })) return false;
    writeEnum(writer, snapshot.matchStatus);
    writeEnum(writer, snapshot.matchOutcome);
    return writeOptional(writer, snapshot.winner,
        [&](PlayerId player) { writeId(writer, player); return true; });
}

bool readSnapshot(Reader& reader, PlayerRoundSnapshot& snapshot) {
    std::uint32_t capacity{};
    if (!readId(reader, snapshot.player) ||
        !readId(reader, snapshot.round) ||
        !reader.i32(snapshot.health) ||
        !reader.i32(snapshot.maxHealth) ||
        !reader.i32(snapshot.arrows) ||
        !reader.i32(snapshot.maxArrows) ||
        !reader.boolean(snapshot.alive) ||
        !readVector(reader, snapshot.inventory.items,
            [&](ItemType& item) { return readItem(reader, item); }) ||
        !reader.count(capacity)) return false;
    snapshot.inventory.capacity = capacity;
    if (!readId(reader, snapshot.currentCave) ||
        !readMap(reader, snapshot.map) ||
        !reader.boolean(snapshot.looseArrowPresent) ||
        !readVector(reader, snapshot.temporarilyRevealedPitCaves,
            [&](CaveId& cave) { return readId(reader, cave); }) ||
        !readVector(reader, snapshot.availableActions,
            [&](AvailableAction& action) {
                return readAvailableAction(reader, action);
            }) ||
        !readVector(reader, snapshot.observations,
            [&](PlayerObservation& observation) {
                return readObservation(reader, observation);
            }) ||
        !reader.boolean(snapshot.recoverableRivalSigilAvailable) ||
        !reader.boolean(snapshot.hasHunterSigil) ||
        !readOptional(reader, snapshot.extractionCave,
            [&](CaveId& cave) { return readId(reader, cave); }) ||
        !readEnum(reader, snapshot.matchStatus,
            static_cast<std::uint8_t>(MatchStatus::Completed)) ||
        !readEnum(reader, snapshot.matchOutcome,
            static_cast<std::uint8_t>(MatchOutcome::Draw)) ||
        !readOptional(reader, snapshot.winner,
            [&](PlayerId& player) { return readId(reader, player); })) return false;
    if (snapshot.currentCave != snapshot.map.currentCave)
        return reader.fail("Snapshot current cave does not match map.");
    if (snapshot.health < 0 || snapshot.maxHealth < 0 ||
        snapshot.health > snapshot.maxHealth || snapshot.arrows < 0 ||
        snapshot.maxArrows < 0 || snapshot.arrows > snapshot.maxArrows ||
        snapshot.inventory.items.size() > snapshot.inventory.capacity)
        return reader.fail("Snapshot counters are inconsistent.");
    return true;
}

bool writePoint(Writer& writer, const LogicalPoint& point) {
    return writer.floating(point.x) && writer.floating(point.y);
}

bool readPoint(Reader& reader, LogicalPoint& point) {
    return reader.floating(point.x) && reader.floating(point.y);
}

bool writeBounds(Writer& writer, const LogicalBounds& bounds) {
    return writer.floating(bounds.minimumX) &&
           writer.floating(bounds.minimumY) &&
           writer.floating(bounds.maximumX) &&
           writer.floating(bounds.maximumY) &&
           (writer.boolean(bounds.populated), true);
}

bool readBounds(Reader& reader, LogicalBounds& bounds) {
    if (!reader.floating(bounds.minimumX) ||
        !reader.floating(bounds.minimumY) ||
        !reader.floating(bounds.maximumX) ||
        !reader.floating(bounds.maximumY) ||
        !reader.boolean(bounds.populated)) return false;
    if (bounds.populated &&
        (bounds.minimumX > bounds.maximumX ||
         bounds.minimumY > bounds.maximumY))
        return reader.fail("Map bounds are inverted.");
    return true;
}

template <typename Key, typename WriteKey>
bool writePointMap(
    Writer& writer,
    const std::map<Key, LogicalPoint>& values,
    WriteKey writeKey) {

    if (!writer.count(values.size())) return false;
    for (const auto& [key, point] : values)
        if (!writeKey(key) || !writePoint(writer, point)) return false;
    return true;
}

template <typename Key, typename ReadKey>
bool readPointMap(
    Reader& reader,
    std::map<Key, LogicalPoint>& values,
    ReadKey readKey) {

    std::uint32_t count{};
    if (!reader.count(count)) return false;
    values.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        Key key{};
        LogicalPoint point;
        if (!readKey(key) || !readPoint(reader, point)) return false;
        if (!values.emplace(key, point).second)
            return reader.fail("Duplicate map geometry key.");
    }
    return true;
}

bool writeGeometry(Writer& writer, const PlayerFixedMapGeometry& geometry) {
    if (!writeBounds(writer, geometry.fullBounds)) return false;
    return writePointMap(writer, geometry.discoveredCaves,
               [&](CaveId cave) { writeId(writer, cave); return true; }) &&
           writePointMap(writer, geometry.unknownExitEndpoints,
               [&](const MapExitKey& exit) {
                   writeId(writer, exit.source);
                   writeId(writer, exit.tunnel);
                   return true;
               }) &&
           writePointMap(writer, geometry.temporarilyRevealedCaves,
               [&](CaveId cave) { writeId(writer, cave); return true; });
}

bool readGeometry(Reader& reader, PlayerFixedMapGeometry& geometry) {
    return readBounds(reader, geometry.fullBounds) &&
           readPointMap(reader, geometry.discoveredCaves,
               [&](CaveId& cave) { return readId(reader, cave); }) &&
           readPointMap(reader, geometry.unknownExitEndpoints,
               [&](MapExitKey& exit) {
                   return readId(reader, exit.source) &&
                          readId(reader, exit.tunnel);
               }) &&
           readPointMap(reader, geometry.temporarilyRevealedCaves,
               [&](CaveId& cave) { return readId(reader, cave); });
}

bool validateGeometryForSnapshot(
    Reader& reader,
    const PlayerRoundSnapshot& snapshot,
    const PlayerFixedMapGeometry& geometry) {

    std::set<CaveId> discovered;
    std::set<MapExitKey> unknownExits;
    for (const DiscoveredCaveView& cave : snapshot.map.caves) {
        if (!discovered.insert(cave.cave).second)
            return reader.fail("Duplicate discovered cave.");
        std::set<TunnelId> tunnelIds;
        for (const TunnelView& exit : cave.exits) {
            if (!tunnelIds.insert(exit.id).second)
                return reader.fail("Duplicate cave-local tunnel identifier.");
            if (!exit.destination.has_value())
                unknownExits.insert(MapExitKey{cave.cave, exit.id});
        }
    }
    if (!discovered.contains(snapshot.map.currentCave))
        return reader.fail("Current cave is not discovered.");
    for (const auto& [cave, point] : geometry.discoveredCaves) {
        static_cast<void>(point);
        if (!discovered.contains(cave))
            return reader.fail("Geometry exposes an undiscovered cave.");
    }
    for (const auto& [exit, point] : geometry.unknownExitEndpoints) {
        static_cast<void>(point);
        if (!unknownExits.contains(exit))
            return reader.fail("Geometry exposes an unknown exit not in the map.");
    }
    const std::set<CaveId> temporary(
        snapshot.temporarilyRevealedPitCaves.begin(),
        snapshot.temporarilyRevealedPitCaves.end());
    for (const auto& [cave, point] : geometry.temporarilyRevealedCaves) {
        static_cast<void>(point);
        if (!temporary.contains(cave))
            return reader.fail("Geometry exposes an unreported temporary cave.");
    }
    return true;
}

bool writeMetadata(Writer& writer, const PublicMatchMetadata& metadata) {
    if (!writer.count(metadata.totalCaves)) return false;
    return writeVector(writer, metadata.players,
        [&](const PublicPlayerSlot& player) {
            writeId(writer, player.player);
            writeEnum(writer, player.slot);
            return true;
        });
}

bool readMetadata(Reader& reader, PublicMatchMetadata& metadata) {
    std::uint32_t totalCaves{};
    if (!reader.count(totalCaves)) return false;
    metadata.totalCaves = totalCaves;
    if (!readVector(reader, metadata.players,
            [&](PublicPlayerSlot& player) {
                return readId(reader, player.player) &&
                       readEnum(reader, player.slot,
                           static_cast<std::uint8_t>(PlayerSlot::P2));
            })) return false;
    if (metadata.players.empty())
        return reader.fail("Match metadata requires a player.");
    std::set<PlayerId> players;
    std::set<PlayerSlot> slots;
    for (const PublicPlayerSlot& player : metadata.players) {
        if (!players.insert(player.player).second ||
            !slots.insert(player.slot).second)
            return reader.fail("Match metadata contains duplicate players or slots.");
    }
    return true;
}

bool writeProfile(Writer& writer, const client::PublicPlayerProfile& profile) {
    writeId(writer, profile.player);
    return writer.string(profile.displayName) &&
           writer.string(profile.callingCardId.value) &&
           writer.string(profile.emblemId.value);
}

bool readProfile(Reader& reader, client::PublicPlayerProfile& profile) {
    return readId(reader, profile.player) &&
           reader.string(profile.displayName) &&
           reader.string(profile.callingCardId.value) &&
           reader.string(profile.emblemId.value);
}

bool writeViewContext(Writer& writer, const client::ClientViewContext& context) {
    writeId(writer, context.localPlayer);
    writeId(writer, context.viewedPlayer);
    writeEnum(writer, context.mode);
    return writeOptional(writer, context.spectatablePlayer,
        [&](PlayerId player) { writeId(writer, player); return true; });
}

bool readViewContext(Reader& reader, client::ClientViewContext& context) {
    if (!readId(reader, context.localPlayer) ||
        !readId(reader, context.viewedPlayer) ||
        !readEnum(reader, context.mode,
            static_cast<std::uint8_t>(client::ClientViewMode::Spectating)) ||
        !readOptional(reader, context.spectatablePlayer,
            [&](PlayerId& player) { return readId(reader, player); })) return false;
    if (context.mode == client::ClientViewMode::Playing &&
        context.localPlayer != context.viewedPlayer)
        return reader.fail("Playing context must view the local player.");
    if (context.mode == client::ClientViewMode::Defeated &&
        context.localPlayer != context.viewedPlayer)
        return reader.fail("Defeated context must view the local player.");
    if (context.mode == client::ClientViewMode::Spectating &&
        context.localPlayer == context.viewedPlayer)
        return reader.fail("Spectating context must view another player.");
    return true;
}

void writeHeader(Writer& writer, WireMessageType type, std::uint32_t version) {
    for (std::uint8_t byte : kMagic) writer.u8(byte);
    writeEnum(writer, type);
    writer.u32(version);
}

bool readHeader(
    Reader& reader,
    WireMessageType expected,
    std::uint32_t& version) {

    for (std::uint8_t magic : kMagic) {
        std::uint8_t byte{};
        if (!reader.u8(byte)) return false;
        if (byte != magic) return reader.fail("Invalid wire message magic.");
    }
    std::uint8_t encodedType{};
    if (!reader.u8(encodedType)) return false;
    switch (static_cast<WireMessageType>(encodedType)) {
        case WireMessageType::ServerBootstrap:
        case WireMessageType::ServerUpdate:
        case WireMessageType::SubmitAction:
        case WireMessageType::LockAction:
        case WireMessageType::WatchRemainingHunter:
        case WireMessageType::Quit:
            break;
        default:
            return reader.fail("Unknown wire message type.");
    }
    if (static_cast<WireMessageType>(encodedType) != expected)
        return reader.fail("Unexpected wire message type.");
    if (!reader.u32(version)) return false;
    if (version != kProtocolVersion)
        return reader.fail("Unsupported Basilisk network protocol version.");
    return true;
}

bool finishRead(Reader& reader) {
    if (!reader.ok()) return false;
    if (!reader.done()) return reader.fail("Trailing data after wire message.");
    return true;
}

WireMessageType commandType(const ClientCommandPayload& payload) {
    return std::visit([](const auto& command) {
        using T = std::decay_t<decltype(command)>;
        if constexpr (std::is_same_v<T, SubmitActionCommand>)
            return WireMessageType::SubmitAction;
        if constexpr (std::is_same_v<T, LockActionCommand>)
            return WireMessageType::LockAction;
        if constexpr (std::is_same_v<T, WatchRemainingHunterCommand>)
            return WireMessageType::WatchRemainingHunter;
        return WireMessageType::Quit;
    }, payload);
}

} // namespace

bool encodeWire(
    const ServerBootstrap& message,
    WireBytes& bytes,
    std::string& error) {

    Writer writer(bytes, error);
    if (message.protocolVersion != kProtocolVersion)
        return writer.fail("Unsupported Basilisk network protocol version.");
    writeHeader(writer, WireMessageType::ServerBootstrap, message.protocolVersion);
    if (!writeMetadata(writer, message.matchMetadata) ||
        !writeVector(writer, message.profiles,
            [&](const client::PublicPlayerProfile& profile) {
                return writeProfile(writer, profile);
            }) ||
        !writeViewContext(writer, message.viewContext) ||
        !writeSnapshot(writer, message.initialSnapshot) ||
        !writeGeometry(writer, message.initialMapGeometry)) return false;
    if (!writer.finish()) return false;
    ServerBootstrap validated;
    return decodeServerBootstrap(bytes, validated, error);
}

bool encodeWire(
    const ServerUpdate& message,
    WireBytes& bytes,
    std::string& error) {

    Writer writer(bytes, error);
    if (message.protocolVersion != kProtocolVersion)
        return writer.fail("Unsupported Basilisk network protocol version.");
    writeHeader(writer, WireMessageType::ServerUpdate, message.protocolVersion);
    if (!writeSnapshot(writer, message.snapshot) ||
        !writeGeometry(writer, message.mapGeometry) ||
        !writeOptional(writer, message.viewContext,
            [&](const client::ClientViewContext& context) {
                return writeViewContext(writer, context);
            })) return false;
    if (!writer.finish()) return false;
    ServerUpdate validated;
    return decodeServerUpdate(bytes, validated, error);
}

bool encodeWire(
    const ClientCommand& message,
    WireBytes& bytes,
    std::string& error) {

    Writer writer(bytes, error);
    if (message.protocolVersion != kProtocolVersion)
        return writer.fail("Unsupported Basilisk network protocol version.");
    writeHeader(writer, commandType(message.payload), message.protocolVersion);
    const bool written = std::visit([&](const auto& command) {
        using T = std::decay_t<decltype(command)>;
        if constexpr (std::is_same_v<T, SubmitActionCommand>) {
            return writePlayerAction(writer, command.action);
        } else if constexpr (std::is_same_v<T, LockActionCommand>) {
            writeId(writer, command.player);
            return true;
        } else if constexpr (std::is_same_v<T, WatchRemainingHunterCommand>) {
            writeId(writer, command.localPlayer);
            writeId(writer, command.viewedPlayer);
            return true;
        } else {
            writeId(writer, command.player);
            return true;
        }
    }, message.payload);
    if (!written || !writer.finish()) return false;
    ClientCommand validated;
    return decodeClientCommand(bytes, validated, error);
}

bool decodeServerBootstrap(
    std::span<const std::uint8_t> bytes,
    ServerBootstrap& message,
    std::string& error) {

    Reader reader(bytes, error);
    ServerBootstrap decoded;
    if (!readHeader(reader, WireMessageType::ServerBootstrap,
            decoded.protocolVersion) ||
        !readMetadata(reader, decoded.matchMetadata) ||
        !readVector(reader, decoded.profiles,
            [&](client::PublicPlayerProfile& profile) {
                return readProfile(reader, profile);
            }) ||
        !readViewContext(reader, decoded.viewContext) ||
        !readSnapshot(reader, decoded.initialSnapshot) ||
        !readGeometry(reader, decoded.initialMapGeometry) ||
        !validateGeometryForSnapshot(
            reader, decoded.initialSnapshot, decoded.initialMapGeometry) ||
        !finishRead(reader)) return false;
    const std::set<PlayerId> players = [&] {
        std::set<PlayerId> result;
        for (const PublicPlayerSlot& player : decoded.matchMetadata.players)
            result.insert(player.player);
        return result;
    }();
    if (!players.contains(decoded.viewContext.localPlayer) ||
        !players.contains(decoded.viewContext.viewedPlayer) ||
        !players.contains(decoded.initialSnapshot.player))
        return reader.fail("Bootstrap references a player outside match metadata.");
    std::set<PlayerId> profilePlayers;
    for (const client::PublicPlayerProfile& profile : decoded.profiles) {
        if (!players.contains(profile.player) ||
            !profilePlayers.insert(profile.player).second)
            return reader.fail("Bootstrap contains invalid player profiles.");
    }
    if (profilePlayers != players)
        return reader.fail("Bootstrap requires one profile per player.");
    if (decoded.initialSnapshot.player != decoded.viewContext.viewedPlayer)
        return reader.fail("Bootstrap snapshot does not match viewed player.");
    message = std::move(decoded);
    return true;
}

bool decodeServerUpdate(
    std::span<const std::uint8_t> bytes,
    ServerUpdate& message,
    std::string& error) {

    Reader reader(bytes, error);
    ServerUpdate decoded;
    if (!readHeader(reader, WireMessageType::ServerUpdate,
            decoded.protocolVersion) ||
        !readSnapshot(reader, decoded.snapshot) ||
        !readGeometry(reader, decoded.mapGeometry) ||
        !validateGeometryForSnapshot(
            reader, decoded.snapshot, decoded.mapGeometry) ||
        !readOptional(reader, decoded.viewContext,
            [&](client::ClientViewContext& context) {
                return readViewContext(reader, context);
            }) ||
        !finishRead(reader)) return false;
    message = std::move(decoded);
    return true;
}

bool decodeClientCommand(
    std::span<const std::uint8_t> bytes,
    ClientCommand& message,
    std::string& error) {

    if (bytes.size() < 5) {
        error = "Truncated wire message.";
        return false;
    }
    const WireMessageType type = static_cast<WireMessageType>(bytes[4]);
    if (type != WireMessageType::SubmitAction &&
        type != WireMessageType::LockAction &&
        type != WireMessageType::WatchRemainingHunter &&
        type != WireMessageType::Quit) {
        error = (type == WireMessageType::ServerBootstrap ||
                 type == WireMessageType::ServerUpdate)
            ? "Unexpected wire message type."
            : "Unknown wire message type.";
        return false;
    }

    Reader reader(bytes, error);
    ClientCommand decoded;
    if (!readHeader(reader, type, decoded.protocolVersion)) return false;
    switch (type) {
        case WireMessageType::SubmitAction: {
            SubmitActionCommand command;
            if (!readPlayerAction(reader, command.action)) return false;
            decoded.payload = std::move(command);
            break;
        }
        case WireMessageType::LockAction: {
            LockActionCommand command;
            if (!readId(reader, command.player)) return false;
            decoded.payload = command;
            break;
        }
        case WireMessageType::WatchRemainingHunter: {
            WatchRemainingHunterCommand command;
            if (!readId(reader, command.localPlayer) ||
                !readId(reader, command.viewedPlayer)) return false;
            decoded.payload = command;
            break;
        }
        case WireMessageType::Quit: {
            QuitCommand command;
            if (!readId(reader, command.player)) return false;
            decoded.payload = command;
            break;
        }
        default:
            return reader.fail("Unexpected wire message type.");
    }
    if (!finishRead(reader)) return false;
    message = std::move(decoded);
    return true;
}

} // namespace basilisk::game::network
