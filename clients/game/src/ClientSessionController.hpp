#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ActionCommands.hpp"
#include "ClientLifecycle.hpp"
#include "MapLayout.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/PublicMatchMetadata.hpp"
#include "basilisk/client/ClientViewContext.hpp"
#include "basilisk/client/MatchMode.hpp"
#include "basilisk/client/PlayerProfile.hpp"

namespace basilisk::game {

class ClientSessionController {
public:
    ClientSessionController() = default;
    ClientSessionController(
        PublicMatchMetadata matchMetadata,
        std::vector<client::PublicPlayerProfile> profiles,
        client::ClientViewContext viewContext,
        std::unique_ptr<ActionCommandSink> actionCommands,
        std::unique_ptr<ClientSessionCommandSink> sessionCommands);

    [[nodiscard]] const PublicMatchMetadata& matchMetadata() const noexcept;
    [[nodiscard]] const std::vector<client::PublicPlayerProfile>& profiles()
        const noexcept;
    [[nodiscard]] const client::ClientViewContext& viewContext() const noexcept;
    [[nodiscard]] std::int64_t trophyTotal() const noexcept;
    [[nodiscard]] client::MatchMode matchMode() const noexcept;
    void setMatchMode(client::MatchMode mode) noexcept;
    void setParticipantSubtitle(PlayerId player, std::string subtitle);
    [[nodiscard]] std::string_view participantSubtitle(PlayerId player) const noexcept;

    void setViewContext(client::ClientViewContext viewContext) noexcept;
    void setTrophyTotal(std::int64_t trophyTotal) noexcept;

    // Same-round replacements are accepted; only snapshots older than the
    // newest cached round for that player are rejected.
    [[nodiscard]] bool ingestSnapshot(PlayerRoundSnapshot snapshot);
    [[nodiscard]] bool ingestSnapshot(
        PlayerRoundSnapshot snapshot,
        PlayerFixedMapGeometry geometry);
    [[nodiscard]] const PlayerRoundSnapshot* snapshotFor(PlayerId player) const noexcept;
    [[nodiscard]] const PlayerRoundSnapshot* displayedSnapshot() const noexcept;
    [[nodiscard]] const PlayerFixedMapGeometry* mapGeometryFor(
        PlayerId player) const noexcept;
    [[nodiscard]] const PlayerFixedMapGeometry* displayedMapGeometry() const noexcept;

    [[nodiscard]] bool canSubmitActions() const noexcept;
    [[nodiscard]] bool submitAndLock(const AvailableAction& action);
    [[nodiscard]] const std::optional<ActiveClash>& activeClash() const noexcept;
    void setActiveClash(std::optional<ActiveClash> clash);
    [[nodiscard]] bool submitClashResponse(std::string response);
    [[nodiscard]] bool watchRemainingHunter();
    [[nodiscard]] bool quit();

private:
    PublicMatchMetadata matchMetadata_;
    std::vector<client::PublicPlayerProfile> profiles_;
    client::ClientViewContext viewContext_;
    std::int64_t trophyTotal_{};
    client::MatchMode matchMode_{client::MatchMode::Online};
    std::map<PlayerId, std::string> participantSubtitles_;
    std::map<PlayerId, PlayerRoundSnapshot> snapshots_;
    std::map<PlayerId, PlayerFixedMapGeometry> mapGeometries_;
    std::unique_ptr<ActionCommandSink> actionCommands_;
    std::unique_ptr<ClientSessionCommandSink> sessionCommands_;
    std::optional<ActiveClash> activeClash_;
};

} // namespace basilisk::game
