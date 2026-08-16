#include "basilisk/client/Presentation.hpp"

#include <algorithm>
#include <string>

namespace basilisk::presentation {

std::string_view itemName(ItemType item) {
    switch (item) {
        case ItemType::HealingDraught: return "Healing Draught";
        case ItemType::OldMinersMap: return "Old Miner's Map";
        case ItemType::SurveyFragment: return "Survey Fragment";
        case ItemType::JackalRepellent: return "Jackal Repellent";
        case ItemType::BloodBait: return "Blood Bait";
        case ItemType::OldHuntersMap: return "Old Hunter's Map";
    }
    return "Unknown Item";
}

std::string observationText(const PlayerObservation& observation) {
    switch (observation.type) {
        case ObservationType::RivalNearby: return "You hear another hunter moving somewhere nearby.";
        case ObservationType::PitNearby: return "A cold draft rises from one of the nearby tunnels.";
        case ObservationType::JackalNearby: return "You hear a jackal prowling nearby.";
        case ObservationType::BasiliskNearby: return "A terrible presence feels close.";
        case ObservationType::BasiliskNearbySubtle: return "The cavern goes strangely quiet.";
        case ObservationType::RestlessBasiliskNoise: return "A heavy scraping sound echoes through distant tunnels.";
        case ObservationType::EnragedLastKnownCave:
            return observation.cave.has_value()
                ? "A monstrous hiss echoes through the tunnels from Cave " +
                    std::to_string(*observation.cave) + "."
                : "The enraged Basilisk left a recent trail.";
        case ObservationType::PitInvestigationSucceeded:
            return observation.tunnel.has_value()
                ? "The cold draft is strongest from Tunnel " + std::to_string(*observation.tunnel) + "."
                : "You identify the direction of the cold draft.";
        case ObservationType::PitInvestigationInconclusive:
            return "The air shifts unpredictably. You can't tell which tunnel the draft is coming from.";
        case ObservationType::ArrowHitYou: return "An arrow strikes you.";
        case ObservationType::YouWereDamaged: return "You take " + std::to_string(observation.amount) + " damage.";
        case ObservationType::YouKilledRival: return "The rival hunter falls.";
        case ObservationType::YouDied: return "You have died.";
        case ObservationType::BasiliskFoundYou: return "The Basilisk found you. You did not survive.";
        case ObservationType::FellIntoPit: return "You stepped into a hidden Pit and fell to your death.";
        case ObservationType::JackalRobbedYou:
            return observation.amount == 1
                ? "A Jackal darts in and steals one of your arrows."
                : "A Jackal darts in and steals " + std::to_string(observation.amount) + " of your arrows.";
        case ObservationType::JackalScaredYou:
            return "A Jackal lunges from the darkness and sends you fleeing into a nearby cave.";
        case ObservationType::JackalKnockedOutYou:
            return "A Jackal overwhelms you, knocks you unconscious, and drags you deeper into the caverns.";
        case ObservationType::JackalRepelled:
            return "Your Jackal Repellent drives the attacking Jackal back.";
        case ObservationType::JackalStunned:
            return "Your arrow strikes the Jackal, stunning it.";
        case ObservationType::RivalDied:
            return "The rival hunter has died somewhere in the caverns. The hunt continues; their Sigil can still be recovered.";
        case ObservationType::RivalDisconnected:
            return "The rival hunter disconnected. Their place in the hunt is being held briefly.";
        case ObservationType::RivalReconnected:
            return "The rival hunter reconnected and has returned to the hunt.";
        case ObservationType::RivalReserveExpired:
            return "The rival hunter exhausted their decision reserve and is out of the hunt.";
        case ObservationType::RivalDisconnectTimedOut:
            return "The rival hunter did not return before the reconnect grace expired and is out of the hunt.";
        case ObservationType::ItemFound:
            return observation.itemType.has_value()
                ? "You found: " + std::string(itemName(*observation.itemType)) + "."
                : "You found an item.";
        case ObservationType::OldHuntersMapFound:
            return "You found: Old Hunter's Map.";
        case ObservationType::OldHuntersMapDistance: {
            const int low = std::max(0, observation.amount - 1);
            const int high = observation.amount + 1;
            return "The markings suggest the Basilisk is roughly " +
                std::to_string(low) + "–" + std::to_string(high) + " caves away.";
        }
        case ObservationType::ArrowFound: return "You found " + std::to_string(observation.amount) + " arrow(s).";
        case ObservationType::ExoticCallingCardFound: return "EXOTIC DISCOVERY: a Calling Card reward has been found.";
        case ObservationType::SigilAcquired: return "You recovered the fallen hunter's Sigil.";
        case ObservationType::ExtractionRevealed:
            return observation.cave.has_value()
                ? "Extraction has activated at Cave " + std::to_string(*observation.cave) + "."
                : "Extraction has activated.";
        case ObservationType::EscapeAvailable: return "You can escape from this cave.";
        case ObservationType::BasiliskEvaded: return "Your arrow found the Basilisk, but it evaded the killing blow.";
        case ObservationType::BasiliskBehaviorChanged: return "Something about the Basilisk's behavior has changed.";
        case ObservationType::BasiliskKilled: return "The Basilisk is dead.";
        case ObservationType::MatchDrawn: return "The hunt ends in a draw.";
    }
    return "Something happened.";
}

} // namespace basilisk::presentation
