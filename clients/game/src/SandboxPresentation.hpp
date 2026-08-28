#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "ClientSessionController.hpp"

namespace basilisk::game {

struct SandboxParticipantPresentation {
    PlayerId player{};
    std::string label;
    std::string subtitle;
    std::optional<bool> alive;
    bool local{false};
    bool viewed{false};
};

[[nodiscard]] inline std::vector<SandboxParticipantPresentation>
sandboxParticipantPresentation(const ClientSessionController& session) {
    std::vector<SandboxParticipantPresentation> result;
    if (session.matchMode() != client::MatchMode::Sandbox) return result;
    result.reserve(session.matchMetadata().players.size());
    for (std::size_t index = 0; index < session.matchMetadata().players.size(); ++index) {
        const PublicPlayerSlot& slot = session.matchMetadata().players[index];
        const auto profile = std::find_if(session.profiles().begin(), session.profiles().end(),
            [&](const client::PublicPlayerProfile& candidate) {
                return candidate.player == slot.player;
            });
        const auto* snapshot = session.snapshotFor(slot.player);
        result.push_back(SandboxParticipantPresentation{
            slot.player,
            index == 0 ? "HOST" :
                (profile == session.profiles().end() ?
                    "AI " + std::to_string(index + 1) : profile->username),
            std::string{session.participantSubtitle(slot.player)},
            snapshot == nullptr
                ? std::nullopt
                : std::optional<bool>{snapshot->alive},
            slot.player == session.viewContext().localPlayer,
            slot.player == session.viewContext().viewedPlayer,
        });
    }
    return result;
}

} // namespace basilisk::game
