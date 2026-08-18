#pragma once

#include <optional>
#include <span>
#include <string>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/client/ClientViewContext.hpp"
#include "basilisk/client/PlayerProfile.hpp"

namespace basilisk::game {

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
    [[nodiscard]] virtual bool quitGame(PlayerId localPlayer) = 0;
};

[[nodiscard]] std::optional<LifecycleModalPresentation>
lifecycleModalPresentation(
    const PlayerRoundSnapshot& viewedSnapshot,
    const client::ClientViewContext& viewContext,
    std::span<const client::PublicPlayerProfile> profiles);

// A client-local view transition only. It preserves local identity and grants
// no authority over the survivor whose player-safe snapshot will be viewed.
[[nodiscard]] bool beginSpectating(client::ClientViewContext& viewContext);

} // namespace basilisk::game
