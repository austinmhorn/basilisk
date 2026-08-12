#pragma once

#include <vector>

#include "basilisk/Player.hpp"
#include "basilisk/Rules.hpp"
#include "basilisk/Types.hpp"
#include "basilisk/world/WorldGraph.hpp"

namespace basilisk {

struct MatchState {
    MatchSeed matchSeed{};
    MapSeed mapSeed{};
    Rules rules{};
    WorldGraph world;
    std::vector<PlayerState> players;
    RoundNumber round{1};
};

} // namespace basilisk
