#pragma once

#include <algorithm>
#include <fstream>
#include <string>

#include "basilisk/ClientSnapshot.hpp"

namespace basilisk::cli_debug {

inline const char* itemName(ItemType item) {
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

inline const char* actionTypeName(ActionType type) {
    switch (type) {
        case ActionType::Move: return "move";
        case ActionType::Search: return "search";
        case ActionType::Shoot: return "shoot";
        case ActionType::UseItem: return "use-item";
        case ActionType::Contextual: return "contextual";
    }
    return "unknown";
}

inline std::string observationText(const PlayerObservation& observation) {
    switch (observation.type) {
        case ObservationType::RivalNearby: return "You hear another hunter moving somewhere nearby.";
        case ObservationType::PitNearby: return "A cold draft rises from one of the nearby tunnels.";
        case ObservationType::JackalNearby: return "You hear a jackal prowling nearby.";
        case ObservationType::BasiliskNearby: return "A terrible presence feels close.";
        case ObservationType::BasiliskNearbySubtle: return "The cavern goes strangely quiet.";
        case ObservationType::RestlessBasiliskNoise: return "A heavy scraping sound echoes through distant tunnels.";
        case ObservationType::EnragedLastKnownCave:
            return observation.cave.has_value()
                ? "The enraged Basilisk was last seen in Cave " + std::to_string(*observation.cave) + "."
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
        case ObservationType::ArrowFound:
            return "You found " + std::to_string(observation.amount) + " arrow(s).";
        case ObservationType::ExoticCallingCardFound:
            return "EXOTIC DISCOVERY: a Calling Card reward has been found.";
        case ObservationType::SigilAcquired:
            return "You recovered the fallen hunter's Sigil.";
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

inline std::string actionText(const AvailableAction& action) {
    auto targetText = [&]() {
        if (action.targetCave.has_value()) return std::string{"Cave "} + std::to_string(*action.targetCave);
        if (action.targetTunnel.has_value()) return std::string{"Tunnel "} + std::to_string(*action.targetTunnel) + " (unknown destination)";
        return std::string{};
    };

    switch (action.type) {
        case ActionType::Move: return "Move through " + targetText();
        case ActionType::Search: return "Search this cave";
        case ActionType::Shoot: return "Fire an arrow toward " + targetText();
        case ActionType::UseItem:
            return action.targetItem.has_value() ? "Use " + std::string(itemName(*action.targetItem)) : "Use item";
        case ActionType::Contextual:
            if (action.contextualAction == ContextualActionType::Escape) return "Escape with the Hunter's Sigil";
            return "Contextual action";
    }
    return "Unknown action";
}

inline void writeQuoted(std::ostream& out, const std::string& value) {
    out << '"';
    for (const char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    out << '"';
}

inline void writeWebDebugState(const PlayerRoundSnapshot& snapshot,
                               const std::string& path = "basilisk_debug_state.json") {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return;

    out << "{\n";
    out << "  \"player\":" << snapshot.player << ",\n";
    out << "  \"round\":" << snapshot.round << ",\n";
    out << "  \"health\":" << snapshot.health << ",\n";
    out << "  \"maxHealth\":" << snapshot.maxHealth << ",\n";
    out << "  \"arrows\":" << snapshot.arrows << ",\n";
    out << "  \"maxArrows\":" << snapshot.maxArrows << ",\n";
    out << "  \"alive\":" << (snapshot.alive ? "true" : "false") << ",\n";
    out << "  \"currentCave\":" << snapshot.currentCave << ",\n";
    out << "  \"looseArrowPresent\":" << (snapshot.looseArrowPresent ? "true" : "false") << ",\n";
    out << "  \"hasHunterSigil\":" << (snapshot.hasHunterSigil ? "true" : "false") << ",\n";
    out << "  \"inventoryCapacity\":" << snapshot.inventory.capacity << ",\n";

    out << "  \"inventory\":[";
    for (std::size_t i = 0; i < snapshot.inventory.items.size(); ++i) {
        if (i) out << ',';
        writeQuoted(out, itemName(snapshot.inventory.items[i]));
    }
    out << "],\n";

    out << "  \"temporaryPitCaves\":[";
    for (std::size_t i = 0; i < snapshot.temporarilyRevealedPitCaves.size(); ++i) {
        if (i) out << ',';
        out << snapshot.temporarilyRevealedPitCaves[i];
    }
    out << "],\n";

    out << "  \"observations\":[";
    for (std::size_t i = 0; i < snapshot.observations.size(); ++i) {
        if (i) out << ',';
        writeQuoted(out, observationText(snapshot.observations[i]));
    }
    out << "],\n";

    out << "  \"actions\":[";
    for (std::size_t i = 0; i < snapshot.availableActions.size(); ++i) {
        if (i) out << ',';
        const auto& action = snapshot.availableActions[i];
        out << "{\"index\":" << (i + 1) << ",\"type\":";
        writeQuoted(out, actionTypeName(action.type));
        out << ",\"text\":"; writeQuoted(out, actionText(action));
        out << ",\"targetCave\":";
        if (action.targetCave.has_value()) out << *action.targetCave; else out << "null";
        out << ",\"targetTunnel\":";
        if (action.targetTunnel.has_value()) out << *action.targetTunnel; else out << "null";
        out << ",\"item\":";
        if (action.targetItem.has_value()) writeQuoted(out, itemName(*action.targetItem)); else out << "null";
        out << '}';
    }
    out << "],\n";

    out << "  \"caves\":[\n";
    for (std::size_t i = 0; i < snapshot.map.caves.size(); ++i) {
        const auto& cave = snapshot.map.caves[i];
        out << "    {\"id\":" << cave.cave << ",\"exits\":[";
        for (std::size_t j = 0; j < cave.exits.size(); ++j) {
            const auto& exit = cave.exits[j];
            if (j) out << ',';
            out << "{\"tunnel\":" << exit.id << ",\"destination\":";
            if (exit.destination.has_value()) out << *exit.destination;
            else out << "null";
            out << ",\"strongColdDraft\":" << (exit.strongColdDraft ? "true" : "false") << '}';
        }
        out << "]}";
        if (i + 1 < snapshot.map.caves.size()) out << ',';
        out << '\n';
    }
    out << "  ]\n";
    out << "}\n";
}

} // namespace basilisk::cli_debug
