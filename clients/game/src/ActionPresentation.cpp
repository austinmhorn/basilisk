#include "ActionPresentation.hpp"

#include <string>

#include "basilisk/client/Presentation.hpp"

namespace basilisk::game {

PresentedAction presentAvailableAction(const AvailableAction& action) {
    switch (action.type) {
        case ActionType::Move:
            if (action.targetCave.has_value()) {
                return {
                    "Move",
                    "Cave " + std::to_string(*action.targetCave) +
                        " · Known tunnel",
                };
            }
            if (action.targetTunnel.has_value()) {
                return {
                    "Move",
                    "Tunnel " + std::to_string(*action.targetTunnel) +
                        " · Destination unknown",
                };
            }
            return {"Move", "Available movement action"};
        case ActionType::Search:
            return {"Search this cave", "Look for supplies and clues"};
        case ActionType::Shoot:
            if (action.targetCave.has_value()) {
                return {
                    "Fire Arrow",
                    "Cave " + std::to_string(*action.targetCave) +
                        " · Uses 1 arrow",
                };
            }
            return {"Fire Arrow", "Uses 1 arrow"};
        case ActionType::UseItem:
            if (action.targetItem.has_value()) {
                if (*action.targetItem == ItemType::SurveyFragment &&
                    action.targetTunnel.has_value()) {
                    return {
                        "Use Survey Fragment",
                        "Tunnel " + std::to_string(*action.targetTunnel) +
                            " · Destination unknown",
                    };
                }
                return {
                    "Use " + std::string{presentation::itemName(*action.targetItem)},
                    "Inventory item",
                };
            }
            return {"Use item", "Inventory item"};
        case ActionType::Contextual:
            if (action.contextualAction == ContextualActionType::Escape) {
                return {"Escape with Hunter's Sigil", "Contextual action"};
            }
            return {"Contextual action", "Available here"};
    }
    return {"Unknown action", ""};
}

std::vector<PresentedAction> presentAvailableActions(
    std::span<const AvailableAction> actions) {

    std::vector<PresentedAction> rows;
    rows.reserve(actions.size());
    for (const AvailableAction& action : actions) {
        rows.push_back(presentAvailableAction(action));
    }
    return rows;
}

} // namespace basilisk::game
