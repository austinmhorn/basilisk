#include <algorithm>
#include <cassert>
#include <iostream>

#include "basilisk/client/ai/AiDecisionEngine.hpp"
#include "basilisk/client/ai/AiKnowledgeState.hpp"
#include "basilisk/client/ai/AiTurnScheduler.hpp"

using namespace basilisk;
using namespace basilisk::client::ai;

namespace {

bool same(const AvailableAction& a, const AvailableAction& b) {
    return a.type == b.type && a.targetCave == b.targetCave &&
        a.targetTunnel == b.targetTunnel && a.targetItem == b.targetItem &&
        a.contextualAction == b.contextualAction;
}

PlayerRoundSnapshot snapshot() {
    PlayerRoundSnapshot result;
    result.player = 42; result.round = 7; result.health = 45; result.maxHealth = 100;
    result.arrows = 3; result.maxArrows = 5; result.alive = true;
    AvailableAction search; search.type = ActionType::Search;
    AvailableAction shoot; shoot.type = ActionType::Shoot; shoot.targetCave = 9;
    AvailableAction move; move.type = ActionType::Move; move.targetTunnel = TunnelId{2};
    AvailableAction heal; heal.type = ActionType::UseItem; heal.targetItem = ItemType::HealingDraught;
    result.availableActions = {search, shoot, move, heal};
    return result;
}

AvailableAction moveTo(CaveId cave) {
    AvailableAction action; action.type = ActionType::Move; action.targetCave = cave;
    return action;
}

AvailableAction moveThrough(TunnelId tunnel) {
    AvailableAction action; action.type = ActionType::Move; action.targetTunnel = tunnel;
    return action;
}

AvailableAction searchAction() {
    AvailableAction action; action.type = ActionType::Search; return action;
}

AvailableAction shootAt(CaveId cave) {
    AvailableAction action; action.type = ActionType::Shoot; action.targetCave = cave;
    return action;
}

PlayerRoundSnapshot awarenessSnapshot(
    RoundNumber round, CaveId current, std::vector<TunnelView> exits,
    std::vector<AvailableAction> actions,
    std::vector<PlayerObservation> observations = {}) {
    PlayerRoundSnapshot result;
    result.player = 42; result.round = round; result.alive = true;
    result.health = 100; result.maxHealth = 100; result.arrows = 3; result.maxArrows = 5;
    result.currentCave = current; result.map.currentCave = current;
    result.map.caves.push_back(DiscoveredCaveView{current, std::move(exits), false});
    result.availableActions = std::move(actions);
    result.observations = std::move(observations);
    return result;
}

void difficultyRespectsWarningsWithDeterministicMistakes() {
    int easyAvoids = 0;
    int easyCareless = 0;
    int mediumAvoids = 0;
    int mediumCareless = 0;
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        auto warned = awarenessSnapshot(9, 1,
            {TunnelView{1, CaveId{2}, false}},
            {moveTo(2), searchAction()},
            {PlayerObservation{ObservationType::PitNearby, 42}});
        AiKnowledgeState easyKnowledge; easyKnowledge.observe(warned);
        AiDecisionEngine engine;
        const auto easy = engine.choose(warned,
            {AiDifficulty::Easy, AiBehavior::Balanced, 42, seed}, easyKnowledge);
        (easy->type == ActionType::Move ? easyCareless : easyAvoids)++;
        AiKnowledgeState mediumKnowledge; mediumKnowledge.observe(warned);
        const auto medium = engine.choose(warned,
            {AiDifficulty::Medium, AiBehavior::Balanced, 42, seed}, mediumKnowledge);
        (medium->type == ActionType::Move ? mediumCareless : mediumAvoids)++;
    }
    assert(easyAvoids > easyCareless && easyCareless > 0);
    assert(easyCareless >= 25 && easyCareless <= 55);
    assert(mediumAvoids > mediumCareless && mediumCareless > 0);
    assert(mediumCareless >= 3 && mediumCareless <= 20);
}

void hardDeducesAndNeverChoosesConfirmedPit() {
    AiKnowledgeState knowledge;
    knowledge.observe(awarenessSnapshot(1, 2,
        {TunnelView{1, CaveId{1}, false}}, {}));
    PlayerRoundSnapshot clue = awarenessSnapshot(2, 1,
        {TunnelView{1, CaveId{2}, false}, TunnelView{2, CaveId{3}, false}},
        {moveTo(2), moveTo(3), searchAction()},
        {PlayerObservation{ObservationType::PitNearby, 42}});
    // Cave 2 was physically visited in the prior round; Cave 3 is only surveyed.
    clue.map.caves.push_back(DiscoveredCaveView{2, {}, false});
    clue.map.caves.push_back(DiscoveredCaveView{3, {}, true});
    knowledge.observe(clue);
    assert(knowledge.isKnownSafe(2));
    assert(knowledge.isConfirmedPit(3));
    AiDecisionEngine engine;
    const auto choice = engine.choose(clue,
        {AiDifficulty::Hard, AiBehavior::Explorer, 42, 5}, knowledge);
    assert(!(choice->type == ActionType::Move && choice->targetCave == CaveId{3}));
    const auto candidates = engine.evaluate(clue,
        {AiDifficulty::Hard, AiBehavior::Explorer, 42, 5}, knowledge);
    assert(std::none_of(candidates.actions.begin(), candidates.actions.end(),
        [](const AiActionUtility& candidate) {
            return candidate.action.type == ActionType::Move &&
                candidate.action.targetCave == CaveId{3};
        }));
}

