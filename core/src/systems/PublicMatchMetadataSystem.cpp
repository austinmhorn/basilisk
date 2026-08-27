#include "basilisk/systems/PublicMatchMetadataSystem.hpp"

#include <algorithm>
#include <stdexcept>

namespace basilisk {

PublicMatchMetadata PublicMatchMetadataSystem::build(const MatchState& state) {
    if (state.players.size() > 6) {
        throw std::invalid_argument("Public match metadata supports at most six player slots.");
    }

    PublicMatchMetadata metadata;
    metadata.totalCaves = state.world.size();
    metadata.players.reserve(state.players.size());

    for (std::size_t index = 0; index < state.players.size(); ++index) {
        const PlayerId player = state.players[index].id;
        const bool duplicate = std::any_of(
            metadata.players.begin(),
            metadata.players.end(),
            [player](const PublicPlayerSlot& entry) { return entry.player == player; });
        if (duplicate) {
            throw std::invalid_argument("Public match metadata requires unique player IDs.");
        }

        // MatchState player order is authoritative and stable for the lifetime
        // of a match. Slot assignment never depends on numeric PlayerId values.
        const PlayerSlot slot = static_cast<PlayerSlot>(index);
        metadata.players.push_back(PublicPlayerSlot{player, slot});
    }

    return metadata;
}

} // namespace basilisk
