#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "MapLayout.hpp"
#include "PublicLeaderboard.hpp"
#include "basilisk/Action.hpp"
#include "basilisk/Clash.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/PublicMatchMetadata.hpp"
#include "basilisk/client/ClientViewContext.hpp"
#include "basilisk/client/AccountCosmetics.hpp"
#include "basilisk/client/PlayerProfile.hpp"
#include "basilisk/client/SandboxConfiguration.hpp"

namespace basilisk::game::network {

inline constexpr std::uint32_t kProtocolVersion{6};

struct CreateAccountRequest {
    std::string email;
    std::string password;
    std::string username;
};

struct LoginRequest {
    std::string email;
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
    client::AccountCosmeticLoadout cosmeticLoadout;
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

struct CosmeticLoadoutUpdateRequest {
    std::uint32_t protocolVersion{kProtocolVersion};
    std::string sessionToken;
    client::AccountCosmeticLoadout loadout;
};

struct CosmeticLoadoutUpdateSuccess {
    client::AccountCosmeticLoadout loadout;
};
struct CosmeticLoadoutUpdateFailure { std::string message; };
using CosmeticLoadoutUpdateResponsePayload = std::variant<
    CosmeticLoadoutUpdateSuccess, CosmeticLoadoutUpdateFailure>;
struct CosmeticLoadoutUpdateResponse {
    std::uint32_t protocolVersion{kProtocolVersion};
    CosmeticLoadoutUpdateResponsePayload payload;
};

struct HostLobbyRequest {};
struct JoinLobbyRequest { std::string lobbyCode; };
struct CancelHostedLobbyRequest { std::string lobbyCode; };
struct FindMatchRequest {};
struct CancelFindMatchRequest {};
struct HostSandboxLobbyRequest { client::SandboxSessionConfig config; };
struct JoinSandboxLobbyRequest { std::string lobbyCode; };
struct LeaveSandboxLobbyRequest { std::string lobbyCode; };
using LobbyRequestPayload = std::variant<
    HostLobbyRequest, JoinLobbyRequest, CancelHostedLobbyRequest,
    FindMatchRequest, CancelFindMatchRequest, HostSandboxLobbyRequest,
    JoinSandboxLobbyRequest, LeaveSandboxLobbyRequest>;
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
struct SandboxLobbySlotView {
    std::uint8_t slot{};
    PlayerId player{};
    client::SandboxLobbySlotKind kind{client::SandboxLobbySlotKind::Ai};
    bool occupied{};
};
struct SandboxLobbyUpdated {
    std::string lobbyCode;
    client::SandboxSessionConfig config;
    std::vector<SandboxLobbySlotView> slots;
};
struct SandboxLobbyClosed { std::string lobbyCode; };
using LobbyResponsePayload = std::variant<
    LobbyHosted, LobbyMatchAssigned, LobbyCancelled, LobbyFailure,
    MatchmakingQueued, MatchmakingCancelled, SandboxLobbyUpdated,
    SandboxLobbyClosed>;
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

struct ClashStarted {
    std::uint32_t protocolVersion{kProtocolVersion};
    ClashId clash{};
    std::vector<PlayerId> participants;
    std::string challengeWord;
    std::uint64_t remainingMs{};
};

struct ClashResolved {
    std::uint32_t protocolVersion{kProtocolVersion};
    ClashId clash{};
    PlayerId winner{};
    std::vector<PlayerId> losers;
};

struct SubmitClashResponse { ClashId clash{}; std::string response; };

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
    SubmitClashResponse,
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
