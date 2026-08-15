#include "SnapshotPresentation.hpp"

#include <string>

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

} // namespace basilisk::game
