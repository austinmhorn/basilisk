#include "ClientSessionController.hpp"

#include <algorithm>
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

client::MatchMode ClientSessionController::matchMode() const noexcept { return matchMode_; }
void ClientSessionController::setMatchMode(client::MatchMode mode) noexcept { matchMode_ = mode; }
void ClientSessionController::setParticipantSubtitle(PlayerId player, std::string subtitle) {
    participantSubtitles_.insert_or_assign(player, std::move(subtitle));
}
std::string_view ClientSessionController::participantSubtitle(PlayerId player) const noexcept {
    const auto found = participantSubtitles_.find(player);
    return found == participantSubtitles_.end() ? std::string_view{} : found->second;
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

const std::optional<ActiveClash>& ClientSessionController::activeClash() const noexcept {
    return activeClash_;
}

void ClientSessionController::setActiveClash(std::optional<ActiveClash> clash) {
    activeClash_ = std::move(clash);
}

bool ClientSessionController::submitClashResponse(std::string response) {
    return canSubmitActions() && actionCommands_ != nullptr && activeClash_.has_value() &&
        actionCommands_->submitClashResponse(
            viewContext_.localPlayer, activeClash_->id, std::move(response));
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

bool ClientSessionController::cycleSpectatedPlayer(int direction) {
    if (matchMode_ != client::MatchMode::Sandbox ||
        viewContext_.mode != client::ClientViewMode::Spectating || direction == 0) {
        return false;
    }
    std::vector<PlayerId> survivors;
    for (const PublicPlayerSlot& slot : matchMetadata_.players) {
        const PlayerRoundSnapshot* snapshot = snapshotFor(slot.player);
        if (slot.player != viewContext_.localPlayer && snapshot != nullptr &&
            snapshot->alive && snapshot->matchStatus == MatchStatus::Active) {
            survivors.push_back(slot.player);
        }
    }
    if (survivors.empty()) return false;
    const auto current = std::find(
        survivors.begin(), survivors.end(), viewContext_.viewedPlayer);
    const std::size_t index = current == survivors.end()
        ? 0U : static_cast<std::size_t>(current - survivors.begin());
    const int count = static_cast<int>(survivors.size());
    const int next = (static_cast<int>(index) + (direction < 0 ? -1 : 1) + count) % count;
    viewContext_.viewedPlayer = survivors[static_cast<std::size_t>(next)];
    viewContext_.spectatablePlayer = viewContext_.viewedPlayer;
    return true;
}

bool ClientSessionController::quit() {
    return sessionCommands_ != nullptr &&
        sessionCommands_->quitGame(viewContext_.localPlayer);
}

} // namespace basilisk::game