void hardSafetyFilterRemovesOnlyPlayerKnownPits() {
    AiDecisionEngine engine;
    auto warned = awarenessSnapshot(6, 1,
        {TunnelView{1, CaveId{2}, true}, TunnelView{2, std::nullopt, false}},
        {moveTo(2), moveThrough(2), searchAction()});
    AiKnowledgeState knowledge;
    knowledge.observe(warned);

    for (const AiBehavior behavior : {
            AiBehavior::Aggressive, AiBehavior::Explorer,
            AiBehavior::ObjectiveFocused, AiBehavior::Survivalist}) {
        for (std::uint64_t seed = 1; seed <= 20; ++seed) {
            const auto evaluation = engine.evaluate(warned,
                {AiDifficulty::Hard, behavior, 42, seed}, knowledge);
            assert(std::none_of(evaluation.actions.begin(), evaluation.actions.end(),
                [](const AiActionUtility& candidate) {
                    return candidate.action.type == ActionType::Move &&
                        candidate.action.targetCave == CaveId{2};
                }));
            assert(std::any_of(evaluation.actions.begin(), evaluation.actions.end(),
                [](const AiActionUtility& candidate) {
                    return candidate.action.type == ActionType::Move &&
                        candidate.action.targetTunnel == TunnelId{2};
                }));
        }
    }

    // The safety boundary is Hard-only; existing Easy/Medium risk utilities
    // remain responsible for their imperfect behavior.
    for (const AiDifficulty difficulty : {AiDifficulty::Easy, AiDifficulty::Medium}) {
        const auto evaluation = engine.evaluate(warned,
            {difficulty, AiBehavior::Aggressive, 42, 9}, knowledge);
        assert(std::any_of(evaluation.actions.begin(), evaluation.actions.end(),
            [](const AiActionUtility& candidate) {
                return candidate.action.type == ActionType::Move &&
                    candidate.action.targetCave == CaveId{2};
            }));
    }
}

void hardSearchesOnlyWhenPitUncertaintyBlocksProgress() {
    auto blocked = awarenessSnapshot(4, 1,
        {TunnelView{1, std::nullopt, false}, TunnelView{2, std::nullopt, false}},
        {moveThrough(1), moveThrough(2), searchAction()},
        {PlayerObservation{ObservationType::PitNearby, 42}});
    AiKnowledgeState blockedKnowledge; blockedKnowledge.observe(blocked);
    AiDecisionEngine engine;
    assert(engine.choose(blocked,
        {AiDifficulty::Hard, AiBehavior::Aggressive, 42, 8}, blockedKnowledge)->type ==
        ActionType::Search);

    AiKnowledgeState irrelevantKnowledge;
    irrelevantKnowledge.observe(awarenessSnapshot(4, 2,
        {TunnelView{1, CaveId{1}, false}}, {}));
    auto irrelevant = awarenessSnapshot(5, 1,
        {TunnelView{1, CaveId{2}, false}, TunnelView{2, CaveId{3}, false}},
        {moveTo(2), moveTo(3), searchAction(), shootAt(2)},
        {PlayerObservation{ObservationType::PitNearby, 42}});
    irrelevant.map.caves.push_back(DiscoveredCaveView{2, {}, false});
    irrelevant.map.caves.push_back(DiscoveredCaveView{3, {}, true});
    irrelevantKnowledge.observe(irrelevant);
    const auto choice = engine.choose(irrelevant,
        {AiDifficulty::Hard, AiBehavior::Aggressive, 42, 8}, irrelevantKnowledge);
    assert(choice->type != ActionType::Search);
}

void mediumBacktracksAndHardBreaksJackalOscillation() {
    AiDecisionEngine engine;
    AiKnowledgeState mediumKnowledge;
    auto atA = awarenessSnapshot(1, 1, {TunnelView{1, CaveId{2}, false}}, {});
    mediumKnowledge.observe(atA);
    auto atB = awarenessSnapshot(2, 2,
        {TunnelView{1, CaveId{1}, false}, TunnelView{2, CaveId{3}, false}},
        {moveTo(1), moveTo(3), searchAction()},
        {PlayerObservation{ObservationType::JackalNearby, 42}});
    mediumKnowledge.observe(atB);
    assert(engine.choose(atB,
        {AiDifficulty::Medium, AiBehavior::Balanced, 42, 19}, mediumKnowledge)->targetCave ==
        CaveId{1});

    AiKnowledgeState hardKnowledge;
    hardKnowledge.observe(atA);
    hardKnowledge.observe(atB);
    auto backAtA = awarenessSnapshot(3, 1,
        {TunnelView{1, CaveId{2}, false}}, {moveTo(2), searchAction()},
        {PlayerObservation{ObservationType::JackalNearby, 42}});
    hardKnowledge.observe(backAtA);
    auto againAtB = atB; againAtB.round = 4;
    hardKnowledge.observe(againAtB);
    assert(hardKnowledge.temporarilyAvoids(1, 2, 4));
    const auto hardChoice = engine.choose(againAtB,
        {AiDifficulty::Hard, AiBehavior::Balanced, 42, 19}, hardKnowledge);
    assert(hardChoice->targetCave != CaveId{1});
}

