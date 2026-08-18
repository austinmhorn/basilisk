#include <cassert>
#include <iostream>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/systems/TurnResolver.hpp"

using namespace basilisk;

namespace {

MatchState makeMatch(MatchSeed seed) {
    MatchState state;
    state.matchSeed = seed;
    state.mapSeed = 9001;

    state.world.connect(1, 3);
    state.world.connect(2, 3);
    state.world.connect(1, 2);

    state.players = {
        PlayerState{1, 1, 100, 3, true},
        PlayerState{2, 2, 100, 3, true}
    };

    state.basilisk.cave = 3;
    state.basilisk.alive = true;
    return state;
}

std::vector<PlayerAction> simultaneousShots() {
    return {
        PlayerAction{1, ActionType::Shoot, CaveId{3}},
        PlayerAction{2, ActionType::Shoot, CaveId{3}}
    };
}

void simultaneousSuccessfulShotsProduceSpecialDraw() {
    TurnResolver resolver;
    bool found = false;

    for (MatchSeed seed = 1; seed < 10000 && !found; ++seed) {
        auto state = makeMatch(seed);
        const auto events = resolver.resolve(state, simultaneousShots());
        (void)events;

        if (state.result.outcome != MatchOutcome::SimultaneousBasiliskKill) {
            continue;
        }

        found = true;
        assert(state.result.status == MatchStatus::Completed);
        assert(!state.result.winner.has_value());
        assert(!state.basilisk.alive);
        assert(state.basilisk.trueEncounters == 1);
        assert(state.players[0].arrows == 2);
        assert(state.players[1].arrows == 2);
    }

    assert(found);
}

void simultaneousMissesAdvanceOnlyOneEncounter() {
    TurnResolver resolver;
    bool found = false;

    for (MatchSeed seed = 1; seed < 10000 && !found; ++seed) {
        auto state = makeMatch(seed);
        const auto events = resolver.resolve(state, simultaneousShots());
        (void)events;

        if (!state.basilisk.alive || state.basilisk.trueEncounters != 1) {
            continue;
        }

        found = true;
        assert(state.result.status == MatchStatus::Active);
        assert(state.result.outcome == MatchOutcome::None);
        assert(!state.result.winner.has_value());
        assert(state.basilisk.trueEncounters == 1);
        assert(state.players[0].arrows == 2);
        assert(state.players[1].arrows == 2);
    }

    assert(found);
}

void oneSuccessfulShotProducesOneWinner() {
    TurnResolver resolver;
    bool found = false;

    for (MatchSeed seed = 1; seed < 10000 && !found; ++seed) {
        auto state = makeMatch(seed);
        const auto events = resolver.resolve(state, simultaneousShots());
        (void)events;

        if (state.result.outcome != MatchOutcome::BasiliskKilled) {
            continue;
        }

        found = true;
        assert(state.result.status == MatchStatus::Completed);
        assert(state.result.winner.has_value());
        assert(*state.result.winner == 1 || *state.result.winner == 2);
        assert(!state.basilisk.alive);
        assert(state.basilisk.trueEncounters == 1);
    }

    assert(found);
}

} // namespace

int main() {
    simultaneousSuccessfulShotsProduceSpecialDraw();
    simultaneousMissesAdvanceOnlyOneEncounter();
    oneSuccessfulShotProducesOneWinner();

    std::cout << "Simultaneous Basilisk tests passed.\n";
    return 0;
}
