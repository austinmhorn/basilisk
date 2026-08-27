#include "basilisk/systems/MatchCoordinator.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <set>

#include "basilisk/Body.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/Random.hpp"
#include "basilisk/systems/MapDiscoverySystem.hpp"
#include "basilisk/systems/RoundController.hpp"
#include "basilisk/systems/SigilPlacementSystem.hpp"

namespace basilisk {
namespace {

PlayerState* findPlayer(MatchState& state, PlayerId id) {
    const auto it = std::find_if(state.players.begin(), state.players.end(),
        [id](const PlayerState& player) { return player.id == id; });
    return it == state.players.end() ? nullptr : &*it;
}

std::string normalizedResponse(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

const PlayerState* findPlayer(const MatchState& state, PlayerId id) {
    const auto it = std::find_if(state.players.begin(), state.players.end(),
        [id](const PlayerState& player) { return player.id == id; });
    return it == state.players.end() ? nullptr : &*it;
}

bool pitCave(const MatchState& state, CaveId cave) {
    return std::any_of(state.pits.begin(), state.pits.end(),
        [cave](const PitState& pit) { return pit.active && pit.cave == cave; });
}

std::optional<CaveId> safeClashRelocation(const MatchState& state, PlayerId loser,
                                           CaveId contested, ClashId clash) {
    std::vector<CaveId> choices;
    const auto* loserState = findPlayer(state, loser);
    for (const CaveId cave : state.world.caveIds()) {
        if (cave == contested || (loserState != nullptr && cave == loserState->cave) ||
            pitCave(state, cave) ||
            (state.basilisk.alive && state.basilisk.cave == cave)) continue;
        const bool occupied = std::any_of(state.players.begin(), state.players.end(),
            [loser, cave](const PlayerState& player) {
                return player.id != loser && player.alive && player.cave == cave;
            });
        if (!occupied) choices.push_back(cave);
    }
    if (choices.empty()) return std::nullopt;
    RandomGenerator rng{static_cast<std::uint64_t>(state.matchSeed) ^
        (static_cast<std::uint64_t>(state.round) << 32U) ^ clash ^ loser};
    return choices[static_cast<std::size_t>(rng.range(0, static_cast<int>(choices.size()) - 1))];
}

std::string challengeWord(const MatchState& state, ClashId clash) {
    static constexpr std::string_view words[] = {
        "fang", "torch", "venom", "cavern", "arrow", "shadow", "sigil", "hunter"
    };
    RandomGenerator rng{static_cast<std::uint64_t>(state.matchSeed) ^
        (static_cast<std::uint64_t>(state.round) << 24U) ^ clash};
    return std::string(words[static_cast<std::size_t>(
        rng.range(0, static_cast<int>(std::size(words)) - 1))]);
}

ClashKind stationaryClashKind(ActionType type) {
    if (type == ActionType::Search) return ClashKind::MoveIntoSearch;
    if (type == ActionType::UseItem) return ClashKind::MoveIntoUseItem;
    return ClashKind::MoveIntoStationary;
}

void assertDistinctLivingHunterCaves(const MatchState& state) {
    if (state.result.status != MatchStatus::Active) return;
    std::set<CaveId> occupied;
    for (const PlayerState& player : state.players) {
        if (!player.alive) continue;
        assert(occupied.insert(player.cave).second &&
            "Resolved round left living hunters in the same cave");
    }
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
    if (pendingClash_ || state_.result.status != MatchStatus::Active || !isLivingPlayer(action.player)) return false;

    const auto it = sessions_.find(action.player);
    if (it == sessions_.end() || !it->second.connected || it->second.actionLocked) return false;

    it->second.pendingAction = action;
    return true;
}

bool MatchCoordinator::lockAction(PlayerId player) {
    lastEvents_.clear();
    if (pendingClash_ || state_.result.status != MatchStatus::Active || !isLivingPlayer(player)) return false;

    const auto it = sessions_.find(player);
    if (it == sessions_.end() || !it->second.connected ||
        it->second.actionLocked || !it->second.pendingAction.has_value()) {
        return false;
    }

    it->second.actionLocked = true;
    if (!tryResolveRound()) {
        it->second.actionLocked = false;
        return false;
    }
    return true;
}

const ActiveClash* MatchCoordinator::activeClash() const noexcept {
    return pendingClash_ ? &pendingClash_->clash : nullptr;
}

ClashSubmissionResult MatchCoordinator::submitClashResponse(
    PlayerId player, ClashId clash, std::string_view response) {
    lastEvents_.clear();
    if (!pendingClash_ || pendingClash_->clash.id != clash ||
        std::find(pendingClash_->clash.participants.begin(),
                  pendingClash_->clash.participants.end(), player) ==
            pendingClash_->clash.participants.end()) return ClashSubmissionResult::Rejected;
    if (normalizedResponse(response) != normalizedResponse(pendingClash_->clash.challengeWord))
        return ClashSubmissionResult::Incorrect;
    resolveClash(player);
    return ClashSubmissionResult::Resolved;
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
    (void)tryResolveRound();
}

void MatchCoordinator::forfeit(PlayerId player) {
    lastEvents_.clear();
    if (state_.result.status != MatchStatus::Active || !isLivingPlayer(player))
        return;

    const auto sessionIt = sessions_.find(player);
    if (sessionIt != sessions_.end()) sessionIt->second.connected = false;
    eliminatePlayer(player);
    updateTerminalResultIfNeeded();
    if (state_.result.status != MatchStatus::Active) pendingClash_.reset();
    else if (pendingClash_) resolveClash(std::nullopt);
    else (void)tryResolveRound();
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

    placeSigilsForDeath(state_, *statePlayer, statePlayer->cave, lastEvents_);
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

        if (!pendingClash_ && !sessionState.actionLocked && anotherLivingPlayerIsLocked(playerId)) {
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
    if (state_.result.status != MatchStatus::Active) {
        pendingClash_.reset();
        return;
    }
    if (pendingClash_) {
        const bool participantEliminated = std::any_of(
            pendingClash_->clash.participants.begin(), pendingClash_->clash.participants.end(),
            [&](PlayerId player) { return !isLivingPlayer(player); });
        if (participantEliminated || elapsedMs >= pendingClash_->clash.remainingMs)
            resolveClash(std::nullopt);
        else pendingClash_->clash.remainingMs -= elapsedMs;
    } else (void)tryResolveRound();
}

bool MatchCoordinator::tryResolveRound() {
    if (pendingClash_ || state_.result.status != MatchStatus::Active || !allRequiredPlayersLocked()) return true;

    std::vector<PlayerAction> actions;
    for (const auto& player : state_.players) {
        if (!player.alive) continue;
        auto& sessionState = sessions_.at(player.id);
        actions.push_back(*sessionState.pendingAction);
    }

    // Clash detection needs authoritative destinations, but the executable
    // action batch must retain its original cave-local tunnel targets so
    // RoundController performs its normal one-time normalization.
    auto clashActions = actions;
    for (auto& action : clashActions) {
        if (action.type != ActionType::Move) continue;
        auto* player = findPlayer(state_, action.player);
        if (!player) continue;
        const auto destination = MapDiscoverySystem::resolveMoveDestination(state_, *player, action);
        action.targetCave = destination;
        action.targetTunnel.reset();
    }

    struct Conflict { ClashKind kind; PlayerId a; PlayerId b; PlayerId mover; PlayerId stationary; CaveId cave; };
    std::vector<Conflict> conflicts;
    for (std::size_t i = 0; i < clashActions.size(); ++i) {
        const auto& a = clashActions[i];
        const auto* pa = findPlayer(state_, a.player);
        if (!pa) continue;
        for (std::size_t j = i + 1; j < clashActions.size(); ++j) {
            const auto& b = clashActions[j];
            const auto* pb = findPlayer(state_, b.player);
            if (!pb) continue;
            const bool aMoves = a.type == ActionType::Move && a.targetCave.has_value();
            const bool bMoves = b.type == ActionType::Move && b.targetCave.has_value();
            if (aMoves && bMoves) {
                if (*a.targetCave == *b.targetCave)
                    conflicts.push_back({ClashKind::MoveToSameCave, a.player, b.player, 0, 0, *a.targetCave});
                else if (pa->cave == *b.targetCave && pb->cave == *a.targetCave)
                    conflicts.push_back({ClashKind::OppositeTraversal, a.player, b.player, 0, 0, *a.targetCave});
            } else if (aMoves && *a.targetCave == pb->cave) {
                conflicts.push_back({stationaryClashKind(b.type),
                    a.player, b.player, a.player, b.player, pb->cave});
            } else if (bMoves && *b.targetCave == pa->cave) {
                conflicts.push_back({stationaryClashKind(a.type),
                    a.player, b.player, b.player, a.player, pa->cave});
            }
        }
    }

    if (!conflicts.empty()) {
        std::set<PlayerId> involved;
        for (const auto& conflict : conflicts) { involved.insert(conflict.a); involved.insert(conflict.b); }
        // Multi-hunter conflict components are deliberately unsupported until
        // their gameplay rule is defined.
        if (involved.size() == 2 && conflicts.size() == 1) {
            const auto& conflict = conflicts.front();
            PendingClashRound pending;
            pending.clash.id = nextClashId_++;
            pending.clash.kind = conflict.kind;
            pending.clash.participants = {conflict.a, conflict.b};
            std::sort(pending.clash.participants.begin(), pending.clash.participants.end());
            pending.clash.challengeWord = challengeWord(state_, pending.clash.id);
            pending.clash.remainingMs = state_.rules.clashTimeoutMs;
            pending.actions = actions;
            for (const auto& action : clashActions) {
                if (action.type == ActionType::Move && action.targetCave.has_value()) {
                    pending.moveDestinations.emplace_back(action.player, *action.targetCave);
                }
            }
            pending.mover = conflict.mover;
            pending.stationary = conflict.stationary;
            pending.contestedCave = conflict.cave;
            if (conflict.stationary != 0) {
                auto actionIt = std::find_if(pending.actions.begin(), pending.actions.end(),
                    [&](const PlayerAction& action) { return action.player == conflict.stationary; });
                if (actionIt != pending.actions.end() &&
                    (actionIt->type == ActionType::Search ||
                     actionIt->type == ActionType::UseItem)) {
                    RoundController controller;
                    pending.completedEvents =
                        controller.resolveStationaryAction(state_, *actionIt);
                    actionIt->type = ActionType::Contextual;
                    actionIt->contextualAction.reset();
                    lastEvents_.insert(lastEvents_.end(),
                        pending.completedEvents.begin(), pending.completedEvents.end());
                }
            }
            pendingClash_ = std::move(pending);
            return true;
        }
        return false;
    }

    RoundController controller;
    const auto roundEvents = controller.resolve(state_, actions);
    lastEvents_.insert(lastEvents_.end(), roundEvents.begin(), roundEvents.end());
    assertDistinctLivingHunterCaves(state_);

    for (auto& [playerId, sessionState] : sessions_) {
        (void)playerId;
        sessionState.pendingAction.reset();
        sessionState.actionLocked = false;
    }
    return true;
}

void MatchCoordinator::resolveClash(std::optional<PlayerId> winner) {
    if (!pendingClash_) return;
    PendingClashRound pending = std::move(*pendingClash_);
    pendingClash_.reset();
    lastEvents_.insert(lastEvents_.end(), pending.completedEvents.begin(), pending.completedEvents.end());

    if (winner.has_value()) {
        const PlayerId loser = pending.clash.participants[0] == *winner
            ? pending.clash.participants[1] : pending.clash.participants[0];
        for (auto& action : pending.actions) {
            if (action.player == loser && action.type == ActionType::Move) {
                action.targetCave.reset();
                action.targetTunnel.reset();
            }
            if (pending.stationary != 0 && *winner == pending.stationary &&
                action.player == pending.mover && action.type == ActionType::Move) {
                action.targetCave.reset();
                action.targetTunnel.reset();
            }
        }
        CaveId winnerCave = pending.contestedCave;
        const auto winnerDestination = std::find_if(
            pending.moveDestinations.begin(), pending.moveDestinations.end(),
            [&](const auto& destination) { return destination.first == *winner; });
        if (winnerDestination != pending.moveDestinations.end()) {
            winnerCave = winnerDestination->second;
        }
        auto* loserState = findPlayer(state_, loser);
        if (loserState && loserState->alive) {
            loserState->health = std::max(0, loserState->health - state_.rules.clashDamage);
            lastEvents_.push_back(GameEvent{GameEventType::PlayerDamaged, *winner, loser,
                loserState->cave, state_.rules.clashDamage});
            if (loserState->health == 0) {
                loserState->alive = false;
                lastEvents_.push_back(GameEvent{GameEventType::PlayerKilled, *winner, loser, loserState->cave});
                placeSigilsForDeath(
                    state_, *loserState, loserState->cave, lastEvents_);
            } else if (const auto cave = safeClashRelocation(state_, loser, winnerCave, pending.clash.id)) {
                loserState->cave = *cave;
                MapDiscoverySystem::discoverCave(*loserState, *cave, lastEvents_);
            }
        }
    } else {
        for (auto& action : pending.actions) {
            if (action.type == ActionType::Move &&
                std::find(pending.clash.participants.begin(), pending.clash.participants.end(),
                          action.player) != pending.clash.participants.end()) {
                action.targetCave.reset();
                action.targetTunnel.reset();
            }
        }
    }

    RoundController controller;
    const auto events = controller.resolve(state_, pending.actions);
    lastEvents_.insert(lastEvents_.end(), events.begin(), events.end());
    assertDistinctLivingHunterCaves(state_);
    for (auto& [id, session] : sessions_) {
        (void)id;
        session.pendingAction.reset();
        session.actionLocked = false;
    }
}

} // namespace basilisk
