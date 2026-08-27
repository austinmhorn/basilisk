#include "SandboxStress.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "basilisk/MatchState.hpp"
#include "basilisk/client/SandboxConfiguration.hpp"
#include "basilisk/client/ai/AiDecisionEngine.hpp"
#include "basilisk/client/ai/AiKnowledgeState.hpp"
#include "basilisk/systems/MatchCoordinator.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/world/MapGenerator.hpp"

namespace basilisk::sim {
namespace {

std::uint64_t mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[noreturn]] void fail(std::size_t players, std::size_t episode,
    RoundNumber round, const std::string& message) {
    std::ostringstream out;
    out << "Sandbox stress failure: players=" << players
        << " episode=" << episode << " round=" << round << ": " << message;
    throw std::runtime_error(out.str());
}

void require(bool condition, std::size_t players, std::size_t episode,
    RoundNumber round, const std::string& message) {
    if (!condition) fail(players, episode, round, message);
}

PlayerAction commandFor(PlayerId player, const AvailableAction& action) {
    return {player, action.type, action.targetCave, action.targetItem,
        action.contextualAction, action.targetTunnel};
}

void verifyState(const MatchState& state, const MatchCoordinator& coordinator,
    std::size_t expectedPlayers, std::size_t episode) {
    require(state.players.size() == expectedPlayers, expectedPlayers, episode,
        state.round, "participant roster size changed");
    std::set<PlayerId> ids;
    std::set<CaveId> livingCaves;
    std::size_t carriers = 0;
    std::optional<PlayerId> carrier;
    for (const PlayerState& player : state.players) {
        require(ids.insert(player.id).second, expectedPlayers, episode, state.round,
            "duplicate participant id");
        if (player.alive) {
            require(state.world.contains(player.cave), expectedPlayers, episode,
                state.round, "living participant has invalid cave");
            require(livingCaves.insert(player.cave).second, expectedPlayers, episode,
                state.round, "duplicate living hunter occupancy");
        }
        if (player.heldSigilFrom.has_value()) {
            ++carriers;
            carrier = player.id;
            require(player.alive, expectedPlayers, episode, state.round,
                "dead participant carries a Sigil");
        }
        const PlayerRoundSnapshot snapshot = SnapshotSystem::buildForPlayer(
            state, player.id, coordinator.authoritativeEvents());
        require(snapshot.player == player.id, expectedPlayers, episode, state.round,
            "snapshot belongs to an invalid participant");
        require(snapshot.round == state.round, expectedPlayers, episode, state.round,
            "snapshot round does not match authoritative state");
    }
    require(carriers <= 1, expectedPlayers, episode, state.round,
        "multiple active Sigil carriers");
    if (carriers == 1) {
        require(state.extraction.active && state.extraction.cave.has_value() &&
            state.extraction.sigilHolder == carrier, expectedPlayers, episode,
            state.round, "extraction does not match its Sigil carrier");
    } else {
        require(!state.extraction.sigilHolder.has_value(), expectedPlayers, episode,
            state.round, "extraction names a missing Sigil carrier");
    }
    if (const ActiveClash* clash = coordinator.activeClash()) {
        std::set<PlayerId> participants;
        require(clash->participants.size() >= 2, expectedPlayers, episode,
            state.round, "Clash has fewer than two participants");
        for (const PlayerId player : clash->participants) {
            require(ids.contains(player), expectedPlayers, episode, state.round,
                "Clash contains an unknown participant");
            require(participants.insert(player).second, expectedPlayers, episode,
                state.round, "duplicate Clash participant membership");
        }
    }
    if (state.result.status == MatchStatus::Active) {
        require(state.result.outcome == MatchOutcome::None &&
            !state.result.winner.has_value(), expectedPlayers, episode, state.round,
            "active match has terminal result data");
    } else {
        require(state.result.outcome != MatchOutcome::None, expectedPlayers, episode,
            state.round, "completed match has no outcome");
        require(coordinator.activeClash() == nullptr, expectedPlayers, episode,
            state.round, "terminal match retained a Clash");
        if (state.result.outcome == MatchOutcome::Draw ||
            state.result.outcome == MatchOutcome::SimultaneousBasiliskKill) {
            require(!state.result.winner.has_value(), expectedPlayers, episode,
                state.round, "draw has a winner");
        } else {
            require(state.result.winner.has_value() && ids.contains(*state.result.winner),
                expectedPlayers, episode, state.round, "winner is not a participant");
        }
    }
}

struct StressAgent {
    client::ai::AiConfig config;
    client::ai::AiDecisionEngine engine;
    client::ai::AiKnowledgeState knowledge;
    std::optional<RoundNumber> decisionRound;
};

SandboxStressCountResult runCount(const SandboxStressConfig& config,
    std::size_t playerCount, std::ostream* progress) {
    SandboxStressCountResult result;
    result.players = playerCount;
    result.matches = config.matchesPerPlayerCount;
    std::optional<MatchState> sixPlayerBase;
    if (playerCount == 6) {
        std::vector<PlayerId> roster;
        for (PlayerId player = 1; player <= 6; ++player) roster.push_back(player);
        sixPlayerBase = MapGenerator::generate(MapSeed{9006}, MatchSeed{42000},
            roster, {}, client::sandboxMapConfig(playerCount));
    }
    for (std::size_t episode = 0; episode < config.matchesPerPlayerCount; ++episode) {
        const std::uint64_t episodeSeed = mix(config.seed ^
            mix(playerCount * 1000000ULL + episode));
        std::vector<PlayerId> roster;
        for (std::size_t index = 0; index < playerCount; ++index)
            roster.push_back(static_cast<PlayerId>(index + 1));
        MatchState state = sixPlayerBase.value_or(MatchState{});
        bool generated = sixPlayerBase.has_value();
        if (generated) state.matchSeed = MatchSeed{mix(episodeSeed ^ 0x4d41544348ULL)};
        for (std::uint64_t retry = 0; retry < 64 && !generated; ++retry) {
            try {
                state = MapGenerator::generate(
                    MapSeed{playerCount == 6
                        ? 9006ULL
                        : mix(episodeSeed ^ 0x4d4150ULL ^ mix(retry))},
                    MatchSeed{mix(episodeSeed ^ 0x4d41544348ULL)}, roster, {},
                    client::sandboxMapConfig(playerCount));
                generated = true;
                result.generationRetries += retry;
            } catch (const std::runtime_error&) {
                // Procedural generation has a deliberate bounded-attempt
                // contract. Advance deterministically to the next corpus seed.
            }
        }
        require(generated, playerCount, episode, 0,
            "unable to generate a valid map after deterministic retries");
        MatchCoordinator coordinator{state};
        std::unordered_map<PlayerId, StressAgent> agents;
        for (const PlayerId player : roster) {
            const auto seed = client::ai::AiSeed{mix(episodeSeed ^ mix(player))};
            const auto requested = static_cast<client::ai::AiBehavior>(
                (episode + player) % 6U);
            agents.emplace(player, StressAgent{{
                client::ai::AiDifficulty::Hard,
                client::ai::resolveBehavior(requested, seed), player, seed}});
        }
        std::vector<GameEvent> previousEvents;
        verifyState(state, coordinator, playerCount, episode);
        while (state.result.status == MatchStatus::Active) {
            require(state.round <= config.maxRounds, playerCount, episode, state.round,
                "match exceeded maximum round count");
            const RoundNumber before = state.round;
            std::vector<PlayerId> acting;
            for (const PlayerState& player : state.players) {
                if (!player.alive) continue;
                auto& agent = agents.at(player.id);
                require(agent.decisionRound != state.round, playerCount, episode,
                    state.round, "AI received duplicate decision opportunity");
                const PlayerRoundSnapshot snapshot = SnapshotSystem::buildForPlayer(
                    state, player.id, previousEvents);
                agent.knowledge.observe(snapshot);
                const auto action = agent.engine.choose(
                    snapshot, agent.config, agent.knowledge);
                require(action.has_value(), playerCount, episode, state.round,
                    "living AI has no legal decision");
                const bool legal = std::find_if(snapshot.availableActions.begin(),
                    snapshot.availableActions.end(), [&](const AvailableAction& candidate) {
                        return candidate.type == action->type &&
                            candidate.targetCave == action->targetCave &&
                            candidate.targetTunnel == action->targetTunnel &&
                            candidate.targetItem == action->targetItem &&
                            candidate.contextualAction == action->contextualAction;
                    }) != snapshot.availableActions.end();
                require(legal, playerCount, episode, state.round,
                    "AI selected action outside its player-safe legal set");
                require(coordinator.submitAction(commandFor(player.id, *action)),
                    playerCount, episode, state.round,
                    "coordinator rejected legal AI action");
                agent.knowledge.recordDecision(*action);
                agent.decisionRound = state.round;
                acting.push_back(player.id);
            }
            require(!acting.empty(), playerCount, episode, state.round,
                "active match has no living actionable participant");
            for (const PlayerId player : acting)
                require(coordinator.lockAction(player), playerCount, episode,
                    before, "coordinator rejected AI lock");

            while (const ActiveClash* clash = coordinator.activeClash()) {
                ++result.clashes;
                verifyState(state, coordinator, playerCount, episode);
                const PlayerId responder = clash->participants[
                    static_cast<std::size_t>(mix(episodeSeed ^ clash->id) %
                        clash->participants.size())];
                const ClashId clashId = clash->id;
                const std::string word = clash->challengeWord;
                require(coordinator.submitClashResponse(responder, clashId, word) ==
                    ClashSubmissionResult::Resolved, playerCount, episode,
                    before, "valid Clash response was rejected");
            }
            previousEvents = coordinator.authoritativeEvents();
            require(state.result.status == MatchStatus::Completed ||
                state.round == before + 1, playerCount, episode, before,
                "round stalled or advanced more than once");
            verifyState(state, coordinator, playerCount, episode);
        }
        ++result.completed;
        if (state.result.outcome == MatchOutcome::Draw ||
            state.result.outcome == MatchOutcome::SimultaneousBasiliskKill)
            ++result.draws;
        if (state.result.outcome == MatchOutcome::BasiliskKilled)
            ++result.basiliskWins;
        if (state.result.outcome == MatchOutcome::EscapedWithSigil)
            ++result.extractionWins;
        result.rounds += state.round;
        if (progress != nullptr && (episode + 1) % 50 == 0)
            *progress << playerCount << " players: " << episode + 1 << "/"
                << config.matchesPerPlayerCount << "\n";
    }
    return result;
}

} // namespace

SandboxStressResult runSandboxStress(
    const SandboxStressConfig& config, std::ostream* progress) {
    if (config.matchesPerPlayerCount == 0 || config.maxRounds == 0)
        throw std::invalid_argument("Sandbox stress counts must be positive");
    SandboxStressResult result;
    for (std::size_t players = 2; players <= 6; ++players)
        result.byPlayerCount.emplace(players, runCount(config, players, progress));
    return result;
}

} // namespace basilisk::sim