void movementWithoutPlayerSafeJackalWarningCreatesNoPrediction() {
    AiKnowledgeState knowledge;
    knowledge.observe(awarenessSnapshot(
        1, 1, {TunnelView{1, CaveId{2}, false}}, {}));
    knowledge.observe(awarenessSnapshot(
        2, 2, {TunnelView{1, CaveId{1}, false}}, {moveTo(1)}));
    assert(!knowledge.jackalWarningHere());
    assert(!knowledge.temporarilyAvoids(1, 2, 2));
}

void basiliskAwarenessPreservesBehaviorPersonality() {
    AiKnowledgeState knowledge;
    knowledge.observe(awarenessSnapshot(1, 1,
        {TunnelView{1, CaveId{2}, false}}, {}));
    auto warned = awarenessSnapshot(2, 2,
        {TunnelView{1, CaveId{1}, false}, TunnelView{2, CaveId{3}, false}},
        {moveTo(1), moveTo(3), searchAction(), shootAt(3)},
        {PlayerObservation{ObservationType::BasiliskNearby, 42}});
    knowledge.observe(warned);
    AiDecisionEngine engine;
    const auto aggressive = engine.choose(warned,
        {AiDifficulty::Hard, AiBehavior::Aggressive, 42, 33}, knowledge);
    const auto survivalist = engine.choose(warned,
        {AiDifficulty::Hard, AiBehavior::Survivalist, 42, 33}, knowledge);
    assert(aggressive->type == ActionType::Shoot);
    assert(survivalist->type == ActionType::Shoot);
}

void repeatedSearchUtilityDecaysWithoutNewInformation() {
    AiDecisionEngine engine;
    AiKnowledgeState knowledge;
    auto firstRound = awarenessSnapshot(1, 1,
        {TunnelView{1, CaveId{2}, false}},
        {moveTo(2), searchAction()});
    knowledge.observe(firstRound);
    const AiConfig hardExplorer{AiDifficulty::Hard, AiBehavior::Explorer, 42, 91};
    const auto first = engine.choose(firstRound, hardExplorer, knowledge);
    assert(first->type == ActionType::Search);
    knowledge.recordDecision(*first);

    auto unchanged = firstRound;
    unchanged.round = 2;
    knowledge.observe(unchanged);
    assert(knowledge.repeatedSearchCount() == 1);
    const auto afterSearch = engine.choose(unchanged, hardExplorer, knowledge);
    assert(afterSearch->type != ActionType::Search);

    // The history is part of the deterministic input, and Search remains legal
    // when it is the only reasonable action rather than being hard-banned.
    assert(same(*afterSearch, *engine.choose(unchanged, hardExplorer, knowledge)));
    unchanged.availableActions = {searchAction()};
    assert(engine.choose(unchanged, hardExplorer, knowledge)->type == ActionType::Search);

    AiKnowledgeState repeated;
    repeated.observe(firstRound);
    repeated.recordDecision(searchAction());
    repeated.recordDecision(searchAction());
    int easyRepeats = 0;
    int mediumRepeats = 0;
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        if (engine.choose(firstRound,
                {AiDifficulty::Easy, AiBehavior::Explorer, 42, seed}, repeated)->type ==
            ActionType::Search) ++easyRepeats;
        if (engine.choose(firstRound,
                {AiDifficulty::Medium, AiBehavior::Explorer, 42, seed}, repeated)->type ==
            ActionType::Search) ++mediumRepeats;
    }
    assert(easyRepeats > mediumRepeats);
    assert(mediumRepeats < 20);
}

void usefulPitInvestigationCanContinueThenStops() {
    AiDecisionEngine engine;
    AiKnowledgeState knowledge;
    const AiConfig hard{AiDifficulty::Hard, AiBehavior::Balanced, 42, 18};
    auto blocked = awarenessSnapshot(1, 1,
        {TunnelView{1, std::nullopt, false}, TunnelView{2, std::nullopt, false}},
        {moveThrough(1), moveThrough(2), searchAction()},
        {PlayerObservation{ObservationType::PitNearby, 42}});
    knowledge.observe(blocked);
    auto choice = engine.choose(blocked, hard, knowledge);
    assert(choice->type == ActionType::Search);
    knowledge.recordDecision(*choice);

    // An inconclusive investigation leaves the same meaningful state, but Hard
    // may search again because every route remains blocked by Pit uncertainty.
    blocked.round = 2;
    blocked.observations.push_back(
        PlayerObservation{ObservationType::PitInvestigationInconclusive, 42});
    knowledge.observe(blocked);
    assert(knowledge.repeatedSearchCount() == 1);
    choice = engine.choose(blocked, hard, knowledge);
    assert(choice->type == ActionType::Search);
    knowledge.recordDecision(*choice);

    // A newly available known-safe route is a material knowledge/route change;
    // continued investigation is no longer necessary.
    auto resolved = awarenessSnapshot(3, 1,
        {TunnelView{1, CaveId{2}, false}, TunnelView{2, std::nullopt, false}},
        {moveTo(2), moveThrough(2), searchAction()},
        {PlayerObservation{ObservationType::PitNearby, 42},
         PlayerObservation{ObservationType::PitInvestigationSucceeded, 42,
             std::nullopt, std::nullopt, 0, std::nullopt, std::nullopt,
             TunnelId{2}}});
    // Visiting Cave 2 previously is the player-safe proof that it is safe.
    AiKnowledgeState resolvedKnowledge;
    resolvedKnowledge.observe(awarenessSnapshot(1, 2,
        {TunnelView{1, CaveId{1}, false}}, {}));
    resolvedKnowledge.recordDecision(searchAction());
    resolvedKnowledge.observe(resolved);
    assert(resolvedKnowledge.materialRevision() > 1);
    assert(engine.choose(resolved, hard, resolvedKnowledge)->type == ActionType::Move);
}

