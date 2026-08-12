#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"

namespace basilisk {

struct PlayerSessionState {
    bool connected{true};
    bool actionLocked{false};
    std::optional<PlayerAction> pendingAction;
    std::uint64_t reserveRemainingMs{300000};
    std::uint64_t disconnectGraceRemainingMs{30000};
};

// Network-agnostic multiplayer/session orchestration. The future server owns
// transport; this class owns action locks, reserve time, reconnect grace and
// deciding when a simultaneous round is ready for RoundController.
class MatchCoordinator {
public:
    explicit MatchCoordinator(MatchState& state);

    // Actions may be replaced until lockAction() succeeds. A lock is final for
    // that round; once all required living hunters are locked, resolution is
    // immediate.
    [[nodiscard]] bool submitAction(const PlayerAction& action);
    [[nodiscard]] bool lockAction(PlayerId player);

    void disconnect(PlayerId player);
    void reconnect(PlayerId player);
    void advanceTime(std::uint64_t elapsedMs);

    [[nodiscard]] const PlayerSessionState* session(PlayerId player) const;
    [[nodiscard]] const std::vector<GameEvent>& lastEvents() const { return lastEvents_; }

private:
    MatchState& state_;
    std::unordered_map<PlayerId, PlayerSessionState> sessions_;
    std::vector<GameEvent> lastEvents_;

    [[nodiscard]] bool isLivingPlayer(PlayerId player) const;
    [[nodiscard]] bool allRequiredPlayersLocked() const;
    [[nodiscard]] bool anotherLivingPlayerIsLocked(PlayerId player) const;
    void tryResolveRound();
    void eliminateForTimeout(PlayerId player, GameEventType reason);
    void updateTerminalResultIfNeeded();
};

} // namespace basilisk
