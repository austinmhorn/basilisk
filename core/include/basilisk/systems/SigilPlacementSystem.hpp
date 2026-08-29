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

// Recovers at most one available rival Sigil at the hunter's current cave.
// The match has one active extraction carrier; competing searches therefore
// resolve deterministically in authoritative action order.
void recoverSigilAtCurrentCave(
    MatchState& state,
    PlayerState& player,
    std::vector<GameEvent>& events);

// Creates the dead hunter's body/Sigil and drops any Sigil they carried.
// All resulting Sigil locations use nearestRecoverableSigilCave().
void placeSigilsForDeath(
    MatchState& state,
    PlayerState& player,
    CaveId intendedCave,
    std::vector<GameEvent>& events);

} // namespace basilisk
