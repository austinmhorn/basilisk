#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ActionSelection.hpp"
#include "MapPresentation.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/client/ClientViewContext.hpp"

namespace basilisk::game {

enum class SpatialActionTargetKind {
    Cave,
    UnknownExit
};

struct SpatialActionTarget {
    SpatialActionTargetKind kind{SpatialActionTargetKind::Cave};
    CaveId cave{};
    TunnelId tunnel{};
};

[[nodiscard]] SpatialActionTarget caveActionTarget(CaveId cave) noexcept;
[[nodiscard]] SpatialActionTarget unknownExitActionTarget(TunnelId tunnel) noexcept;

// Returns original snapshot indices. Matching never generates or infers an action.
[[nodiscard]] std::vector<std::size_t> matchingSpatialActionIndices(
    std::span<const AvailableAction> actions,
    SpatialActionTarget target);

[[nodiscard]] std::string spatialActionTitle(const AvailableAction& action);

enum class MapActionMenuChoiceKind {
    GameplayAction,
    MarkDestination,
    ClearDestination
};

struct MapActionMenuChoice {
    MapActionMenuChoiceKind kind{MapActionMenuChoiceKind::GameplayAction};
    std::size_t actionIndex{};

    bool operator==(const MapActionMenuChoice&) const = default;
};

class MapActionMenuState {
public:
    [[nodiscard]] bool open(
        SpatialActionTarget target,
        double anchorX,
        double anchorY,
        std::span<const AvailableAction> actions,
        const client::ClientViewContext& viewContext,
        DestinationControl destinationControl = DestinationControl::None,
        bool allowGameplayActions = true);

    void dismiss() noexcept;
    void setHoveredChoice(std::optional<MapActionMenuChoice> choice) noexcept;

    [[nodiscard]] bool chooseGameplayAction(
        MapActionMenuChoice choice,
        std::span<const AvailableAction> actions,
        const client::ClientViewContext& viewContext,
        ActionSelectionState& selection);

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] const std::optional<SpatialActionTarget>& target() const noexcept;
    [[nodiscard]] const std::vector<MapActionMenuChoice>& choices() const noexcept;
    [[nodiscard]] std::optional<MapActionMenuChoice> hoveredChoice() const noexcept;
    [[nodiscard]] double anchorX() const noexcept;
    [[nodiscard]] double anchorY() const noexcept;

private:
    std::optional<SpatialActionTarget> target_;
    std::vector<MapActionMenuChoice> choices_;
    std::optional<MapActionMenuChoice> hoveredChoice_;
    double anchorX_{0.0};
    double anchorY_{0.0};
};

} // namespace basilisk::game
