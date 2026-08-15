#include "MapActionMenu.hpp"

#include <algorithm>

#include "ActionPresentation.hpp"

namespace basilisk::game {

SpatialActionTarget caveActionTarget(CaveId cave) noexcept {
    SpatialActionTarget target;
    target.kind = SpatialActionTargetKind::Cave;
    target.cave = cave;
    return target;
}

SpatialActionTarget unknownExitActionTarget(TunnelId tunnel) noexcept {
    SpatialActionTarget target;
    target.kind = SpatialActionTargetKind::UnknownExit;
    target.tunnel = tunnel;
    return target;
}

std::vector<std::size_t> matchingSpatialActionIndices(
    std::span<const AvailableAction> actions,
    SpatialActionTarget target) {

    std::vector<std::size_t> matches;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const AvailableAction& action = actions[index];
        const bool matchesTarget = target.kind == SpatialActionTargetKind::Cave
            ? action.targetCave == target.cave
            : action.targetTunnel == target.tunnel;
        if (matchesTarget) matches.push_back(index);
    }
    return matches;
}

std::string spatialActionTitle(const AvailableAction& action) {
    if (action.type == ActionType::Move && action.targetCave.has_value()) {
        return "MOVE TO CAVE " + std::to_string(*action.targetCave);
    }
    if (action.type == ActionType::Move && action.targetTunnel.has_value()) {
        return "ENTER UNKNOWN EXIT";
    }
    if (action.type == ActionType::Shoot && action.targetCave.has_value()) {
        return "SHOOT INTO CAVE " + std::to_string(*action.targetCave);
    }
    return presentAvailableAction(action).title;
}

bool MapActionMenuState::open(
    SpatialActionTarget target,
    double anchorX,
    double anchorY,
    std::span<const AvailableAction> actions,
    const client::ClientViewContext& viewContext,
    DestinationControl destinationControl,
    bool allowGameplayActions) {

    dismiss();
    if (viewContext.canSubmitActions() && allowGameplayActions) {
        for (const std::size_t actionIndex :
             matchingSpatialActionIndices(actions, target)) {
            choices_.push_back(MapActionMenuChoice{
                MapActionMenuChoiceKind::GameplayAction,
                actionIndex,
            });
        }
    }
    if (destinationControl == DestinationControl::Mark) {
        choices_.push_back(MapActionMenuChoice{
            MapActionMenuChoiceKind::MarkDestination, 0});
    } else if (destinationControl == DestinationControl::Clear) {
        choices_.push_back(MapActionMenuChoice{
            MapActionMenuChoiceKind::ClearDestination, 0});
    }
    if (choices_.empty()) return false;

    target_ = target;
    anchorX_ = anchorX;
    anchorY_ = anchorY;
    return true;
}

void MapActionMenuState::dismiss() noexcept {
    target_.reset();
    choices_.clear();
    hoveredChoice_.reset();
}

void MapActionMenuState::setHoveredChoice(
    std::optional<MapActionMenuChoice> choice) noexcept {

    hoveredChoice_ = choice;
}

bool MapActionMenuState::chooseGameplayAction(
    MapActionMenuChoice choice,
    std::span<const AvailableAction> actions,
    const client::ClientViewContext& viewContext,
    ActionSelectionState& selection) {

    if (!isOpen() || choice.kind != MapActionMenuChoiceKind::GameplayAction ||
        std::find(choices_.begin(), choices_.end(), choice) == choices_.end()) {
        return false;
    }
    if (!selection.select(choice.actionIndex, actions, viewContext)) return false;
    dismiss();
    return true;
}

bool MapActionMenuState::isOpen() const noexcept {
    return target_.has_value() && !choices_.empty();
}

const std::optional<SpatialActionTarget>& MapActionMenuState::target() const noexcept {
    return target_;
}

const std::vector<MapActionMenuChoice>& MapActionMenuState::choices() const noexcept {
    return choices_;
}

std::optional<MapActionMenuChoice> MapActionMenuState::hoveredChoice() const noexcept {
    return hoveredChoice_;
}

double MapActionMenuState::anchorX() const noexcept {
    return anchorX_;
}

double MapActionMenuState::anchorY() const noexcept {
    return anchorY_;
}

} // namespace basilisk::game