void arrowConservationUsesOnlyPlayerSafeEvidence() {
    AiDecisionEngine engine;
    auto unsupported = awarenessSnapshot(8, 1,
        {TunnelView{1, CaveId{2}, false}},
        {moveTo(2), searchAction(), shootAt(2)});
    AiKnowledgeState none;
    none.observe(unsupported);
    assert(engine.choose(unsupported,
        {AiDifficulty::Hard, AiBehavior::Aggressive, 42, 5}, none)->type !=
        ActionType::Shoot);

    int easySpeculative = 0;
    int mediumSpeculative = 0;
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        if (engine.choose(unsupported,
            {AiDifficulty::Easy, AiBehavior::Aggressive, 42, seed}, none)->type ==
            ActionType::Shoot) ++easySpeculative;
        if (engine.choose(unsupported,
            {AiDifficulty::Medium, AiBehavior::Aggressive, 42, seed}, none)->type ==
            ActionType::Shoot) ++mediumSpeculative;
    }
    assert(easySpeculative > 0);
    assert(easySpeculative > mediumSpeculative);

    auto ambiguous = awarenessSnapshot(9, 1,
        {TunnelView{1, CaveId{2}, false}, TunnelView{2, CaveId{3}, false}},
        {moveTo(2), searchAction(), shootAt(2), shootAt(3)},
        {PlayerObservation{ObservationType::BasiliskNearby, 42},
         PlayerObservation{ObservationType::PitNearby, 42}});
    AiKnowledgeState ambiguousKnowledge;
    ambiguousKnowledge.observe(ambiguous);
    assert(engine.choose(ambiguous,
        {AiDifficulty::Hard, AiBehavior::ObjectiveFocused, 42, 5},
        ambiguousKnowledge)->type == ActionType::Shoot);

    auto supported = awarenessSnapshot(10, 1,
        {TunnelView{1, CaveId{2}, false}},
        {moveTo(2), searchAction(), shootAt(2)},
        {PlayerObservation{ObservationType::BasiliskNearby, 42}});
    supported.arrows = 1;
    supported.recoverableRivalSigilAvailable = true;
    AiKnowledgeState supportedKnowledge;
    supportedKnowledge.observe(supported);
    assert(engine.choose(supported,
        {AiDifficulty::Hard, AiBehavior::ObjectiveFocused, 42, 5},
        supportedKnowledge)->type == ActionType::Shoot);

    auto jackal = awarenessSnapshot(11, 1,
        {TunnelView{1, CaveId{2}, false}},
        {moveTo(2), searchAction(), shootAt(2)},
        {PlayerObservation{ObservationType::JackalNearby, 42}});
    AiKnowledgeState jackalKnowledge;
    jackalKnowledge.observe(jackal);
    assert(engine.choose(jackal,
        {AiDifficulty::Medium, AiBehavior::Aggressive, 42, 5},
        jackalKnowledge)->type != ActionType::Shoot);
    assert(engine.choose(jackal,
        {AiDifficulty::Hard, AiBehavior::Aggressive, 42, 5},
        jackalKnowledge)->type != ActionType::Shoot);
    jackal.availableActions = {searchAction(), shootAt(2)};
    assert(engine.choose(jackal,
        {AiDifficulty::Hard, AiBehavior::Aggressive, 42, 5},
        jackalKnowledge)->type == ActionType::Shoot);

    auto rival = supported;
    rival.recoverableRivalSigilAvailable = false;
    rival.observations = {PlayerObservation{ObservationType::RivalNearby, 42}};
    rival.arrows = rival.maxArrows;
    AiKnowledgeState rivalKnowledge;
    rivalKnowledge.observe(rival);
    assert(engine.choose(rival,
        {AiDifficulty::Hard, AiBehavior::Aggressive, 42, 5},
        rivalKnowledge)->type == ActionType::Shoot);

    auto scarce = unsupported;
    scarce.arrows = 1;
    int fullArrowShots = 0;
    int lastArrowShots = 0;
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        if (engine.choose(unsupported,
            {AiDifficulty::Easy, AiBehavior::Aggressive, 42, seed}, none)->type ==
            ActionType::Shoot) ++fullArrowShots;
        if (engine.choose(scarce,
            {AiDifficulty::Easy, AiBehavior::Aggressive, 42, seed}, none)->type ==
            ActionType::Shoot) ++lastArrowShots;
    }
    assert(lastArrowShots < fullArrowShots);

    auto rivalLastArrow = rival;
    rivalLastArrow.arrows = 1;
    assert(engine.choose(rival,
        {AiDifficulty::Hard, AiBehavior::Aggressive, 42, 5},
        rivalKnowledge)->type == ActionType::Shoot);
    AiKnowledgeState rivalLastKnowledge;
    rivalLastKnowledge.observe(rivalLastArrow);
    assert(engine.choose(rivalLastArrow,
        {AiDifficulty::Hard, AiBehavior::Aggressive, 42, 5},
        rivalLastKnowledge)->type != ActionType::Shoot);
}

