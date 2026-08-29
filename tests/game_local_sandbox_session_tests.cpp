#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <set>

#include "LocalSandboxSessionAdapter.hpp"
#include "SandboxPresentation.hpp"
#include "basilisk/client/MatchMode.hpp"

using namespace basilisk;
using namespace basilisk::game;

namespace {

client::SandboxSessionConfig configFor(std::size_t hunters, MapSeed mapSeed,
    MatchSeed matchSeed, client::ai::AiDifficulty difficulty,
    client::ai::AiBehavior behavior, client::ai::AiSeed aiSeed) {
    auto config = client::defaultSandboxSessionConfig(hunters);
    config.mapSeed = mapSeed;
    config.matchSeed = matchSeed;
    config.aiDifficulty = difficulty;
    config.aiBehavior = behavior;
    config.aiSeed = aiSeed;
    return config;
}

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
    for (std::size_t hunters = 2; hunters <= 6; ++hunters) {
        for (const std::size_t caves : client::sandboxCaveCounts) {
            auto candidate = client::defaultSandboxSessionConfig(hunters);
            candidate.caveCount = caves;
            candidate.jackalCount = client::defaultSandboxJackals(caves);
            assert(client::validateSandboxSessionConfig(candidate).has_value() ==
                (caves < client::minimumSandboxCaves(hunters)));
        }
        const auto defaults = client::defaultSandboxSessionConfig(hunters);
        assert(defaults.humanPlayerCount == 1);
        assert(client::validateOnlineSandboxSessionConfig(defaults).has_value());
        auto onlineDefaults = defaults;
        onlineDefaults.humanPlayerCount = 2;
        assert(!client::validateOnlineSandboxSessionConfig(onlineDefaults).has_value());
        assert(client::sandboxAiCount(defaults) == hunters - 1);
        const auto lobbySlots = client::sandboxLobbySlots(defaults);
        assert(lobbySlots.size() == hunters);
        assert(lobbySlots.front().kind == client::SandboxLobbySlotKind::Host);
        assert(std::all_of(lobbySlots.begin() + 1, lobbySlots.end(),
            [](const client::SandboxLobbySlot& slot) {
                return slot.kind == client::SandboxLobbySlotKind::Ai;
            }));
        auto mixedLobby = defaults;
        mixedLobby.humanPlayerCount = std::min<std::size_t>(3, hunters);
        const auto mixedSlots = client::sandboxLobbySlots(mixedLobby);
        assert(std::count_if(mixedSlots.begin(), mixedSlots.end(),
            [](const client::SandboxLobbySlot& slot) {
                return slot.kind == client::SandboxLobbySlotKind::Host ||
                    slot.kind == client::SandboxLobbySlotKind::EmptyHuman;
            }) == static_cast<std::ptrdiff_t>(mixedLobby.humanPlayerCount));
        assert(std::count_if(mixedSlots.begin(), mixedSlots.end(),
            [](const client::SandboxLobbySlot& slot) {
                return slot.kind == client::SandboxLobbySlotKind::Ai;
            }) == static_cast<std::ptrdiff_t>(client::sandboxAiCount(mixedLobby)));
        assert(defaults.caveCount == (hunters == 2 ? 30 : 60));
        assert(defaults.jackalCount == (hunters == 2 ? 2 : 4));
        assert(defaults.arrowSpawnIntervalRounds == 5);
        assert(defaults.startingArrows == 3 && defaults.maxArrows == 5);
    }
    assert(LocalSandboxSessionAdapter::create(
        configFor(1, 1, 2, client::ai::AiDifficulty::Medium,
            client::ai::AiBehavior::Balanced, 3)).session == nullptr);
    assert(LocalSandboxSessionAdapter::create(
        configFor(7, 1, 2, client::ai::AiDifficulty::Medium,
            client::ai::AiBehavior::Balanced, 3)).session == nullptr);
    auto reservedHumans = configFor(4, 9004, 42000,
        client::ai::AiDifficulty::Medium, client::ai::AiBehavior::Balanced, 3);
    reservedHumans.humanPlayerCount = 2;
    assert(!client::validateSandboxSessionConfig(reservedHumans).has_value());
    assert(LocalSandboxSessionAdapter::create(reservedHumans).session == nullptr);
    reservedHumans.humanPlayerCount = 0;
    assert(client::validateSandboxSessionConfig(reservedHumans).has_value());
    reservedHumans.humanPlayerCount = 5;
    assert(client::validateSandboxSessionConfig(reservedHumans).has_value());

