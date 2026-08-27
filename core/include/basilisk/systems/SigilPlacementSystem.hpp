#pragma once

#include <optional>
#include <vector>

#include "basilisk/Event.hpp"
#include "basilisk/MatchState.hpp"

namespace basilisk {

// Finds the nearest cave where a living hunter can recover a Sigil without
// entering the Basilisk cave. Distance ties prefer the lowest CaveId.
[[nodiscard]] std::optional<CaveId> nearestRecoverableSigilCave(
    const MatchState& state, CaveId intendedCave);

// Creates the dead hunter's body/Sigil and drops any Sigil they carried.
// All resulting Sigil locations use nearestRecoverableSigilCave().
void placeSigilsForDeath(
    MatchState& state,
    PlayerState& player,
    CaveId intendedCave,
    std::vector<GameEvent>& events);

} // namespace basilisk
