#include "basilisk/systems/MatchCoordinator.hpp"

#include <algorithm>

#include "basilisk/Body.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/systems/RoundController.hpp"

namespace basilisk {
namespace {

PlayerState* findPlayer(MatchState& state, PlayerId id) {
    const auto it = std::find_if(state.players.begin(), state.players.end(),
        [id](const PlayerState& player) { return player.id == id; });
    return it == state.players.end() ? nullptr : &*it;
}

bool bodyExists(const MatchState& state, PlayerId owner) {
    return std::any_of(state.bodies.begin(), state.bodies.end(),
        [owner](const BodyState& body) { return body.owner == owner; });
}

} // namespace

MatchCoordinator::MatchCoordinator(MatchState& state) : state_(state) {
    for (const auto& player : state_.players) {
        HostSessionState session;
        session.reserveRemainingMs = state_.rules.multiplayerReserveMs;
        session.disconnectGraceRemainingMs = state_.rules.disconnectGraceMs;
        sessions_.emplace(player.id, session);
    }
}

bool MatchCoordinator::isLivingPlayer(PlayerId player) const {
    const auto it = std::find_if(state_.players.begin(), state_.players.end(),
        [player](const PlayerState& candidate) { return candidate.id == player; });
    return it != state_.players.end() && it->alive;
}

const HostSessionState* MatchCoordinator::hostSession(PlayerId player) const {
    const auto it = sessions_.find(player);
    return it == sessions_.end() ? nullptr : &it->second;
}

bool MatchCoordinator::submitAction(const PlayerAction& action) {
    lastEvents_.clear();
    if (state_.result.status != MatchStatus::Active || !isLivingPlayer(action.player)) return false;

    const auto it = sessions_.find(action.player);
    if (it == sessions_.end() || !it->second.connected || it->second.actionLocked) return false;

    it->second.pendingAction = action;
    return true;
}

bool MatchCoordinator::lockAction(PlayerId player) {
    lastEvents_.clear();
    if (state_.result.status != MatchStatus::Active || !isLivingPlayer(player)) return false;

    const auto it = sessions_.find(player);
    if (it == sessions_.end() || !it->second.connected ||
        it->second.actionLocked || !it->second.pendingAction.has_value()) {
        return false;
    }

    it->second.actionLocked = true;
    tryResolveRound();
    return true;
}

void MatchCoordinator::disconnect(PlayerId player) {
    lastEvents_.clear();
    if (!isLivingPlayer(player)) return;

    const auto it = sessions_.find(player);
    if (it == sessions_.end() || !it->second.connected) return;

    it->second.connected = false;
    it->second.disconnectGraceRemainingMs = state_.rules.disconnectGraceMs;
    lastEvents_.push_back(GameEvent{GameEventType::PlayerDisconnected, player});
}

void MatchCoordinator::reconnect(PlayerId player) {
    lastEvents_.clear();
    if (!isLivingPlayer(player)) return;

    const auto it = sessions_.find(player);
    if (it == sessions_.end() || it->second.connected) return;

    it->second.connected = true;
    it->second.disconnectGraceRemainingMs = state_.rules.disconnectGraceMs;
    lastEvents_.push_back(GameEvent{GameEventType::PlayerReconnected, player});
    tryResolveRound();
}

void MatchCoordinator::forfeit(PlayerId player) {
    lastEvents_.clear();
    if (state_.result.status != MatchStatus::Active || !isLivingPlayer(player))
        return;

    const auto sessionIt = sessions_.find(player);
    if (sessionIt != sessions_.end()) sessionIt->second.connected = false;
    eliminatePlayer(player);
    updateTerminalResultIfNeeded();
    if (state_.result.status == MatchStatus::Active) tryResolveRound();
}

bool MatchCoordinator::anotherLivingPlayerIsLocked(PlayerId player) const {
    for (const auto& candidate : state_.players) {
        if (!candidate.alive || candidate.id == player) continue;
        const auto it = sessions_.find(candidate.id);
        if (it != sessions_.end() && it->second.actionLocked) return true;
    }
    return false;
}

bool MatchCoordinator::allRequiredPlayersLocked() const {
    bool foundLivingPlayer = false;
    for (const auto& player : state_.players) {
        if (!player.alive) continue;
        foundLivingPlayer = true;
        const auto it = sessions_.find(player.id);
        if (it == sessions_.end() || !it->second.actionLocked ||
            !it->second.pendingAction.has_value()) {
            return false;
        }
    }
    return foundLivingPlayer;
}

void MatchCoordinator::eliminatePlayer(
    PlayerId player,
    std::optional<GameEventType> reason) {
    PlayerState* statePlayer = findPlayer(state_, player);
    if (statePlayer == nullptr || !statePlayer->alive) return;

    statePlayer->health = 0;
    statePlayer->alive = false;

    auto sessionIt = sessions_.find(player);
    if (sessionIt != sessions_.end()) {
        sessionIt->second.pendingAction.reset();
        sessionIt->second.actionLocked = false;
    }

    if (reason.has_value()) {
        lastEvents_.push_back(
            GameEvent{*reason, player, player, statePlayer->cave});
    }
    lastEvents_.push_back(GameEvent{GameEventType::PlayerKilled, std::nullopt, player, statePlayer->cave});

    if (!bodyExists(state_, player)) {
        state_.bodies.push_back(BodyState{player, statePlayer->cave, true, statePlayer->cave});
        lastEvents_.push_back(GameEvent{GameEventType::BodyCreated, std::nullopt, player, statePlayer->cave});
    }
}

void MatchCoordinator::updateTerminalResultIfNeeded() {
    if (state_.result.status != MatchStatus::Active) return;

    const auto living = std::count_if(state_.players.begin(), state_.players.end(),
        [](const PlayerState& player) { return player.alive; });

    if (living == 0) {
        state_.result.status = MatchStatus::Completed;
        state_.result.outcome = MatchOutcome::Draw;
        state_.result.winner.reset();
        lastEvents_.push_back(GameEvent{GameEventType::MatchDrawn});
    }
}

void MatchCoordinator::advanceTime(std::uint64_t elapsedMs) {
    lastEvents_.clear();
    if (state_.result.status != MatchStatus::Active || elapsedMs == 0) return;

    std::vector<PlayerId> disconnectExpired;
    std::vector<PlayerId> reserveExpired;

    // Iterate authoritative player order instead of unordered_map order so
    // simultaneous timeout event ordering is reproducible across platforms.
    for (const auto& player : state_.players) {
        const PlayerId playerId = player.id;
        if (!player.alive) continue;
        auto& sessionState = sessions_.at(playerId);

        if (!sessionState.connected) {
            if (elapsedMs >= sessionState.disconnectGraceRemainingMs) {
                sessionState.disconnectGraceRemainingMs = 0;
                disconnectExpired.push_back(playerId);
            } else {
                sessionState.disconnectGraceRemainingMs -= elapsedMs;
            }
            continue;
        }

        if (!sessionState.actionLocked && anotherLivingPlayerIsLocked(playerId)) {
            if (elapsedMs >= sessionState.reserveRemainingMs) {
                sessionState.reserveRemainingMs = 0;
                reserveExpired.push_back(playerId);
            } else {
                sessionState.reserveRemainingMs -= elapsedMs;
            }
        }
    }

    for (const PlayerId player : disconnectExpired) {
        eliminatePlayer(player, GameEventType::PlayerDisconnectTimedOut);
    }
    for (const PlayerId player : reserveExpired) {
        eliminatePlayer(player, GameEventType::PlayerReserveExpired);
    }

    updateTerminalResultIfNeeded();
    if (state_.result.status == MatchStatus::Active) tryResolveRound();
}

void MatchCoordinator::tryResolveRound() {
    if (state_.result.status != MatchStatus::Active || !allRequiredPlayersLocked()) return;

    std::vector<PlayerAction> actions;
    for (const auto& player : state_.players) {
        if (!player.alive) continue;
        auto& sessionState = sessions_.at(player.id);
        actions.push_back(*sessionState.pendingAction);
    }

    RoundController controller;
    const auto roundEvents = controller.resolve(state_, actions);
    lastEvents_.insert(lastEvents_.end(), roundEvents.begin(), roundEvents.end());

    for (auto& [playerId, sessionState] : sessions_) {
        (void)playerId;
        sessionState.pendingAction.reset();
        sessionState.actionLocked = false;
    }
}

} // namespace basilisk
