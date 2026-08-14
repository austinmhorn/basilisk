#pragma once

#include <compare>
#include <map>
#include <optional>

#include "basilisk/world/DiscoveryState.hpp"

namespace basilisk::game {

struct LogicalPoint {
    double x{0.0};
    double y{0.0};

    bool operator==(const LogicalPoint&) const = default;
};

class PlayerMapLayout {
public:
    void update(const PlayerMapView& map);

    [[nodiscard]] std::optional<LogicalPoint> cavePosition(CaveId cave) const;
    [[nodiscard]] std::optional<LogicalPoint> exitStubPosition(
        CaveId source,
        TunnelId tunnel) const;

private:
    struct ExitKey {
        CaveId source{};
        TunnelId tunnel{};

        auto operator<=>(const ExitKey&) const = default;
    };

    [[nodiscard]] bool positionIsFree(LogicalPoint candidate) const;
    [[nodiscard]] LogicalPoint positionAlongExit(
        CaveId source,
        TunnelId tunnel) const;
    [[nodiscard]] LogicalPoint disconnectedPosition() const;

    std::map<CaveId, LogicalPoint> cavePositions_;
    std::map<ExitKey, LogicalPoint> exitDirections_;
};

} // namespace basilisk::game
