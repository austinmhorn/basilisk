#include "SnapshotPresentation.hpp"

#include <string>
#include <utility>

#include "basilisk/client/Presentation.hpp"

namespace basilisk::game {

std::optional<SecondaryObjectivePresentation> secondaryObjectivePresentation(
    const PlayerRoundSnapshot& snapshot) {

    if (snapshot.hasHunterSigil) {
        return SecondaryObjectivePresentation{
            SecondaryObjectiveKind::Secured,
            "HUNTER'S SIGIL",
            "SECURED",
            {"You carry the fallen hunter's Sigil."},
            snapshot.extractionCave.has_value()
                ? std::optional<std::string>{
                      "Extraction at Cave " + std::to_string(*snapshot.extractionCave)}
                : std::optional<std::string>{
                      "Extraction is active \xC2\xB7 location unavailable"},
        };
    }
    if (snapshot.recoverableRivalSigilAvailable) {
        return SecondaryObjectivePresentation{
            SecondaryObjectiveKind::Recoverable,
            "RECOVER HUNTER'S SIGIL",
            "AVAILABLE",
            {
                "The fallen hunter's Sigil can be recovered",
                "somewhere in the caverns.",
            },
            std::nullopt,
        };
    }
    return std::nullopt;
}

std::vector<std::string> roundReportText(const PlayerRoundSnapshot& snapshot) {
    if (snapshot.observations.empty()) return {"No new observations."};

    std::vector<std::string> result;
    result.reserve(snapshot.observations.size());
    for (const PlayerObservation& observation : snapshot.observations) {
        result.push_back(presentation::observationText(observation));
    }
    return result;
}

std::optional<MatchStatePresentation> matchStatePresentation(
    const PlayerRoundSnapshot& snapshot) {

    if (snapshot.matchStatus == MatchStatus::Completed) {
        std::string detail;
        switch (snapshot.matchOutcome) {
            case MatchOutcome::BasiliskKilled:
                detail = snapshot.winner.has_value()
                    ? "Hunter " + std::to_string(*snapshot.winner) +
                          " killed the Basilisk and wins the hunt."
                    : "The Basilisk was killed.";
                break;
            case MatchOutcome::SimultaneousBasiliskKill:
                detail = "Both hunters struck the Basilisk down. The hunt ends in a draw.";
                break;
            case MatchOutcome::EscapedWithSigil:
                detail = snapshot.winner.has_value()
                    ? "Hunter " + std::to_string(*snapshot.winner) +
                          " escaped with the rival Hunter's Sigil and wins the hunt."
                    : "A hunter escaped with the rival Hunter's Sigil.";
                break;
            case MatchOutcome::Draw:
                detail = "No hunter survived. The hunt ends in a draw.";
                break;
            case MatchOutcome::None:
                detail = "The hunt ended without a recorded result.";
                break;
        }
        return MatchStatePresentation{"HUNT ENDED", std::move(detail)};
    }
    if (!snapshot.alive) {
        return MatchStatePresentation{
            "YOU DIED",
            "The hunt continues without your hunter.",
        };
    }
    return std::nullopt;
}

} // namespace basilisk::game
