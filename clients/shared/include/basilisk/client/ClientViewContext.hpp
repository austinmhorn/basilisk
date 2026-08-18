#pragma once

#include <optional>

#include "basilisk/Types.hpp"

namespace basilisk::client {

enum class ClientViewMode {
    Playing,
    Defeated,
    Spectating
};

// Transport-independent presentation/session context. This identifies whose
// player-safe snapshot is displayed without changing the local player's
// identity or granting authority over the viewed player's actions.
// Playing views the local player. Defeated continues to view the local player
// and may offer a surviving hunter. Spectating views that survivor's ordinary
// PlayerRoundSnapshot while preserving the defeated local player's identity.
struct ClientViewContext {
    PlayerId localPlayer{};
    PlayerId viewedPlayer{};
    ClientViewMode mode{ClientViewMode::Playing};
    std::optional<PlayerId> spectatablePlayer;

    // Client-side eligibility only. An authoritative host must still validate
    // every submitted action against the submitting session.
    [[nodiscard]] bool canSubmitActions() const noexcept {
        return mode == ClientViewMode::Playing && localPlayer == viewedPlayer;
    }
};

} // namespace basilisk::client