void hardBasiliskEngagementUsesDeductionsAndRetreat() {
    AiDecisionEngine engine;
    const AiConfig objective{
        AiDifficulty::Hard, AiBehavior::ObjectiveFocused, 42, 27};

    // Adjacent evidence starts terminal engagement even with multiple targets.
    auto ambiguous = awarenessSnapshot(2, 1,
        {TunnelView{1, CaveId{2}, false}, TunnelView{2, CaveId{3}, false}},
        {moveTo(2), moveTo(3), searchAction(), shootAt(2), shootAt(3)},
        {PlayerObservation{ObservationType::BasiliskNearby, 42},
         PlayerObservation{ObservationType::PitNearby, 42}});
    AiKnowledgeState knowledge;
    knowledge.observe(ambiguous);
    assert(engine.choose(ambiguous, objective, knowledge)->type == ActionType::Shoot);

    // The investigation confirms Cave 2's tunnel as the Pit route. Cave 3 is
    // now the sole supported Basilisk target, without authoritative truth.
    auto narrowed = ambiguous;
    narrowed.round = 3;
    narrowed.observations.push_back(PlayerObservation{
        ObservationType::PitInvestigationSucceeded, 42, std::nullopt,
        std::nullopt, 0, std::nullopt, std::nullopt, TunnelId{1}});
    knowledge.observe(narrowed);
    const auto supportedShot = engine.choose(narrowed, objective, knowledge);
    assert(supportedShot->type == ActionType::Shoot);
    assert(supportedShot->targetCave == CaveId{3});
    const auto trace = engine.evaluate(narrowed, objective, knowledge);
    assert(trace.basiliskAdjacentEvidence && trace.basiliskCandidates == 1);
    assert(trace.actions[trace.chosenIndex].action.type == ActionType::Shoot);
    const double chosenUtility = trace.actions[trace.chosenIndex].utility;
    assert(std::all_of(trace.actions.begin(), trace.actions.end(),
        [&](const AiActionUtility& scored) {
            return scored.action.type == ActionType::Shoot ||
                chosenUtility > scored.utility;
        }));

    // Personality and safe retreat cannot override active engagement.
    AiKnowledgeState retreatKnowledge;
    retreatKnowledge.observe(awarenessSnapshot(1, 4,
        {TunnelView{1, CaveId{1}, false}}, {}));
    auto cannotNarrow = awarenessSnapshot(2, 1,
        {TunnelView{1, CaveId{4}, false}, TunnelView{2, CaveId{3}, false}},
        {moveTo(4), moveTo(3), searchAction(), shootAt(4), shootAt(3)},
        {PlayerObservation{ObservationType::BasiliskNearby, 42}});
    retreatKnowledge.observe(cannotNarrow);
    const auto survivalist = engine.choose(cannotNarrow,
        {AiDifficulty::Hard, AiBehavior::Survivalist, 42, 27}, retreatKnowledge);
    assert(survivalist->type == ActionType::Shoot);

    // A prior non-informative Search cannot break engagement.
    retreatKnowledge.recordDecision(searchAction());
    auto repeatedWarning = cannotNarrow;
    repeatedWarning.round = 3;
    retreatKnowledge.observe(repeatedWarning);
    const auto repeated = engine.choose(repeatedWarning,
        {AiDifficulty::Hard, AiBehavior::Survivalist, 42, 27}, retreatKnowledge);
    assert(repeated->type == ActionType::Shoot);

    const auto aggressive = engine.choose(cannotNarrow,
        {AiDifficulty::Hard, AiBehavior::Aggressive, 42, 27}, retreatKnowledge);
    assert(aggressive->type == ActionType::Shoot);
}

void recoverableSigilDrivesPlayerSafeObjectiveChoices() {
    AiDecisionEngine engine;
    const AiConfig objective{
        AiDifficulty::Hard, AiBehavior::ObjectiveFocused, 42, 91};

    auto reached = awarenessSnapshot(2, 7,
        {TunnelView{1, CaveId{8}, false}},
        {moveTo(8), searchAction()});
    reached.recoverableRivalSigilAvailable = true;
    AiKnowledgeState reachedKnowledge;
    reachedKnowledge.observe(reached);
    assert(engine.choose(reached, objective, reachedKnowledge)->type ==
        ActionType::Search);

    // The snapshot exposes only existence, never the hidden Sigil cave. Two
    // otherwise identical views therefore produce the same decision.
    AiKnowledgeState identicalKnowledge;
    identicalKnowledge.observe(reached);
    assert(same(*engine.choose(reached, objective, reachedKnowledge),
        *engine.choose(reached, objective, identicalKnowledge)));

    // Once a cave has been searched, objective exploration prefers a different
    // player-known, unsearched destination rather than revisiting it.
    auto searched = awarenessSnapshot(1, 8, {}, {searchAction()},
        {PlayerObservation{ObservationType::SearchEmpty, 42}});
    searched.recoverableRivalSigilAvailable = true;
    AiKnowledgeState routeKnowledge;
    routeKnowledge.observe(searched);
    auto routing = awarenessSnapshot(2, 7,
        {TunnelView{1, CaveId{8}, false}, TunnelView{2, CaveId{9}, false}},
        {moveTo(8), moveTo(9)});
    routing.recoverableRivalSigilAvailable = true;
    routeKnowledge.observe(routing);
    const auto route = engine.choose(routing, objective, routeKnowledge);
    assert(route->type == ActionType::Move && route->targetCave == CaveId{9});

    // Critical health still permits an immediate survival action to override
    // the otherwise-high recovery Search utility.
    auto critical = reached;
    critical.health = 20;
    AvailableAction heal;
    heal.type = ActionType::UseItem;
    heal.targetItem = ItemType::HealingDraught;
    critical.availableActions.push_back(heal);
    AiKnowledgeState criticalKnowledge;
    criticalKnowledge.observe(critical);
    const auto survival = engine.choose(critical,
        {AiDifficulty::Hard, AiBehavior::Survivalist, 42, 91}, criticalKnowledge);
    assert(survival->type == ActionType::UseItem &&
        survival->targetItem == ItemType::HealingDraught);
}

