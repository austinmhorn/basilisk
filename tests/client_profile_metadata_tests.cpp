#include <cassert>
#include <iostream>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/PublicMatchMetadata.hpp"
#include "basilisk/client/PlayerProfile.hpp"

using namespace basilisk;
using namespace basilisk::client;

namespace {

template <typename T>
concept HasSlotMember = requires(T value) { value.slot; };

template <typename T>
concept HasHealthMember = requires(T value) { value.health; };

template <typename T>
concept HasArrowsMember = requires(T value) { value.arrows; };

template <typename T>
concept HasInventoryMember = requires(T value) { value.inventory; };

template <typename T>
concept HasCurrentCaveMember = requires(T value) { value.currentCave; };

template <typename T>
concept HasActionsMember = requires(T value) { value.availableActions; };

template <typename T>
concept HasObservationsMember = requires(T value) { value.observations; };

template <typename T>
concept HasUsernameMember = requires(T value) { value.username; };

template <typename T>
concept HasCallingCardIdMember = requires(T value) { value.callingCardId; };

template <typename T>
concept HasEmblemIdMember = requires(T value) { value.emblemId; };

static_assert(!HasSlotMember<PublicPlayerProfile>);
static_assert(!HasHealthMember<PublicPlayerProfile>);
static_assert(!HasArrowsMember<PublicPlayerProfile>);
static_assert(!HasInventoryMember<PublicPlayerProfile>);
static_assert(!HasCurrentCaveMember<PublicPlayerProfile>);
static_assert(!HasActionsMember<PublicPlayerProfile>);
static_assert(!HasObservationsMember<PublicPlayerProfile>);

static_assert(!HasUsernameMember<PlayerRoundSnapshot>);
static_assert(!HasCallingCardIdMember<PlayerRoundSnapshot>);
static_assert(!HasEmblemIdMember<PlayerRoundSnapshot>);
static_assert(!HasUsernameMember<MatchState>);
static_assert(!HasCallingCardIdMember<MatchState>);
static_assert(!HasEmblemIdMember<MatchState>);
static_assert(!HasUsernameMember<PublicMatchMetadata>);
static_assert(!HasCallingCardIdMember<PublicMatchMetadata>);
static_assert(!HasEmblemIdMember<PublicMatchMetadata>);

void playersCanUseDifferentPublicProfiles() {
    const PublicPlayerProfile first{
        PlayerId{42},
        "Mara Voss",
        CallingCardId{"crimson-veil"},
        EmblemId{"wayfinder"}};
    const PublicPlayerProfile second{
        PlayerId{7},
        "Elias Thorn",
        CallingCardId{"moonlit-stone"},
        EmblemId{"ward"}};

    assert(first.player != second.player);
    assert(first.username != second.username);
    assert(first.callingCardId != second.callingCardId);
    assert(first.emblemId != second.emblemId);
}

void cosmeticIdsAreIndependentOfPlayerAndSlotIdentity() {
    const CallingCardId sharedCard{"founders-mark"};
    const EmblemId sharedEmblem{"compass"};
    const PublicPlayerProfile p1Profile{
        PlayerId{900}, "First", sharedCard, sharedEmblem};
    const PublicPlayerProfile p2Profile{
        PlayerId{3}, "Second", sharedCard, sharedEmblem};

    const PublicMatchMetadata match{
        40,
        {
            PublicPlayerSlot{p1Profile.player, PlayerSlot::P1},
            PublicPlayerSlot{p2Profile.player, PlayerSlot::P2},
        }};

    assert(p1Profile.callingCardId == p2Profile.callingCardId);
    assert(p1Profile.emblemId == p2Profile.emblemId);
    assert(match.players[0].slot == PlayerSlot::P1);
    assert(match.players[1].slot == PlayerSlot::P2);
    assert(!HasSlotMember<PublicPlayerProfile>);
}

void profileConstructionDoesNotMutateGameplayState() {
    MatchState state;
    PlayerState player;
    player.id = PlayerId{5};
    player.cave = CaveId{12};
    player.health = 65;
    player.arrows = 2;
    state.players.push_back(player);

    const auto originalPlayer = state.players.front();
    const PublicPlayerProfile profile{
        player.id,
        "Ash Rowan",
        CallingCardId{"ember-frame"},
        EmblemId{"lantern"}};

    assert(profile.player == state.players.front().id);
    assert(state.players.front().cave == originalPlayer.cave);
    assert(state.players.front().health == originalPlayer.health);
    assert(state.players.front().arrows == originalPlayer.arrows);
    assert(state.players.front().inventory.items.size() ==
           originalPlayer.inventory.items.size());
}

} // namespace

int main() {
    playersCanUseDifferentPublicProfiles();
    cosmeticIdsAreIndependentOfPlayerAndSlotIdentity();
    profileConstructionDoesNotMutateGameplayState();

    std::cout << "Basilisk client profile metadata tests passed.\n";
    return 0;
}
