#include <array>
#include <cassert>
#include <iostream>
#include <string_view>
#include <utility>

#include "basilisk/client/Presentation.hpp"

using namespace basilisk;

namespace {

PlayerObservation observation(ObservationType type) {
    PlayerObservation result;
    result.type = type;
    return result;
}

void allItemNamesAreStable() {
    constexpr std::array expected{
        std::pair{ItemType::HealingDraught, std::string_view{"Healing Draught"}},
        std::pair{ItemType::OldMinersMap, std::string_view{"Old Miner's Map"}},
        std::pair{ItemType::SurveyFragment, std::string_view{"Survey Fragment"}},
        std::pair{ItemType::JackalRepellent, std::string_view{"Jackal Repellent"}},
        std::pair{ItemType::BloodBait, std::string_view{"Blood Bait"}},
        std::pair{ItemType::OldHuntersMap, std::string_view{"Old Hunter's Map"}},
    };

    for (const auto& [item, name] : expected) {
        assert(presentation::itemName(item) == name);
    }
}

void staticObservationTextIsStable() {
    assert(presentation::observationText(observation(ObservationType::RivalNearby)) ==
           "You hear another hunter moving somewhere nearby.");
    assert(presentation::observationText(observation(ObservationType::BasiliskNearbySubtle)) ==
           "The cavern goes strangely quiet.");
    assert(presentation::observationText(observation(ObservationType::PitInvestigationInconclusive)) ==
           "The air shifts unpredictably. You can't tell which tunnel the draft is coming from.");
    assert(presentation::observationText(observation(ObservationType::BasiliskEvaded)) ==
           "Your arrow found the Basilisk, but it evaded the killing blow.");
    assert(presentation::observationText(observation(ObservationType::JackalStunned)) ==
           "Your arrow strikes the Jackal, stunning it.");
    assert(presentation::observationText(observation(ObservationType::MatchDrawn)) ==
           "The hunt ends in a draw.");
}

void payloadObservationTextIsStable() {
    auto enraged = observation(ObservationType::EnragedLastKnownCave);
    enraged.cave = CaveId{17};
    assert(presentation::observationText(enraged) ==
           "A monstrous hiss echoes through the tunnels from Cave 17.");

    auto pit = observation(ObservationType::PitInvestigationSucceeded);
    pit.tunnel = TunnelId{9};
    assert(presentation::observationText(pit) ==
           "The cold draft is strongest from Tunnel 9.");

    auto damage = observation(ObservationType::YouWereDamaged);
    damage.amount = 50;
    assert(presentation::observationText(damage) == "You take 50 damage.");

    auto found = observation(ObservationType::ItemFound);
    found.itemType = ItemType::JackalRepellent;
    assert(presentation::observationText(found) == "You found: Jackal Repellent.");

    auto extraction = observation(ObservationType::ExtractionRevealed);
    extraction.cave = CaveId{23};
    assert(presentation::observationText(extraction) ==
           "Extraction has activated at Cave 23.");

    auto arrows = observation(ObservationType::ArrowFound);
    arrows.amount = 2;
    assert(presentation::observationText(arrows) == "You found 2 arrow(s).");
}

void optionalPayloadFallbacksAreStable() {
    assert(presentation::observationText(observation(ObservationType::EnragedLastKnownCave)) ==
           "The enraged Basilisk left a recent trail.");
    assert(presentation::observationText(observation(ObservationType::PitInvestigationSucceeded)) ==
           "You identify the direction of the cold draft.");
    assert(presentation::observationText(observation(ObservationType::ItemFound)) ==
           "You found an item.");
    assert(presentation::observationText(observation(ObservationType::ExtractionRevealed)) ==
           "Extraction has activated.");
}

void deathMessagesAreStable() {
    assert(presentation::observationText(observation(ObservationType::YouKilledRival)) ==
           "The rival hunter falls.");
    assert(presentation::observationText(observation(ObservationType::YouDied)) ==
           "You have died.");
    assert(presentation::observationText(observation(ObservationType::BasiliskFoundYou)) ==
           "The Basilisk found you. You did not survive.");
    assert(presentation::observationText(observation(ObservationType::FellIntoPit)) ==
           "You stepped into a hidden Pit and fell to your death.");
    assert(presentation::observationText(observation(ObservationType::RivalDied)) ==
           "The rival hunter has died somewhere in the caverns. The hunt continues; their Sigil can still be recovered.");
}

void oldHuntersMapBoundsAreStable() {
    auto distance = observation(ObservationType::OldHuntersMapDistance);

    distance.amount = 0;
    assert(presentation::observationText(distance) ==
           "The markings suggest the Basilisk is roughly 0–1 caves away.");

    distance.amount = 1;
    assert(presentation::observationText(distance) ==
           "The markings suggest the Basilisk is roughly 0–2 caves away.");

    distance.amount = 4;
    assert(presentation::observationText(distance) ==
           "The markings suggest the Basilisk is roughly 3–5 caves away.");
}

void arrowPluralizationIsStable() {
    auto robbed = observation(ObservationType::JackalRobbedYou);

    robbed.amount = 1;
    assert(presentation::observationText(robbed) ==
           "A Jackal darts in and steals one of your arrows.");

    robbed.amount = 3;
    assert(presentation::observationText(robbed) ==
           "A Jackal darts in and steals 3 of your arrows.");
}

} // namespace

int main() {
    allItemNamesAreStable();
    staticObservationTextIsStable();
    payloadObservationTextIsStable();
    optionalPayloadFallbacksAreStable();
    deathMessagesAreStable();
    oldHuntersMapBoundsAreStable();
    arrowPluralizationIsStable();
    std::cout << "client presentation tests passed\n";
}
