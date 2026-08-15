#pragma once

#include <span>
#include <string>
#include <vector>

#include "basilisk/ClientSnapshot.hpp"

namespace basilisk::game {

struct PresentedAction {
    std::string title;
    std::string detail;
};

[[nodiscard]] PresentedAction presentAvailableAction(const AvailableAction& action);
[[nodiscard]] std::vector<PresentedAction> presentAvailableActions(
    std::span<const AvailableAction> actions);

} // namespace basilisk::game
