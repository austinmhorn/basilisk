#pragma once

#include <optional>
#include <span>
#include <string>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/client/ClientViewContext.hpp"
#include "basilisk/client/MatchMode.hpp"
#include "basilisk/client/PlayerProfile.hpp"

namespace basilisk::game {

class ClientSessionController;

enum class LifecycleModalKind {
    FirstDeath,
    FinalDeath,
    HuntEnded
};

struct LifecycleModalPresentation {
    LifecycleModalKind kind{LifecycleModalKind::FinalDeath};
    std::string title;
    std::string detail;
    bool offersWatch{false};
};

class ClientSessionCommandSink {
public:
    virtual ~ClientSessionCommandSink() = default;
    [[nodiscard]] virtual bool watchRemainingHunter(
        PlayerId,
        PlayerId) {
        return true;
    }
    [[nodiscard]] virtual bool quitGame(PlayerId localPlayer) = 0;
};

[[nodiscard]] std::optional<LifecycleModalPresentation>
lifecycleModalPresentation(
    const PlayerRoundSnapshot& viewedSnapshot,
    const client::ClientViewContext& viewContext,
    std::span<const client::PublicPlayerProfile> profiles,
    client::MatchMode matchMode = client::MatchMode::Online);

// The authoritative snapshot carrying RivalDisconnected remains current during
// reconnect grace. A reconnect or timeout publication replaces it immediately.
[[nodiscard]] bool rivalReconnectWaiting(
    const PlayerRoundSnapshot& snapshot) noexcept;

// A client-local view transition only. It preserves local identity and grants
// no authority over the survivor whose player-safe snapshot will be viewed.
[[nodiscard]] bool beginSpectating(client::ClientViewContext& viewContext);

// A gameplay bootstrap is authoritative proof that this authenticated
// connection owns an active match. This deliberately does not depend on the
// menu page or on how authentication was initiated, so a fresh client can
// resume an existing match after session authentication.
[[nodiscard]] bool hasAuthoritativeGameplaySession(
    const ClientSessionController* session) noexcept;

[[nodiscard]] bool shouldAttemptStartupSessionRestore(
    bool developmentLaunch,
    bool fixedTokenLaunch,
    bool hasStoredSessionToken) noexcept;

} // namespace basilisk::game
