#include <cassert>
#include <iostream>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/systems/TurnResolver.hpp"

using namespace basilisk;

namespace {

bool hasEvent(const std::vector<GameEvent>& events, GameEventType type) {
    for (const auto& event : events) {
        if (event.type == type) return true;
    }
    return false;
}

MatchState makeObjectiveMatch() {
    MatchState state;
    state.matchSeed = 5555;
    state.mapSeed = 7777;
    state.basilisk.cave = 6;

    for (CaveId cave = 1; cave <= 6; ++cave) state.world.addCave(cave);
    state.world.connect(1, 2);
    state.world.connect(2, 3);
    state.world.connect(3, 4);
    state.world.connect(4, 5);
    state.world.connect(5, 6);

    state.players = {
        PlayerState{1, 1, 100, 5, true},
        PlayerState{2, 2, 20, 5, true}
    };
    return state;
}

void deathCreatesBodyAndSigil() {
    auto state = makeObjectiveMatch();
    TurnResolver resolver;

    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Shoot, CaveId{2}}
    });

    assert(!state.players[1].alive);
    assert(state.bodies.size() == 1);
    assert(state.bodies[0].owner == 2);
    assert(state.bodies[0].cave == 2);
    assert(state.bodies[0].sigilAvailable);
    assert(hasEvent(events, GameEventType::BodyCreated));
}

void searchRecoversSigilAndActivatesExtraction() {
    auto state = makeObjectiveMatch();
    TurnResolver resolver;

    static_cast<void>(resolver.resolve(state, {
        PlayerAction{1, ActionType::Shoot, CaveId{2}}
    }));
    static_cast<void>(resolver.resolve(state, {
        PlayerAction{1, ActionType::Move, CaveId{2}}
    }));

    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    });

    assert(state.players[0].heldSigilFrom == PlayerId{2});
    assert(!state.bodies[0].sigilAvailable);
    assert(state.extraction.active);
    assert(state.extraction.sigilHolder == PlayerId{1});
    assert(state.extraction.cave.has_value());
    assert(*state.extraction.cave == CaveId{6});
    assert(hasEvent(events, GameEventType::BodyFound));
    assert(hasEvent(events, GameEventType::SigilAcquired));
    assert(hasEvent(events, GameEventType::ExtractionActivated));
}

void bodyRemainsDynamicAfterStaticSearch() {
    auto state = makeObjectiveMatch();
    TurnResolver resolver;

    // Player 1 consumes Cave 2's static search before a body exists there.
    static_cast<void>(resolver.resolve(state, {
        PlayerAction{1, ActionType::Move, CaveId{2}}
    }));
    static_cast<void>(resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    }));

    // Move away, kill Player 2 after placing them in Cave 2, then return.
    static_cast<void>(resolver.resolve(state, {
        PlayerAction{1, ActionType::Move, CaveId{1}}
    }));
    state.players[1].health = 0;
    state.players[1].alive = false;
    state.bodies.push_back(BodyState{2, 2, true});
    static_cast<void>(resolver.resolve(state, {
        PlayerAction{1, ActionType::Move, CaveId{2}}
    }));

    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    });

    assert(state.players[0].heldSigilFrom == PlayerId{2});
    assert(hasEvent(events, GameEventType::BodyFound));
    assert(hasEvent(events, GameEventType::SigilAcquired));
    assert(hasEvent(events, GameEventType::CaveAlreadySearched));
}

void escapeRequiresExplicitContextualAction() {
    auto state = makeObjectiveMatch();
    state.players[1].alive = false;
    state.bodies.push_back(BodyState{2, 1, true});
    TurnResolver resolver;

    static_cast<void>(resolver.resolve(state, {
        PlayerAction{1, ActionType::Search, std::nullopt}
    }));
    assert(state.extraction.cave == CaveId{6});

    for (CaveId next : {2u, 3u, 4u, 5u, 6u}) {
        static_cast<void>(resolver.resolve(state, {
            PlayerAction{1, ActionType::Move, next}
        }));
    }

    assert(state.result.status == MatchStatus::Active);

    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Contextual, std::nullopt, std::nullopt,
                     ContextualActionType::Escape}
    });

    assert(state.result.status == MatchStatus::Completed);
    assert(state.result.outcome == MatchOutcome::EscapedWithSigil);
    assert(state.result.winner == PlayerId{1});
    assert(hasEvent(events, GameEventType::PlayerEscaped));
}

void mutualDeathIsDraw() {
    auto state = makeObjectiveMatch();
    state.players[0].health = 20;
    state.players[1].health = 20;
    TurnResolver resolver;

    const auto events = resolver.resolve(state, {
        PlayerAction{1, ActionType::Shoot, CaveId{2}},
        PlayerAction{2, ActionType::Shoot, CaveId{1}}
    });

    assert(state.result.status == MatchStatus::Completed);
    assert(state.result.outcome == MatchOutcome::Draw);
    assert(!state.result.winner.has_value());
    assert(state.bodies.size() == 2);
    assert(hasEvent(events, GameEventType::MatchDrawn));
}

} // namespace

int main() {
    deathCreatesBodyAndSigil();
    searchRecoversSigilAndActivatesExtraction();
    bodyRemainsDynamicAfterStaticSearch();
    escapeRequiresExplicitContextualAction();
    mutualDeathIsDraw();

    std::cout << "Basilisk objective tests passed.\n";
    return 0;
}
