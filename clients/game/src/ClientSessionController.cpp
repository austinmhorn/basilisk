#include "ClientSessionController.hpp"

#include <utility>

namespace basilisk::game {

ClientSessionController::ClientSessionController(
    PublicMatchMetadata matchMetadata,
    std::vector<client::PublicPlayerProfile> profiles,
    client::ClientViewContext viewContext,
    std::unique_ptr<ActionCommandSink> actionCommands,
    std::unique_ptr<ClientSessionCommandSink> sessionCommands)
    : matchMetadata_(std::move(matchMetadata)),
      profiles_(std::move(profiles)),
      viewContext_(viewContext),
      actionCommands_(std::move(actionCommands)),
      sessionCommands_(std::move(sessionCommands)) {}

const PublicMatchMetadata& ClientSessionController::matchMetadata() const noexcept {
    return matchMetadata_;
}

const std::vector<client::PublicPlayerProfile>&
ClientSessionController::profiles() const noexcept {
    return profiles_;
}

const client::ClientViewContext& ClientSessionController::viewContext() const noexcept {
    return viewContext_;
}

std::int64_t ClientSessionController::trophyTotal() const noexcept {
    return trophyTotal_;
}

void ClientSessionController::setViewContext(
    client::ClientViewContext viewContext) noexcept {
    viewContext_ = viewContext;
}

void ClientSessionController::setTrophyTotal(std::int64_t trophyTotal) noexcept {
    trophyTotal_ = trophyTotal;
}

bool ClientSessionController::ingestSnapshot(PlayerRoundSnapshot snapshot) {
    const auto found = snapshots_.find(snapshot.player);
    if (found != snapshots_.end() && snapshot.round < found->second.round) {
        return false;
    }
    snapshots_.insert_or_assign(snapshot.player, std::move(snapshot));
    return true;
}

bool ClientSessionController::ingestSnapshot(
    PlayerRoundSnapshot snapshot,
    PlayerFixedMapGeometry geometry) {

    const PlayerId player = snapshot.player;
    if (!ingestSnapshot(std::move(snapshot))) return false;
    mapGeometries_.insert_or_assign(player, std::move(geometry));
    return true;
}

const PlayerRoundSnapshot* ClientSessionController::snapshotFor(
    PlayerId player) const noexcept {
    const auto found = snapshots_.find(player);
    return found == snapshots_.end() ? nullptr : &found->second;
}

const PlayerRoundSnapshot* ClientSessionController::displayedSnapshot() const noexcept {
    const PlayerId player = viewContext_.mode == client::ClientViewMode::Spectating
        ? viewContext_.viewedPlayer
        : viewContext_.localPlayer;
    return snapshotFor(player);
}

const PlayerFixedMapGeometry* ClientSessionController::mapGeometryFor(
    PlayerId player) const noexcept {

    const auto found = mapGeometries_.find(player);
    return found == mapGeometries_.end() ? nullptr : &found->second;
}

const PlayerFixedMapGeometry*
ClientSessionController::displayedMapGeometry() const noexcept {
    const PlayerRoundSnapshot* snapshot = displayedSnapshot();
    return snapshot == nullptr ? nullptr : mapGeometryFor(snapshot->player);
}

bool ClientSessionController::canSubmitActions() const noexcept {
    const PlayerRoundSnapshot* snapshot = displayedSnapshot();
    return viewContext_.canSubmitActions() && snapshot != nullptr &&
        snapshot->player == viewContext_.localPlayer;
}

bool ClientSessionController::submitAndLock(const AvailableAction& action) {
    if (!canSubmitActions() || actionCommands_ == nullptr) return false;
    const PlayerAction playerAction = makePlayerAction(action, viewContext_.localPlayer);
    return actionCommands_->submitAction(playerAction) &&
        actionCommands_->lockAction(viewContext_.localPlayer);
}

bool ClientSessionController::watchRemainingHunter() {
    if (viewContext_.mode != client::ClientViewMode::Defeated ||
        !viewContext_.spectatablePlayer.has_value()) {
        return false;
    }
    if (sessionCommands_ != nullptr &&
        !sessionCommands_->watchRemainingHunter(
            viewContext_.localPlayer,
            *viewContext_.spectatablePlayer)) {
        return false;
    }
    return beginSpectating(viewContext_);
}

bool ClientSessionController::quit() {
    return sessionCommands_ != nullptr &&
        sessionCommands_->quitGame(viewContext_.localPlayer);
}

} // namespace basilisk::game