void distantBasiliskNoiseIsNotAdjacentTargetEvidence() {
    auto distant = awarenessSnapshot(3, 1,
        {TunnelView{1, CaveId{2}, false}},
        {moveTo(2), searchAction(), shootAt(2)},
        {PlayerObservation{ObservationType::RestlessBasiliskNoise, 42}});
    distant.arrows = 5;
    AiKnowledgeState knowledge;
    knowledge.observe(distant);
    assert(!knowledge.basiliskWarningHere());
    assert(knowledge.basiliskDistantWarningHere());
    AiDecisionEngine engine;
    const AiConfig config{
        AiDifficulty::Hard, AiBehavior::ObjectiveFocused, 42, 17};
    const auto evaluation = engine.evaluate(distant, config, knowledge);
    assert(!evaluation.basiliskAdjacentEvidence &&
        evaluation.basiliskDistantEvidence);
    const auto choice = engine.choose(distant, config, knowledge);
    assert(choice->type != ActionType::Shoot);
}

void hardTerminalBasiliskOverrideTracksCurrentEvidence() {
    AiDecisionEngine engine;
    AiKnowledgeState knowledge;
    const AiConfig hard{
        AiDifficulty::Hard, AiBehavior::Survivalist, 42, 313};
    auto supported = awarenessSnapshot(5, 1,
        {TunnelView{1, CaveId{2}, false}},
        {moveTo(2), searchAction(), shootAt(2)},
        {PlayerObservation{ObservationType::BasiliskNearby, 42}});
    supported.arrows = 1;
    knowledge.observe(supported);
    assert(engine.choose(supported, hard, knowledge)->type == ActionType::Shoot);

    // With unchanged player-safe evidence, the next actionable round keeps
    // taking the terminal shot even with the final arrow.
    auto stillSupported = supported;
    stillSupported.round = 6;
    knowledge.observe(stillSupported);
    assert(engine.choose(stillSupported, hard, knowledge)->type == ActionType::Shoot);

    // An evade invalidates the old target. Without fresh adjacent evidence the
    // stale Cave 2 shot cannot survive as a terminal override.
    auto evaded = stillSupported;
    evaded.round = 7;
    evaded.observations = {
        PlayerObservation{ObservationType::BasiliskEvaded, 42}};
    knowledge.observe(evaded);
    assert(engine.choose(evaded, hard, knowledge)->type != ActionType::Shoot);
}

PlayerRoundSnapshot extractionSnapshot() {
    auto result = awarenessSnapshot(10, 1,
        {TunnelView{1, CaveId{2}, false}, TunnelView{2, CaveId{3}, false}},
        {moveTo(2), moveTo(3), searchAction()});
    result.hasHunterSigil = true;
    result.extractionCave = CaveId{4};
    result.map.caves = {
        DiscoveredCaveView{1,
            {TunnelView{1, CaveId{2}, false}, TunnelView{2, CaveId{3}, false}}, false},
        DiscoveredCaveView{2,
            {TunnelView{1, CaveId{1}, false}, TunnelView{2, CaveId{4}, false}}, false},
        DiscoveredCaveView{3,
            {TunnelView{1, CaveId{1}, false}, TunnelView{2, CaveId{5}, false}}, false},
        DiscoveredCaveView{4,
            {TunnelView{1, CaveId{2}, false}, TunnelView{2, CaveId{5}, false}}, false},
        DiscoveredCaveView{5,
            {TunnelView{1, CaveId{3}, false}, TunnelView{2, CaveId{4}, false}}, false}};
    return result;
}

