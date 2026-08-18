#include "ActionCommands.hpp"

namespace basilisk::game {

PlayerAction makePlayerAction(
    const AvailableAction& available,
    PlayerId localPlayer) {

    PlayerAction action;
    action.player = localPlayer;
    action.type = available.type;
    action.targetCave = available.targetCave;
    action.targetTunnel = available.targetTunnel;
    action.targetItem = available.targetItem;
    action.contextualAction = available.contextualAction;
    return action;
}

} // namespace basilisk::game
