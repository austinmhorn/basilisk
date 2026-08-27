#include <algorithm>
#include <cassert>
#include <iostream>
#include <type_traits>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/PublicMatchMetadata.hpp"
#include "basilisk/systems/PublicMatchMetadataSystem.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"

using namespace basilisk;

namespace {

template <typename T>
concept HasWorldMember = requires(T value) { value.world; };

template <typename T>
concept HasCavesMember = requires(T value) { value.caves; };

template <typename T>
concept HasTopologyMember = requires(T value) { value.topology; };

template <typename T>
concept HasHazardsMember = requires(T value) { value.hazards; };

template <typename T>
concept HasActorsMember = requires(T value) { value.actors; };

template <typename T>
concept HasGeometryMember = requires(T value) { value.geometry; };

template <typename T>
concept HasEventsMember = requires(T value) { value.events; };

template <typename T>
concept HasActionsMember = requires(T value) { value.actions; };

template <typename T>
concept HasTotalCavesMember = requires(T value) { value.totalCaves; };

template <typename T>
concept HasPlayersMember = requires(T value) { value.players; };

static_assert(std::is_same_v<decltype(PublicMatchMetadata::totalCaves), std::size_t>);
static_assert(std::is_same_v<decltype(PublicMatchMetadata::players),
                             std::vector<PublicPlayerSlot>>);
static_assert(!HasWorldMember<PublicMatchMetadata>);
static_assert(!HasCavesMember<PublicMatchMetadata>);
static_assert(!HasTopologyMember<PublicMatchMetadata>);
static_assert(!HasHazardsMember<PublicMatchMetadata>);
static_assert(!HasActorsMember<PublicMatchMetadata>);
static_assert(!HasGeometryMember<PublicMatchMetadata>);
static_assert(!HasEventsMember<PublicMatchMetadata>);
static_assert(!HasActionsMember<PublicMatchMetadata>);
static_assert(!HasTotalCavesMember<PlayerRoundSnapshot>);
static_assert(!HasPlayersMember<PlayerRoundSnapshot>);

MatchState makeMatch() {
    MatchState state;
    state.rules.mapDiscoveryMode = MapDiscoveryMode::FogOfWar;
    for (CaveId cave = 10; cave <= 50; cave += 10) state.world.addCave(cave);
    state.world.connect(CaveId{10}, CaveId{20});
    state.world.connect(CaveId{20}, CaveId{30});
    state.world.connect(CaveId{30}, CaveId{40});
    state.world.connect(CaveId{40}, CaveId{50});

    PlayerState first;
    first.id = PlayerId{42};
    first.cave = CaveId{10};

    PlayerState second;
    second.id = PlayerId{7};
    second.cave = CaveId{50};

    state.players = {first, second};
    state.basilisk.cave = CaveId{30};
    return state;
}

void exposesScalarCountAndEveryPlayerExactlyOnce() {
    const auto state = makeMatch();
    const auto metadata = PublicMatchMetadataSystem::build(state);

    assert(metadata.totalCaves == 5);
    assert(metadata.players.size() == 2);
    assert(std::count_if(metadata.players.begin(), metadata.players.end(),
               [](const PublicPlayerSlot& entry) { return entry.player == PlayerId{42}; }) == 1);
    assert(std::count_if(metadata.players.begin(), metadata.players.end(),
               [](const PublicPlayerSlot& entry) { return entry.player == PlayerId{7}; }) == 1);
}

void slotsFollowStableAuthoritativeOrderNotNumericIds() {
    auto state = makeMatch();
    const auto firstBuild = PublicMatchMetadataSystem::build(state);

    assert(firstBuild.players[0].player == PlayerId{42});
    assert(firstBuild.players[0].slot == PlayerSlot::P1);
    assert(firstBuild.players[1].player == PlayerId{7});
    assert(firstBuild.players[1].slot == PlayerSlot::P2);

    state.players[0].health = 25;
    state.players[1].arrows = 0;
    const auto laterBuild = PublicMatchMetadataSystem::build(state);
    assert(laterBuild.players[0].player == firstBuild.players[0].player);
    assert(laterBuild.players[0].slot == firstBuild.players[0].slot);
    assert(laterBuild.players[1].player == firstBuild.players[1].player);
    assert(laterBuild.players[1].slot == firstBuild.players[1].slot);
}

void metadataBuildLeavesOrdinarySnapshotUnchanged() {
    const auto state = makeMatch();
    const auto before = SnapshotSystem::buildForPlayer(state, PlayerId{42}, {});
    const auto metadata = PublicMatchMetadataSystem::build(state);
    const auto after = SnapshotSystem::buildForPlayer(state, PlayerId{42}, {});

    assert(metadata.totalCaves == 5);
    assert(before.player == after.player);
    assert(before.round == after.round);
    assert(before.health == after.health);
    assert(before.arrows == after.arrows);
    assert(before.currentCave == after.currentCave);
    assert(before.map.currentCave == after.map.currentCave);
    assert(before.map.caves.size() == after.map.caves.size());
    assert(before.map.caves.size() == 1);
}

void assignsStableSeatsThroughP6() {
    auto state = makeMatch();
    for (PlayerId id = 3; id <= 6; ++id) {
        PlayerState player;
        player.id = 100 + id;
        player.cave = CaveId{10};
        state.players.push_back(player);
    }
    const auto metadata = PublicMatchMetadataSystem::build(state);
    assert(metadata.players.size() == 6);
    assert(metadata.players[0].slot == PlayerSlot::P1);
    assert(metadata.players[1].slot == PlayerSlot::P2);
    assert(metadata.players[2].slot == PlayerSlot::P3);
    assert(metadata.players[3].slot == PlayerSlot::P4);
    assert(metadata.players[4].slot == PlayerSlot::P5);
    assert(metadata.players[5].slot == PlayerSlot::P6);
}

} // namespace

int main() {
    exposesScalarCountAndEveryPlayerExactlyOnce();
    slotsFollowStableAuthoritativeOrderNotNumericIds();
    metadataBuildLeavesOrdinarySnapshotUnchanged();
    assignsStableSeatsThroughP6();

    std::cout << "Basilisk public match metadata tests passed.\n";
    return 0;
}
