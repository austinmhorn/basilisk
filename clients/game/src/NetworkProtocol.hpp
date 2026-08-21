#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "MapLayout.hpp"
#include "basilisk/Action.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/PublicMatchMetadata.hpp"
#include "basilisk/client/ClientViewContext.hpp"
#include "basilisk/client/PlayerProfile.hpp"

namespace basilisk::game::network {

inline constexpr std::uint32_t kProtocolVersion{1};

// Initial player-safe state supplied after an online session is established.
struct ServerBootstrap {
    std::uint32_t protocolVersion{kProtocolVersion};
    PublicMatchMetadata matchMetadata;
    std::vector<client::PublicPlayerProfile> profiles;
    client::ClientViewContext viewContext;
    PlayerRoundSnapshot initialSnapshot;
    PlayerFixedMapGeometry initialMapGeometry;
};

// A complete player-safe snapshot update. View context is present only when
// the server changes which ordinary player snapshot this client may view.
struct ServerUpdate {
    std::uint32_t protocolVersion{kProtocolVersion};
    PlayerRoundSnapshot snapshot;
    PlayerFixedMapGeometry mapGeometry;
    std::optional<client::ClientViewContext> viewContext;
};

struct SubmitActionCommand {
    PlayerAction action;
};

struct LockActionCommand {
    PlayerId player{};
};

struct WatchRemainingHunterCommand {
    PlayerId localPlayer{};
    PlayerId viewedPlayer{};
};

struct QuitCommand {
    PlayerId player{};
};

using ClientCommandPayload = std::variant<
    SubmitActionCommand,
    LockActionCommand,
    WatchRemainingHunterCommand,
    QuitCommand>;

struct ClientCommand {
    std::uint32_t protocolVersion{kProtocolVersion};
    ClientCommandPayload payload;
};

// Transport-only seam. A future byte transport owns framing and delivery;
// this interface sees typed protocol messages and no authoritative state.
class ClientTransport {
public:
    virtual ~ClientTransport() = default;
    [[nodiscard]] virtual bool send(const ClientCommand& command) = 0;
};

} // namespace basilisk::game::network
