#include <algorithm>
#include <cassert>
#include <cstddef>
#include <set>

#include "LocalSandboxSessionAdapter.hpp"
#include "SandboxPresentation.hpp"
#include "basilisk/client/MatchMode.hpp"

using namespace basilisk;
using namespace basilisk::game;

namespace {

const AvailableAction& searchAction(const PlayerRoundSnapshot& snapshot) {
    const auto action = std::find_if(snapshot.availableActions.begin(),
        snapshot.availableActions.end(), [](const AvailableAction& candidate) {
            return candidate.type == ActionType::Search;
        });
    assert(action != snapshot.availableActions.end());
    return *action;
}

void advanceUntilNextRound(LocalSandboxSession& local, PlayerId human) {
    const RoundNumber round = local.session->snapshotFor(human)->round;
    for (int step = 0; step < 30 &&
            local.session->snapshotFor(human)->round == round; ++step) {
        local.driver->advance(500);
    }
    assert(local.session->snapshotFor(human)->round == round + 1);
    local.driver->advance(1);
    assert(local.session->snapshotFor(human)->round == round + 1);
}

} // namespace

int main() {
    assert(LocalSandboxSessionAdapter::create(1, MapSeed{1}, MatchSeed{2},
        client::ai::AiDifficulty::Medium, client::ai::AiBehavior::Balanced,
        client::ai::AiSeed{3}).session == nullptr);
    assert(LocalSandboxSessionAdapter::create(7, MapSeed{1}, MatchSeed{2},
        client::ai::AiDifficulty::Medium, client::ai::AiBehavior::Balanced,
        client::ai::AiSeed{3}).session == nullptr);

    for (std::size_t hunterCount = 2; hunterCount <= 6; ++hunterCount) {
        const MapSeed mapSeed = hunterCount == 2
            ? MapSeed{20260816} : MapSeed{9000 + hunterCount};
        auto local = LocalSandboxSessionAdapter::create(hunterCount,
            mapSeed, MatchSeed{42000},
            client::ai::AiDifficulty::Hard, client::ai::AiBehavior::Random,
            client::ai::AiSeed{77});
        assert(local.session != nullptr && local.driver != nullptr);
        assert(local.session->matchMode() == client::MatchMode::Sandbox);
        assert(!client::trophyEligible(local.session->matchMode()));
        assert(local.session->matchMetadata().players.size() == hunterCount);
        assert(local.session->profiles().size() == hunterCount);
        assert(local.resolvedBehaviors.size() == hunterCount - 1);
        assert(std::none_of(local.resolvedBehaviors.begin(),
            local.resolvedBehaviors.end(), [](client::ai::AiBehavior behavior) {
                return behavior == client::ai::AiBehavior::Random;
            }));

        std::set<PlayerId> players;
        for (const PublicPlayerSlot& slot : local.session->matchMetadata().players) {
            assert(players.insert(slot.player).second);
            assert(local.session->snapshotFor(slot.player) != nullptr);
        }
        const auto strip = sandboxParticipantPresentation(*local.session);
        assert(strip.size() == hunterCount);
        assert(strip.front().label == "HOST");
        assert(strip.front().local && strip.front().viewed && strip.front().alive);
        for (std::size_t index = 1; index < strip.size(); ++index) {
            assert(strip[index].label == "BASILISK AI " + std::to_string(index + 1));
            assert(!strip[index].subtitle.empty());
            assert(strip[index].subtitle.find("Random") == std::string::npos);
            assert(!strip[index].local && strip[index].alive);
        }
        const PlayerId human = local.session->viewContext().localPlayer;
        const PlayerRoundSnapshot before = *local.session->snapshotFor(human);
        assert(local.session->submitAndLock(searchAction(before)));
        advanceUntilNextRound(local, human);
        for (const PublicPlayerSlot& slot : local.session->matchMetadata().players)
            assert(local.session->snapshotFor(slot.player)->round == before.round + 1);
    }

    auto first = LocalSandboxSessionAdapter::create(4, MapSeed{9004},
        MatchSeed{42000}, client::ai::AiDifficulty::Medium,
        client::ai::AiBehavior::Random, client::ai::AiSeed{8181});
    auto second = LocalSandboxSessionAdapter::create(4, MapSeed{9004},
        MatchSeed{42000}, client::ai::AiDifficulty::Medium,
        client::ai::AiBehavior::Random, client::ai::AiSeed{8181});
    assert(first.resolvedBehaviors == second.resolvedBehaviors);
}
