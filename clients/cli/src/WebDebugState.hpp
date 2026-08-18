#pragma once

#include <fstream>
#include <string>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/client/Presentation.hpp"

namespace basilisk::cli_debug {

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
            return action.targetItem.has_value()
                ? "Use " + std::string(presentation::itemName(*action.targetItem))
                : "Use item";
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
    out << "  \"recoverableRivalSigilAvailable\":"
        << (snapshot.recoverableRivalSigilAvailable ? "true" : "false") << ",\n";
    out << "  \"hasHunterSigil\":" << (snapshot.hasHunterSigil ? "true" : "false") << ",\n";
    out << "  \"inventoryCapacity\":" << snapshot.inventory.capacity << ",\n";

    out << "  \"inventory\":[";
    for (std::size_t i = 0; i < snapshot.inventory.items.size(); ++i) {
        if (i) out << ',';
        writeQuoted(out, std::string(presentation::itemName(snapshot.inventory.items[i])));
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
        writeQuoted(out, presentation::observationText(snapshot.observations[i]));
    }
    out << "],\n";

    out << "  \"actions\":[";
    for (std::size_t i = 0; i < snapshot.availableActions.size(); ++i) {
        if (i) out << ',';
        const auto& action = snapshot.availableActions[i];
        out << "{\"index\":" << (i + 1) << ",\"type\":";
        writeQuoted(out, actionTypeName(action.type));
        out << ",\"sourceCave\":" << snapshot.currentCave;
        out << ",\"text\":"; writeQuoted(out, actionText(action));
        out << ",\"targetCave\":";
        if (action.targetCave.has_value()) out << *action.targetCave; else out << "null";
        out << ",\"targetTunnel\":";
        if (action.targetTunnel.has_value()) out << *action.targetTunnel; else out << "null";
        out << ",\"item\":";
        if (action.targetItem.has_value()) {
            writeQuoted(out, std::string(presentation::itemName(*action.targetItem)));
        } else {
            out << "null";
        }
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
            out << "{\"sourceCave\":" << cave.cave
                << ",\"tunnel\":" << exit.id << ",\"destination\":";
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
