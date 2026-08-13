#pragma once

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

    out << "  \"observations\":[],\n";
    out << "  \"actions\":[],\n";
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
