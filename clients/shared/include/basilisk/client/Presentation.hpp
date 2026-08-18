#pragma once

#include <string>
#include <string_view>

#include "basilisk/Observation.hpp"
#include "basilisk/items/Item.hpp"

namespace basilisk::presentation {

std::string_view itemName(ItemType item);
std::string observationText(const PlayerObservation& observation);

} // namespace basilisk::presentation
