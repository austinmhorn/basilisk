#include <cassert>
#include <algorithm>
#include <cstddef>
#include <memory>

#include "LocalAiGameSessionAdapter.hpp"
#include "basilisk/client/MatchMode.hpp"

using namespace basilisk;
using namespace basilisk::game;

namespace {

const AvailableAction& actionOfType(
    const PlayerRoundSnapshot& snapshot,
    ActionType type) {
    const auto action = std::find_if(snapshot.availableActions.begin(),
        snapshot.availableActions.end(), [&](const AvailableAction& candidate) {
            return candidate.type == type;
        });
    assert(action != snapshot.availableActions.end());
    return *action;
}

void advanceOneRound(
    LocalAiSession& local,
    PlayerId human,
    const AvailableAction& humanAction) {
    const RoundNumber round = local.session->snapshotFor(human)->round;
    assert(local.session->submitAndLock(humanAction));

    // The AI may already have completed its independent think/lock while the
    // human was idle. In that case the human lock resolves immediately.
    if (local.session->snapshotFor(human)->round == round + 1) return;

    // The AI must not act before its deterministic think deadline.
    local.driver->advance(900);
    assert(local.session->snapshotFor(human)->round == round + 1);

    // The consumed schedule cannot act a second time in the same round.
    local.driver->advance(1);
    assert(local.session->snapshotFor(human)->round == round + 1);
}

} // namespace

int main() {
    auto local = LocalAiGameSessionAdapter::create(
        MapSeed{20260816}, MatchSeed{424242},
        client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Random,
        client::ai::AiSeed{77});
    assert(local.session != nullptr && local.driver != nullptr);
    assert(local.resolvedBehavior != client::ai::AiBehavior::Random);
    assert(local.session->matchMode() == client::MatchMode::AI);
    assert(local.session->matchMetadata().players.size() == 2);

    const PlayerId human = local.session->viewContext().localPlayer;
    const auto aiSlot = std::find_if(local.session->matchMetadata().players.begin(),
        local.session->matchMetadata().players.end(),
        [&](const PublicPlayerSlot& slot) { return slot.player != human; });
    assert(aiSlot != local.session->matchMetadata().players.end());
    assert(local.session->snapshotFor(human) != nullptr);
    assert(local.session->snapshotFor(aiSlot->player) != nullptr);
    assert(local.session->snapshotFor(human)->arrows == 3);
    assert(local.session->snapshotFor(human)->maxArrows == 5);
    assert(local.session->snapshotFor(aiSlot->player)->arrows == 3);
    assert(local.session->snapshotFor(aiSlot->player)->maxArrows == 5);
    const auto aiProfile = std::find_if(local.session->profiles().begin(),
        local.session->profiles().end(), [&](const client::PublicPlayerProfile& profile) {
            return profile.player == aiSlot->player;
        });
    assert(aiProfile != local.session->profiles().end());
    assert(aiProfile->username == "BASILISK AI");
    assert(!local.session->participantSubtitle(aiSlot->player).empty());

    const PlayerRoundSnapshot before = *local.session->snapshotFor(human);
    const auto search = std::find_if(before.availableActions.begin(),
        before.availableActions.end(), [](const AvailableAction& action) {
            return action.type == ActionType::Search;
        });
    assert(search != before.availableActions.end());
    assert(local.session->submitAndLock(*search));

    // The UI may be paused, but its local session driver continues to advance.
    local.driver->advance(901);
    const PlayerRoundSnapshot* after = local.session->snapshotFor(human);
    assert(after != nullptr && after->round == before.round + 1);
    assert(local.session->snapshotFor(aiSlot->player)->round == after->round);

    // No stale AI schedule can resolve the same round twice.
    local.driver->advance(349);
    assert(local.session->snapshotFor(human)->round == after->round);

    // A stationary human action never gates the AI turn. Each actionable round
    // receives one AI decision, submission, and lock, advancing exactly once.
    for (std::size_t turn = 0; turn < 8; ++turn) {
        const PlayerRoundSnapshot snapshot = *local.session->snapshotFor(human);
        assert(snapshot.matchStatus == MatchStatus::Active && snapshot.alive);
        advanceOneRound(local, human, actionOfType(snapshot, ActionType::Search));
        assert(local.session->snapshotFor(aiSlot->player)->round ==
            local.session->snapshotFor(human)->round);
    }

    // Moving uses the identical scheduling path.
    const PlayerRoundSnapshot moveRound = *local.session->snapshotFor(human);
    advanceOneRound(local, human, actionOfType(moveRound, ActionType::Move));

    // Shooting is another stationary action; it cannot gate the AI scheduler.
    auto shooting = LocalAiGameSessionAdapter::create(
        MapSeed{20260816}, MatchSeed{424242},
        client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Random,
        client::ai::AiSeed{77});
    const PlayerId shooter = shooting.session->viewContext().localPlayer;
    const PlayerRoundSnapshot shootRound = *shooting.session->snapshotFor(shooter);
    advanceOneRound(shooting, shooter,
        actionOfType(shootRound, ActionType::Shoot));

    // Shadow inference observes the same safe decision without changing the
    // authoritative heuristic action or round outcome.
    auto telemetry = std::make_shared<client::ai::AiShadowTelemetry>();
    auto shadow = LocalAiGameSessionAdapter::create(
        MapSeed{20260816}, MatchSeed{424242}, client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Balanced, client::ai::AiSeed{77},
        {client::ai::RuntimeAiPolicyMode::Shadow, BASILISK_TEST_LEARNED_MODEL,
            "local-ai-shadow", telemetry});
    auto control = LocalAiGameSessionAdapter::create(
        MapSeed{20260816}, MatchSeed{424242}, client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Balanced, client::ai::AiSeed{77});
    const PlayerId shadowHuman = shadow.session->viewContext().localPlayer;
    const PlayerId controlHuman = control.session->viewContext().localPlayer;
    advanceOneRound(shadow, shadowHuman,
        actionOfType(*shadow.session->snapshotFor(shadowHuman), ActionType::Search));
    advanceOneRound(control, controlHuman,
        actionOfType(*control.session->snapshotFor(controlHuman), ActionType::Search));
    assert(telemetry->aggregate().decisions >= 1);
    assert(shadow.session->snapshotFor(shadowHuman)->round ==
        control.session->snapshotFor(controlHuman)->round);
    assert(shadow.session->snapshotFor(shadowHuman)->currentCave ==
        control.session->snapshotFor(controlHuman)->currentCave);

    auto learned = LocalAiGameSessionAdapter::create(
        MapSeed{20260816}, MatchSeed{424242}, client::ai::AiDifficulty::Hard,
        client::ai::AiBehavior::Balanced, client::ai::AiSeed{77},
        {client::ai::RuntimeAiPolicyMode::Learned, BASILISK_TEST_LEARNED_MODEL,
            "local-ai-learned", {}});
    const PlayerId learnedHuman = learned.session->viewContext().localPlayer;
    advanceOneRound(learned, learnedHuman,
        actionOfType(*learned.session->snapshotFor(learnedHuman), ActionType::Search));
    assert(learned.session->snapshotFor(learnedHuman)->round == RoundNumber{2});
}