    for (std::size_t hunterCount = 2; hunterCount <= 6; ++hunterCount) {
        const MapSeed mapSeed = hunterCount == 2
            ? MapSeed{20260816} : MapSeed{9000 + hunterCount};
        auto local = LocalSandboxSessionAdapter::create(configFor(hunterCount,
            mapSeed, MatchSeed{42000}, client::ai::AiDifficulty::Hard,
            client::ai::AiBehavior::Random, client::ai::AiSeed{77}));
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
        assert(strip.front().local && strip.front().viewed &&
            strip.front().alive == std::optional<bool>{true});
        for (std::size_t index = 1; index < strip.size(); ++index) {
            assert(strip[index].label == "BASILISK AI " + std::to_string(index + 1));
            assert(!strip[index].subtitle.empty());
            assert(strip[index].subtitle.find("Random") == std::string::npos);
            assert(!strip[index].local &&
                strip[index].alive == std::optional<bool>{true});
        }
        const PlayerId human = local.session->viewContext().localPlayer;
        const PlayerRoundSnapshot before = *local.session->snapshotFor(human);
        assert(local.session->submitAndLock(searchAction(before)));
        advanceUntilNextRound(local, human);
        for (const PublicPlayerSlot& slot : local.session->matchMetadata().players)
            assert(local.session->snapshotFor(slot.player)->round == before.round + 1);
    }

    auto first = LocalSandboxSessionAdapter::create(configFor(4, MapSeed{9004},
        MatchSeed{42000}, client::ai::AiDifficulty::Medium,
        client::ai::AiBehavior::Random, client::ai::AiSeed{8181}));
    auto second = LocalSandboxSessionAdapter::create(configFor(4, MapSeed{9004},
        MatchSeed{42000}, client::ai::AiDifficulty::Medium,
        client::ai::AiBehavior::Random, client::ai::AiSeed{8181}));
    assert(first.resolvedBehaviors == second.resolvedBehaviors);

    auto custom = configFor(3, MapSeed{1}, MatchSeed{424242},
        client::ai::AiDifficulty::Medium, client::ai::AiBehavior::Balanced,
        client::ai::AiSeed{99});
    custom.caveCount = 40;
    custom.jackalCount = 4;
    custom.arrowSpawnIntervalRounds = 3;
    custom.startingArrows = 1;
    custom.maxArrows = 2;
    auto configured = LocalSandboxSessionAdapter::create(custom);
    assert(configured.session != nullptr);
    for (const auto& slot : configured.session->matchMetadata().players) {
        const auto* snapshot = configured.session->snapshotFor(slot.player);
        assert(snapshot != nullptr);
        assert(snapshot->arrows == 1);
        assert(snapshot->maxArrows == 2);
    }

    custom.startingArrows = 3;
    assert(LocalSandboxSessionAdapter::create(custom).session == nullptr);
    custom.startingArrows = 1;
    custom.hunterCount = 4;
    custom.caveCount = 30;
    custom.jackalCount = 2;
    assert(LocalSandboxSessionAdapter::create(custom).session == nullptr);
    custom.hunterCount = 3;
    custom.caveCount = 40;
    custom.jackalCount = 5;
    assert(LocalSandboxSessionAdapter::create(custom).session == nullptr);
}
