#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/Clash.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"

namespace basilisk {

// Host-private orchestration state, including a player's pending action.
// This must not be exposed directly to players.
struct HostSessionState {
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
    [[nodiscard]] ClashSubmissionResult submitClashResponse(
        PlayerId player, ClashId clash, std::string_view response);
    [[nodiscard]] const ActiveClash* activeClash() const noexcept;

    void disconnect(PlayerId player);
    [[nodiscard]] bool reconnect(PlayerId player);
    // An intentional departure eliminates the hunter immediately. Unlike a
    // transport disconnect, it does not start reconnect grace.
    void forfeit(PlayerId player);
    void advanceTime(std::uint64_t elapsedMs);

    [[nodiscard]] const HostSessionState* hostSession(PlayerId player) const;

    // Unfiltered authoritative events for trusted host processing only. These
    // must not be exposed directly to players.
    [[nodiscard]] const std::vector<GameEvent>& authoritativeEvents() const { return lastEvents_; }

private:
    MatchState& state_;
    std::unordered_map<PlayerId, HostSessionState> sessions_;
    std::vector<GameEvent> lastEvents_;
    std::optional<PendingClashRound> pendingClash_;
    ClashId nextClashId_{1};

    [[nodiscard]] bool isLivingPlayer(PlayerId player) const;
    [[nodiscard]] bool allRequiredPlayersLocked() const;
    [[nodiscard]] bool anotherLivingPlayerIsLocked(PlayerId player) const;
    [[nodiscard]] bool tryResolveRound();
    void resolveClash(std::optional<PlayerId> winner);
    void startNextClashOrResolve();
    void completePreparedRound();
    void eliminatePlayer(
        PlayerId player,
        std::optional<GameEventType> reason = std::nullopt);
    void updateTerminalResultIfNeeded();
};

} // namespace basilisk
