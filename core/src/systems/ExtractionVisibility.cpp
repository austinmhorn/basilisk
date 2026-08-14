#include "ExtractionVisibility.hpp"

namespace basilisk {

bool isExtractionVisibleTo(const MatchState& state, const PlayerState& player) {
    if (!state.extraction.active || !state.extraction.cave.has_value() ||
        state.extraction.sigilHolder != player.id) {
        return false;
    }

    const CaveId extraction = *state.extraction.cave;
    switch (state.extraction.revealPolicy) {
        case ExtractionRevealPolicy::RevealImmediately:
            return true;
        case ExtractionRevealPolicy::DiscoverThroughExploration:
            return player.discovery.knownCaves.contains(extraction);
        case ExtractionRevealPolicy::ProximityOnly:
            return player.cave == extraction ||
                   state.world.areConnected(player.cave, extraction);
        case ExtractionRevealPolicy::Hidden:
            return false;
    }
    return false;
}

} // namespace basilisk
