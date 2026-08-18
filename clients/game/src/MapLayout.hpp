#pragma once

#include <compare>
#include <limits>
#include <map>
#include <optional>

#include "basilisk/world/DiscoveryState.hpp"

namespace basilisk::game {

struct LogicalPoint {
    double x{0.0};
    double y{0.0};

    bool operator==(const LogicalPoint&) const = default;
};

struct LogicalBounds {
    double minimumX{std::numeric_limits<double>::max()};
    double minimumY{std::numeric_limits<double>::max()};
    double maximumX{std::numeric_limits<double>::lowest()};
    double maximumY{std::numeric_limits<double>::lowest()};
    bool populated{false};

    bool operator==(const LogicalBounds&) const = default;
};

struct MapExitKey {
    CaveId source{};
    TunnelId tunnel{};

    auto operator<=>(const MapExitKey&) const = default;
};

// Player-safe projection of an immutable host-generated physical layout.
// Only discovered CaveIds, exits already present in PlayerMapView, and CaveIds
// separately exposed by a temporary player-safe reveal receive positions;
// fullBounds reveals presentation extent, not hidden topology.
struct PlayerFixedMapGeometry {
    LogicalBounds fullBounds;
    std::map<CaveId, LogicalPoint> discoveredCaves;
    std::map<MapExitKey, LogicalPoint> unknownExitEndpoints;
    // Only CaveIds already disclosed by a temporary player-safe reveal may
    // appear here. They remain separate from permanent map discovery.
    std::map<CaveId, LogicalPoint> temporarilyRevealedCaves;
};

class PlayerMapLayout {
public:
    void update(const PlayerMapView& map);

    // Host-side deterministic finalization for a complete physical layout.
    // The topology is used only while generating fixed presentation geometry.
    void finalizeFullLayout(const PlayerMapView& fullMap);
    void finalizeFullLayout(
        const PlayerMapView& fullMap,
        double targetAspect,
        double maximumHorizontalScale);

    void updateFixed(const PlayerFixedMapGeometry& geometry);

    [[nodiscard]] std::optional<LogicalPoint> cavePosition(CaveId cave) const;
    [[nodiscard]] std::optional<LogicalPoint> exitStubPosition(
        CaveId source,
        TunnelId tunnel) const;
    [[nodiscard]] std::optional<LogicalPoint> temporarilyRevealedCavePosition(
        CaveId cave) const;
    [[nodiscard]] std::optional<LogicalBounds> fixedBounds() const noexcept;
    [[nodiscard]] LogicalBounds positionedBounds() const noexcept;

private:
    [[nodiscard]] bool positionIsFree(LogicalPoint candidate) const;
    [[nodiscard]] LogicalPoint positionAlongExit(
        CaveId source,
        TunnelId tunnel) const;
    [[nodiscard]] LogicalPoint disconnectedPosition() const;

    std::map<CaveId, LogicalPoint> cavePositions_;
    std::map<MapExitKey, LogicalPoint> exitDirections_;
    std::map<MapExitKey, LogicalPoint> fixedExitEndpoints_;
    std::map<CaveId, LogicalPoint> temporarilyRevealedCavePositions_;
    std::optional<LogicalBounds> fixedBounds_;
};

} // namespace basilisk::game
