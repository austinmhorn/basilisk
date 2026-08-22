#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "MapLayout.hpp"
#include "PublicLeaderboard.hpp"
#include "basilisk/Action.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/PublicMatchMetadata.hpp"
#include "basilisk/client/ClientViewContext.hpp"
#include "basilisk/client/PlayerProfile.hpp"

namespace basilisk::game::network {

inline constexpr std::uint32_t kProtocolVersion{2};

struct CreateAccountRequest {
    std::string login;
    std::string password;
    PublicAccountProfile profile;
};

struct LoginRequest {
    std::string login;
    std::string password;
};

struct AuthenticateSessionRequest { std::string sessionToken; };
struct LogoutRequest { std::string sessionToken; };

using AuthenticationRequestPayload = std::variant<
    CreateAccountRequest,
    LoginRequest,
    AuthenticateSessionRequest,
    LogoutRequest>;

struct AuthenticationRequest {
    std::uint32_t protocolVersion{kProtocolVersion};
    AuthenticationRequestPayload payload;
};

struct AuthenticationSuccess {
    std::string sessionToken;
    PublicAccountProfile profile;
};

struct AuthenticationFailure { std::string message; };
struct LogoutSuccess {};

using AuthenticationResponsePayload = std::variant<
    AuthenticationSuccess,
    AuthenticationFailure,
    LogoutSuccess>;

struct AuthenticationResponse {
    std::uint32_t protocolVersion{kProtocolVersion};
    AuthenticationResponsePayload payload;
};

struct HostLobbyRequest {};
struct JoinLobbyRequest { std::string lobbyCode; };
struct CancelHostedLobbyRequest { std::string lobbyCode; };
struct FindMatchRequest {};
struct CancelFindMatchRequest {};
using LobbyRequestPayload = std::variant<
    HostLobbyRequest, JoinLobbyRequest, CancelHostedLobbyRequest,
    FindMatchRequest, CancelFindMatchRequest>;
struct LobbyRequest {
    std::uint32_t protocolVersion{kProtocolVersion};
    LobbyRequestPayload payload;
};

enum class LobbyAssignmentRole : std::uint8_t { Host = 1, Guest = 2 };
struct LobbyHosted { std::string lobbyCode; };
struct LobbyMatchAssigned {
    std::string lobbyCode;
    LobbyAssignmentRole role{LobbyAssignmentRole::Guest};
};
struct LobbyCancelled { std::string lobbyCode; };
struct LobbyFailure { std::string message; };
struct MatchmakingQueued {};
struct MatchmakingCancelled {};
using LobbyResponsePayload = std::variant<
    LobbyHosted, LobbyMatchAssigned, LobbyCancelled, LobbyFailure,
    MatchmakingQueued, MatchmakingCancelled>;
struct LobbyResponse {
    std::uint32_t protocolVersion{kProtocolVersion};
    LobbyResponsePayload payload;
};

// Initial player-safe state supplied after an online session is established.
struct ServerBootstrap {
    std::uint32_t protocolVersion{kProtocolVersion};
    PublicMatchMetadata matchMetadata;
    std::vector<client::PublicPlayerProfile> profiles;
    client::ClientViewContext viewContext;
    PlayerRoundSnapshot initialSnapshot;
    PlayerFixedMapGeometry initialMapGeometry;
    // Read-only, connection-bound total supplied by the authoritative server.
    std::int64_t trophyTotal{};
};

// A complete player-safe snapshot update. View context is present only when
// the server changes which ordinary player snapshot this client may view.
struct ServerUpdate {
    std::uint32_t protocolVersion{kProtocolVersion};
    PlayerRoundSnapshot snapshot;
    PlayerFixedMapGeometry mapGeometry;
    std::optional<client::ClientViewContext> viewContext;
    // Refreshed from server persistence; clients never calculate or mutate it.
    std::int64_t trophyTotal{};
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

inline constexpr std::uint32_t kMaximumLeaderboardPageSize{100};

struct LeaderboardPageRequest {
    std::uint32_t offset{};
    std::uint32_t limit{};
};

struct LeaderboardPageResponse {
    std::uint32_t protocolVersion{kProtocolVersion};
    std::uint32_t offset{};
    std::vector<PublicTrophyLeaderboardEntry> entries;
};

using ClientCommandPayload = std::variant<
    SubmitActionCommand,
    LockActionCommand,
    WatchRemainingHunterCommand,
    QuitCommand,
    LeaderboardPageRequest>;

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
