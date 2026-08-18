#include <cassert>
#include <iostream>

#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/client/ClientViewContext.hpp"

using namespace basilisk;
using namespace basilisk::client;

namespace {

template <typename T>
concept HasViewModeMember = requires(T value) { value.mode; };

template <typename T>
concept HasViewedPlayerMember = requires(T value) { value.viewedPlayer; };

template <typename T>
concept HasSpectatablePlayerMember = requires(T value) { value.spectatablePlayer; };

static_assert(!HasViewModeMember<PlayerRoundSnapshot>);
static_assert(!HasViewedPlayerMember<PlayerRoundSnapshot>);
static_assert(!HasSpectatablePlayerMember<PlayerRoundSnapshot>);
static_assert(!HasViewModeMember<MatchState>);
static_assert(!HasViewedPlayerMember<MatchState>);
static_assert(!HasSpectatablePlayerMember<MatchState>);

void playingViewsTheLocalPlayerAndAllowsLocalSubmission() {
    const ClientViewContext context{
        PlayerId{1},
        PlayerId{1},
        ClientViewMode::Playing,
        std::nullopt};

    assert(context.localPlayer == context.viewedPlayer);
    assert(context.mode == ClientViewMode::Playing);
    assert(!context.spectatablePlayer.has_value());
    assert(context.canSubmitActions());
}

void firstDefeatPreservesLocalViewAndOffersTheSurvivor() {
    const ClientViewContext context{
        PlayerId{1},
        PlayerId{1},
        ClientViewMode::Defeated,
        PlayerId{2}};

    assert(context.localPlayer == PlayerId{1});
    assert(context.viewedPlayer == context.localPlayer);
    assert(context.spectatablePlayer == PlayerId{2});
    assert(!context.canSubmitActions());
}

void spectatingChangesOnlyTheViewedPlayer() {
    const ClientViewContext defeated{
        PlayerId{1},
        PlayerId{1},
        ClientViewMode::Defeated,
        PlayerId{2}};
    const ClientViewContext spectating{
        defeated.localPlayer,
        *defeated.spectatablePlayer,
        ClientViewMode::Spectating,
        defeated.spectatablePlayer};

    assert(spectating.localPlayer == defeated.localPlayer);
    assert(spectating.viewedPlayer == PlayerId{2});
    assert(spectating.mode == ClientViewMode::Spectating);
    assert(!spectating.canSubmitActions());
}

void finalDefeatHasNoSpectatorTarget() {
    const ClientViewContext context{
        PlayerId{2},
        PlayerId{2},
        ClientViewMode::Defeated,
        std::nullopt};

    assert(context.localPlayer == context.viewedPlayer);
    assert(!context.spectatablePlayer.has_value());
    assert(!context.canSubmitActions());
}

void survivorSnapshotDoesNotGrantSpectatorActionAuthority() {
    PlayerRoundSnapshot survivorSnapshot;
    survivorSnapshot.player = PlayerId{2};
    survivorSnapshot.alive = true;
    survivorSnapshot.availableActions.push_back(AvailableAction{ActionType::Search});

    const ClientViewContext context{
        PlayerId{1},
        survivorSnapshot.player,
        ClientViewMode::Spectating,
        survivorSnapshot.player};

    assert(context.viewedPlayer == survivorSnapshot.player);
    assert(!survivorSnapshot.availableActions.empty());
    assert(!context.canSubmitActions());
}

} // namespace

int main() {
    playingViewsTheLocalPlayerAndAllowsLocalSubmission();
    firstDefeatPreservesLocalViewAndOffersTheSurvivor();
    spectatingChangesOnlyTheViewedPlayer();
    finalDefeatHasNoSpectatorTarget();
    survivorSnapshotDoesNotGrantSpectatorActionAuthority();

    std::cout << "Basilisk client view context tests passed.\n";
    return 0;
}
