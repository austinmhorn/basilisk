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

void ClientSessionController::setViewContext(
    client::ClientViewContext viewContext) noexcept {
    viewContext_ = viewContext;
}

bool ClientSessionController::ingestSnapshot(PlayerRoundSnapshot snapshot) {
    const auto found = snapshots_.find(snapshot.player);
    if (found != snapshots_.end() && snapshot.round < found->second.round) {
        return false;
    }
    snapshots_.insert_or_assign(snapshot.player, std::move(snapshot));
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
    return beginSpectating(viewContext_);
}

bool ClientSessionController::quit() {
    return sessionCommands_ != nullptr &&
        sessionCommands_->quitGame(viewContext_.localPlayer);
}

} // namespace basilisk::game
