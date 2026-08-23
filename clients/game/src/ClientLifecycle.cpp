#include "ClientLifecycle.hpp"

#include <algorithm>
#include <string_view>

namespace basilisk::game {
namespace {

std::string_view username(
    PlayerId player,
    std::span<const client::PublicPlayerProfile> profiles) {

    const auto found = std::find_if(
        profiles.begin(), profiles.end(),
        [player](const client::PublicPlayerProfile& profile) {
            return profile.player == player;
        });
    return found == profiles.end() ? std::string_view{"Unknown hunter"}
                                   : std::string_view{found->username};
}

std::string resultText(
    const PlayerRoundSnapshot& snapshot,
    std::span<const client::PublicPlayerProfile> profiles) {

    const std::string winner = snapshot.winner.has_value()
        ? std::string{username(*snapshot.winner, profiles)}
        : std::string{"A hunter"};
    switch (snapshot.matchOutcome) {
        case MatchOutcome::BasiliskKilled:
            return snapshot.winner.has_value()
                ? winner + " killed the Basilisk and wins the hunt."
                : "The Basilisk was killed.";
        case MatchOutcome::SimultaneousBasiliskKill:
            return "Both hunters struck the Basilisk down. The hunt ends in a draw.";
        case MatchOutcome::EscapedWithSigil:
            return snapshot.winner.has_value()
                ? winner + " escaped with the rival Hunter's Sigil and wins the hunt."
                : "A hunter escaped with the rival Hunter's Sigil.";
        case MatchOutcome::Draw:
            return "No hunter survived. The hunt ends in a draw.";
        case MatchOutcome::None:
            return "The hunt ended without a recorded result.";
    }
    return "The hunt has ended.";
}

} // namespace

std::optional<LifecycleModalPresentation> lifecycleModalPresentation(
    const PlayerRoundSnapshot& viewedSnapshot,
    const client::ClientViewContext& viewContext,
    std::span<const client::PublicPlayerProfile> profiles) {

    if (viewContext.mode == client::ClientViewMode::Defeated &&
        !viewedSnapshot.alive) {
        const bool canWatch = viewContext.spectatablePlayer.has_value() &&
            viewedSnapshot.matchStatus == MatchStatus::Active;
        return LifecycleModalPresentation{
            canWatch ? LifecycleModalKind::FirstDeath
                     : LifecycleModalKind::FinalDeath,
            "YOU DIED",
            canWatch ? "The remaining hunter continues the hunt."
                     : "No hunter remains to watch.",
            canWatch,
        };
    }
    if (viewedSnapshot.matchStatus == MatchStatus::Completed) {
        return LifecycleModalPresentation{
            LifecycleModalKind::HuntEnded,
            "HUNT ENDED",
            resultText(viewedSnapshot, profiles),
            false,
        };
    }
    return std::nullopt;
}

bool rivalReconnectWaiting(const PlayerRoundSnapshot& snapshot) noexcept {
    bool waiting = false;
    for (const PlayerObservation& observation : snapshot.observations) {
        switch (observation.type) {
            case ObservationType::RivalDisconnected:
                waiting = true;
                break;
            case ObservationType::RivalReconnected:
            case ObservationType::RivalDisconnectTimedOut:
            case ObservationType::RivalReserveExpired:
            case ObservationType::RivalDied:
                waiting = false;
                break;
            default:
                break;
        }
    }
    return waiting;
}

bool beginSpectating(client::ClientViewContext& viewContext) {
    if (viewContext.mode != client::ClientViewMode::Defeated ||
        !viewContext.spectatablePlayer.has_value()) {
        return false;
    }
    viewContext.viewedPlayer = *viewContext.spectatablePlayer;
    viewContext.mode = client::ClientViewMode::Spectating;
    return true;
}

} // namespace basilisk::game