void hardExtractionUsesShortestKnownSafeRoute() {
    AiDecisionEngine engine;
    const AiConfig hard{
        AiDifficulty::Hard, AiBehavior::Explorer, 42, 808};
    auto extraction = extractionSnapshot();
    AiKnowledgeState knowledge;
    knowledge.observe(extraction);
    auto choice = engine.choose(extraction, hard, knowledge);
    assert(choice->type == ActionType::Move && choice->targetCave == CaveId{2});

    // A confirmed lethal Cave 2 invalidates the shortest route. Replanning over
    // the same player-known graph selects the longer safe branch through 3.
    auto hazardous = extraction;
    hazardous.round = 11;
    hazardous.temporarilyRevealedPitCaves = {2};
    knowledge.observe(hazardous);
    choice = engine.choose(hazardous, hard, knowledge);
    assert(choice->type == ActionType::Move && choice->targetCave == CaveId{3});

    // Relocation/map change recomputes from the latest current cave.
    auto relocated = hazardous;
    relocated.round = 12;
    relocated.currentCave = 3;
    relocated.map.currentCave = 3;
    relocated.availableActions = {moveTo(1), moveTo(5), searchAction()};
    knowledge.observe(relocated);
    choice = engine.choose(relocated, hard, knowledge);
    assert(choice->type == ActionType::Move && choice->targetCave == CaveId{5});

    auto arrived = relocated;
    arrived.round = 13;
    arrived.currentCave = 4;
    arrived.map.currentCave = 4;
    AvailableAction escape;
    escape.type = ActionType::Contextual;
    escape.contextualAction = ContextualActionType::Escape;
    arrived.availableActions = {searchAction(), moveTo(5), escape};
    knowledge.observe(arrived);
    for (const AiBehavior behavior : {
            AiBehavior::Explorer, AiBehavior::Aggressive,
            AiBehavior::ObjectiveFocused, AiBehavior::Survivalist}) {
        const auto win = engine.choose(arrived,
            {AiDifficulty::Hard, behavior, 42, 808}, knowledge);
        assert(win->type == ActionType::Contextual &&
            win->contextualAction == ContextualActionType::Escape);
    }
}

void hardLearnsBasiliskTargetsFromVisibleMisses() {
    AiDecisionEngine engine;
    const AiConfig hard{
        AiDifficulty::Hard, AiBehavior::Aggressive, 42, 404};
    auto warned = awarenessSnapshot(20, 1,
        {TunnelView{1, CaveId{2}, false}, TunnelView{2, CaveId{3}, false}},
        {moveTo(2), moveTo(3), searchAction(), shootAt(2), shootAt(3)},
        {PlayerObservation{ObservationType::BasiliskNearby, 42}});
    warned.arrows = 5;
    AiKnowledgeState knowledge;
    knowledge.observe(warned);

    auto sigilInvestigation = warned;
    sigilInvestigation.recoverableRivalSigilAvailable = true;
    AiKnowledgeState sigilKnowledge;
    sigilKnowledge.observe(sigilInvestigation);
    assert(engine.choose(sigilInvestigation,
        {AiDifficulty::Hard, AiBehavior::ObjectiveFocused, 42, 404},
        sigilKnowledge)->type == ActionType::Shoot);

    knowledge.recordDecision(shootAt(2));
    auto missedA = warned;
    missedA.round = 21;
    missedA.observations.push_back(
        PlayerObservation{ObservationType::ArrowMissed, 42});
    knowledge.observe(missedA);
    const auto rotated = engine.choose(missedA, hard, knowledge);
    assert(rotated->type == ActionType::Shoot && rotated->targetCave == CaveId{3});

    knowledge.recordDecision(*rotated);
    auto missedBoth = warned;
    missedBoth.round = 22;
    missedBoth.observations.push_back(
        PlayerObservation{ObservationType::ArrowMissed, 42});
    knowledge.observe(missedBoth);
    const auto reconciled = engine.choose(missedBoth, hard, knowledge);
    assert(reconciled->type == ActionType::Shoot &&
        reconciled->targetCave == CaveId{2});

    // Evade is an explicit player-visible context reset. Fresh nearby evidence
    // rebuilds both candidates instead of retaining stale misses.
    auto reacquired = warned;
    reacquired.round = 23;
    reacquired.observations.push_back(
        PlayerObservation{ObservationType::BasiliskEvaded, 42});
    knowledge.observe(reacquired);
    assert(engine.evaluate(reacquired, hard, knowledge).basiliskCandidates == 2);

    // A supported target with no miss/evade feedback remains supported.
    AiKnowledgeState hitKnowledge;
    auto unique = awarenessSnapshot(30, 5,
        {TunnelView{1, CaveId{6}, false}},
        {moveTo(6), searchAction(), shootAt(6)},
        {PlayerObservation{ObservationType::BasiliskNearby, 42}});
    unique.arrows = 5;
    hitKnowledge.observe(unique);
    hitKnowledge.recordDecision(shootAt(6));
    unique.round = 31;
    hitKnowledge.observe(unique);
    const auto repeat = engine.choose(unique, hard, hitKnowledge);
    assert(repeat->type == ActionType::Shoot && repeat->targetCave == CaveId{6});

    // Persistent adjacent evidence can consume the full magazine. Misses
    // rotate through alternatives before the deterministic candidate pass is
    // reconciled and restarted.
    AiKnowledgeState magazineKnowledge;
    auto magazine = warned;
    magazine.round = 50;
    magazine.arrows = 5;
    magazineKnowledge.observe(magazine);
    std::vector<CaveId> fired;
    for (int remaining = 5; remaining > 0; --remaining) {
        const auto shot = engine.choose(magazine, hard, magazineKnowledge);
        assert(shot->type == ActionType::Shoot && shot->targetCave);
        fired.push_back(*shot->targetCave);
        magazineKnowledge.recordDecision(*shot);
        magazine.round += 1;
        magazine.arrows = remaining - 1;
        magazine.observations = {
            PlayerObservation{ObservationType::BasiliskNearby, 42},
            PlayerObservation{ObservationType::ArrowMissed, 42}};
        magazineKnowledge.observe(magazine);
    }
    assert((fired == std::vector<CaveId>{2, 3, 2, 3, 2}));
    assert(engine.choose(magazine, hard, magazineKnowledge)->type !=
        ActionType::Shoot);
}

void hardUnknownExtractionAdvancesDiscovery() {
    AiDecisionEngine engine;
    AiKnowledgeState knowledge;
    auto hidden = awarenessSnapshot(40, 1,
        {TunnelView{1, CaveId{2}, false}, TunnelView{2, std::nullopt, false}},
        {moveTo(2), moveThrough(2), searchAction()});
    hidden.hasHunterSigil = true;
    AvailableAction survey;
    survey.type = ActionType::UseItem;
    survey.targetItem = ItemType::SurveyFragment;
    hidden.availableActions.push_back(survey);
    knowledge.observe(hidden);
    for (const AiBehavior behavior : {
            AiBehavior::Aggressive, AiBehavior::ObjectiveFocused,
            AiBehavior::Survivalist}) {
        const auto choice = engine.choose(hidden,
            {AiDifficulty::Hard, behavior, 42, 505}, knowledge);
        assert(choice->type == ActionType::UseItem &&
            choice->targetItem == ItemType::SurveyFragment);
    }

    hidden.availableActions.pop_back();
    const auto explore = engine.choose(hidden,
        {AiDifficulty::Hard, AiBehavior::Survivalist, 42, 505}, knowledge);
    assert(explore->type == ActionType::Move && explore->targetTunnel == TunnelId{2});
}

} // namespace

int main() {
    const PlayerRoundSnapshot safe = snapshot();
    AiDecisionEngine engine;
    const AiConfig hard{AiDifficulty::Hard, AiBehavior::Survivalist, 42, 12345};
    const auto first = engine.choose(safe, hard);
    const auto second = engine.choose(safe, hard);
    assert(first && second && same(*first, *second));
    assert(std::any_of(safe.availableActions.begin(), safe.availableActions.end(),
        [&](const AvailableAction& action) { return same(action, *first); }));
    assert(first->targetItem == ItemType::HealingDraught);

    difficultyRespectsWarningsWithDeterministicMistakes();
    hardDeducesAndNeverChoosesConfirmedPit();
    hardSafetyFilterRemovesOnlyPlayerKnownPits();
    hardSearchesOnlyWhenPitUncertaintyBlocksProgress();
    mediumBacktracksAndHardBreaksJackalOscillation();
    movementWithoutPlayerSafeJackalWarningCreatesNoPrediction();
    basiliskAwarenessPreservesBehaviorPersonality();
    repeatedSearchUtilityDecaysWithoutNewInformation();
    usefulPitInvestigationCanContinueThenStops();
    arrowConservationUsesOnlyPlayerSafeEvidence();
    hardBasiliskEngagementUsesDeductionsAndRetreat();
    recoverableSigilDrivesPlayerSafeObjectiveChoices();
    distantBasiliskNoiseIsNotAdjacentTargetEvidence();
    hardTerminalBasiliskOverrideTracksCurrentEvidence();
    hardExtractionUsesShortestKnownSafeRoute();
    hardLearnsBasiliskTargetsFromVisibleMisses();
    hardUnknownExtractionAdvancesDiscovery();

    const AiConfig aggressive{AiDifficulty::Hard, AiBehavior::Aggressive, 42, 12345};
    assert(engine.choose(safe, aggressive)->type != ActionType::Shoot);
    const AiBehavior random = resolveBehavior(AiBehavior::Random, 9);
    assert(random != AiBehavior::Random);
    assert(resolveBehavior(AiBehavior::Random, 9) == random);

    for (const AiDifficulty difficulty : {AiDifficulty::Easy, AiDifficulty::Medium, AiDifficulty::Hard}) {
        const AiConfig config{difficulty, AiBehavior::Balanced, 42, 456};
        const auto think = AiTurnScheduler::thinkDelayMs(config, 3);
        assert(think >= 350 && think <= 900);
        const auto clash = AiTurnScheduler::clashDelayMs(config, 7);
        if (difficulty == AiDifficulty::Easy) assert(clash >= 4500 && clash <= 6500);
        if (difficulty == AiDifficulty::Medium) assert(clash >= 2500 && clash <= 4000);
        if (difficulty == AiDifficulty::Hard) assert(clash >= 1400 && clash <= 2200);
    }

    AiTurnScheduler scheduler;
    scheduler.scheduleAction(*first, 100, hard, safe.round);
    const auto deadline = *scheduler.actionDeadline();
    assert(!scheduler.takeDueAction(deadline - 1));
    assert(scheduler.takeDueAction(deadline));
    assert(!scheduler.takeDueAction(deadline));
    scheduler.scheduleClash(8, "fang", 200, hard);
    const auto clashDeadline = *scheduler.clashDeadline();
    assert(!scheduler.takeDueClash(clashDeadline - 1));
    assert(scheduler.takeDueClash(clashDeadline)->response == "fang");
    assert(!scheduler.takeDueClash(clashDeadline));

    std::cout << "Basilisk client AI tests passed.\n";
}
