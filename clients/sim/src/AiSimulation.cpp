#include "AiSimulation.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "basilisk/MatchState.hpp"
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

bool sameAction(const AvailableAction& a, const AvailableAction& b) {
    return a.type == b.type && a.targetCave == b.targetCave &&
        a.targetTunnel == b.targetTunnel && a.targetItem == b.targetItem &&
        a.contextualAction == b.contextualAction;
}

PlayerAction commandFor(PlayerId player, const AvailableAction& action) {
    return {player, action.type, action.targetCave, action.targetItem,
            action.contextualAction, action.targetTunnel};
}

const char* outcomeName(MatchOutcome outcome) {
    switch (outcome) {
        case MatchOutcome::None: return "none";
        case MatchOutcome::BasiliskKilled: return "basilisk_killed";
        case MatchOutcome::SimultaneousBasiliskKill: return "simultaneous_basilisk_kill";
        case MatchOutcome::EscapedWithSigil: return "escaped_with_sigil";
        case MatchOutcome::Draw: return "draw";
    }
    return "unknown";
}

std::string deathCause(const std::vector<GameEvent>& events, PlayerId player) {
    for (const auto& event : events) {
        if (event.targetPlayer != player && event.actor != player) continue;
        switch (event.type) {
            case GameEventType::PitTriggered: return "pit";
            case GameEventType::JackalKnockedOutPlayer: return "jackal";
            case GameEventType::PlayerDisconnectTimedOut: return "disconnect_timeout";
            case GameEventType::PlayerReserveExpired: return "reserve_timeout";
            default: break;
        }
    }
    for (const auto& event : events) {
        if (event.type == GameEventType::PlayerKilled && event.targetPlayer == player)
            return event.actor.has_value() ? "hunter" : "basilisk_or_hazard";
    }
    return "none";
}

void collectEvents(EpisodeTelemetry& episode, const std::vector<GameEvent>& events) {
    for (const auto& event : events) {
        auto player = [&](std::optional<PlayerId> id) -> PlayerTelemetry* {
            if (!id) return nullptr;
            const auto it = std::find_if(episode.players.begin(), episode.players.end(),
                [&](const PlayerTelemetry& value) { return value.player == *id; });
            return it == episode.players.end() ? nullptr : &*it;
        };
        switch (event.type) {
            case GameEventType::ArrowFired:
                if (auto* value = player(event.actor)) ++value->arrowsFired;
                break;
            case GameEventType::ArrowMissed:
                if (auto* value = player(event.actor)) ++value->arrowMisses;
                break;
            case GameEventType::ArrowHitPlayer:
            case GameEventType::ArrowHitJackal:
            case GameEventType::ArrowReachedBasilisk:
                if (auto* value = player(event.actor)) ++value->arrowHits;
                break;
            case GameEventType::PlayerKilled:
                if (auto* value = player(event.targetPlayer)) {
                    ++value->deaths;
                    value->deathCause = deathCause(events, value->player);
                }
                break;
            case GameEventType::SigilAcquired:
                if (auto* value = player(event.actor)) ++value->sigilRecoveries;
                break;
            default: break;
        }
    }
}

struct Agent {
    client::ai::AiConfig config;
    client::ai::AiKnowledgeState knowledge;
    HeuristicPolicy policy;
};

} // namespace

std::optional<AvailableAction> HeuristicPolicy::select(
    const PlayerRoundSnapshot& snapshot, const client::ai::AiConfig& config,
    const client::ai::AiKnowledgeState& knowledge) {
    return engine_.choose(snapshot, config, knowledge);
}

EpisodeTelemetry runEpisode(const SimulationConfig& config, std::uint64_t episodeIndex) {
    EpisodeTelemetry episode;
    episode.simulationSeed = config.seed;
    episode.episodeIndex = episodeIndex;
    episode.mapSeed = mix(config.seed ^ mix(episodeIndex * 2U + 1U));
    episode.matchSeed = mix(config.seed ^ mix(episodeIndex * 2U + 2U));
    MatchState state = MapGenerator::generate(episode.mapSeed, episode.matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, Agent> agents;
    const AgentSpec specs[] = {config.p1, config.p2};
    if (state.players.size() != 2) throw std::runtime_error("AI simulation currently requires two generated hunters");
    for (std::size_t index = 0; index < state.players.size(); ++index) {
        const PlayerId id = state.players[index].id;
        const std::uint64_t aiSeed = mix(config.seed ^ mix(episodeIndex + 1U) ^ mix(id));
        const auto resolved = client::ai::resolveBehavior(specs[index].behavior, aiSeed);
        agents.emplace(id, Agent{{specs[index].difficulty, resolved, id, aiSeed}, {}, {}});
        episode.players.push_back({id, specs[index], resolved, aiSeed});
    }

    std::vector<GameEvent> previousEvents;
    while (state.result.status == MatchStatus::Active && state.round <= config.maxRounds) {
        const RoundNumber before = state.round;
        bool submittedAny = false;
        for (const auto& playerState : state.players) {
            if (!playerState.alive) continue;
            auto& agent = agents.at(playerState.id);
            const auto snapshot = SnapshotSystem::buildForPlayer(
                state, playerState.id, previousEvents);
            agent.knowledge.observe(snapshot);
            const auto selected = agent.policy.select(snapshot, agent.config, agent.knowledge);
            if (!selected) throw std::runtime_error("AI policy returned no action in an actionable round");
            if (std::none_of(snapshot.availableActions.begin(), snapshot.availableActions.end(),
                    [&](const AvailableAction& legal) { return sameAction(legal, *selected); }))
                throw std::runtime_error("AI policy returned an action outside its player-safe legal action set: player " +
                    std::to_string(playerState.id) + " round " + std::to_string(state.round) +
                    " type " + std::to_string(static_cast<int>(selected->type)) +
                    " legal-count " + std::to_string(snapshot.availableActions.size()));
            if (!coordinator.submitAction(commandFor(playerState.id, *selected)))
                throw std::runtime_error("MatchCoordinator rejected a legal AI action");
            agent.knowledge.recordDecision(*selected);
            auto& metrics = *std::find_if(episode.players.begin(), episode.players.end(),
                [&](const PlayerTelemetry& value) { return value.player == playerState.id; });
            switch (selected->type) {
                case ActionType::Move: ++metrics.moves; break;
                case ActionType::Search: ++metrics.searches; break;
                case ActionType::Shoot: ++metrics.shoots; break;
                case ActionType::UseItem: ++metrics.itemUses; break;
                case ActionType::Contextual: ++metrics.contextualActions; break;
            }
            submittedAny = true;
        }
        if (!submittedAny) break;
        for (const auto& playerState : state.players)
            if (playerState.alive && !coordinator.lockAction(playerState.id))
                throw std::runtime_error("MatchCoordinator rejected a submitted AI lock");

        if (const ActiveClash* clash = coordinator.activeClash()) {
            ++episode.clashes;
            // There is no wall-clock reaction delay in headless simulation.
            // Independent AI streams deterministically decide which participant
            // reaches the server first.
            const PlayerId first = (mix(agents.at(clash->participants[0]).config.seed ^
                state.round ^ clash->id) & 1U) == 0U
                ? clash->participants[0] : clash->participants[1];
            const auto result = coordinator.submitClashResponse(
                first, clash->id, clash->challengeWord);
            if (result != ClashSubmissionResult::Resolved)
                throw std::runtime_error("AI clash response failed to resolve");
            auto& winner = *std::find_if(episode.players.begin(), episode.players.end(),
                [&](const PlayerTelemetry& value) { return value.player == first; });
            ++winner.clashWins;
        }
        previousEvents = coordinator.authoritativeEvents();
        collectEvents(episode, previousEvents);
        if (state.result.status == MatchStatus::Active && state.round == before)
            throw std::runtime_error("Authoritative AI round did not advance");
    }

    episode.status = state.result.status;
    episode.outcome = state.result.outcome;
    episode.winner = state.result.winner;
    episode.rounds = state.round;
    episode.extractionWin = state.result.outcome == MatchOutcome::EscapedWithSigil;
    episode.basiliskKill = state.result.outcome == MatchOutcome::BasiliskKilled ||
        state.result.outcome == MatchOutcome::SimultaneousBasiliskKill;
    for (auto& metrics : episode.players) {
        const auto player = std::find_if(state.players.begin(), state.players.end(),
            [&](const PlayerState& value) { return value.id == metrics.player; });
        metrics.alive = player != state.players.end() && player->alive;
        metrics.health = player == state.players.end() ? 0 : player->health;
        metrics.arrows = player == state.players.end() ? 0 : player->arrows;
    }
    return episode;
}

void BatchAggregate::add(const EpisodeTelemetry& episode) {
    ++matches;
    totalRounds += episode.rounds;
    totalClashes += episode.clashes;
    if (episode.status == MatchStatus::Completed) ++completed; else ++unfinished;
    if (episode.outcome == MatchOutcome::Draw || !episode.winner) ++draws;
    else if (!episode.players.empty() && episode.winner == episode.players[0].player) ++p1Wins;
    else if (episode.players.size() > 1 && episode.winner == episode.players[1].player) ++p2Wins;
    if (episode.basiliskKill) ++basiliskWins;
    if (episode.extractionWin) ++extractionWins;
    for (const auto& player : episode.players) {
        totalMoves += player.moves; totalSearches += player.searches;
        totalShoots += player.shoots; totalItemUses += player.itemUses;
        totalArrowHits += player.arrowHits; totalArrowMisses += player.arrowMisses;
        if (player.deaths != 0) deathCauses[player.deathCause] += player.deaths;
    }
}

std::string episodeJson(const EpisodeTelemetry& episode) {
    std::ostringstream out;
    out << "{\"schemaVersion\":" << kAiEpisodeSchemaVersion
        << ",\"simulationSeed\":" << episode.simulationSeed
        << ",\"episodeIndex\":" << episode.episodeIndex
        << ",\"mapSeed\":" << episode.mapSeed
        << ",\"matchSeed\":" << episode.matchSeed
        << ",\"status\":\"" << (episode.status == MatchStatus::Completed ? "completed" : "active")
        << "\",\"outcome\":\"" << outcomeName(episode.outcome) << "\",\"winner\":";
    if (episode.winner) out << *episode.winner; else out << "null";
    out << ",\"rounds\":" << episode.rounds << ",\"clashes\":" << episode.clashes
        << ",\"extractionWin\":" << (episode.extractionWin ? "true" : "false")
        << ",\"basiliskKill\":" << (episode.basiliskKill ? "true" : "false")
        << ",\"players\":[";
    for (std::size_t i = 0; i < episode.players.size(); ++i) {
        if (i) out << ',';
        const auto& p = episode.players[i];
        out << "{\"player\":" << p.player << ",\"difficulty\":\""
            << client::ai::difficultyName(p.requested.difficulty)
            << "\",\"requestedBehavior\":\"" << client::ai::behaviorName(p.requested.behavior)
            << "\",\"resolvedBehavior\":\"" << client::ai::behaviorName(p.resolvedBehavior)
            << "\",\"aiSeed\":" << p.aiSeed << ",\"alive\":" << (p.alive ? "true" : "false")
            << ",\"health\":" << p.health << ",\"arrows\":" << p.arrows
            << ",\"actions\":{\"move\":" << p.moves << ",\"search\":" << p.searches
            << ",\"shoot\":" << p.shoots << ",\"useItem\":" << p.itemUses
            << ",\"contextual\":" << p.contextualActions << "}"
            << ",\"arrowsTelemetry\":{\"fired\":" << p.arrowsFired
            << ",\"missed\":" << p.arrowMisses << ",\"hit\":" << p.arrowHits << "}"
            << ",\"clashWins\":" << p.clashWins << ",\"deaths\":" << p.deaths
            << ",\"deathCause\":\"" << p.deathCause << "\",\"sigilRecoveries\":"
            << p.sigilRecoveries << '}';
    }
    out << "]}";
    return out.str();
}

void writeSummary(std::ostream& out, const BatchAggregate& a, double seconds) {
    const double matches = static_cast<double>(std::max<std::uint64_t>(1, a.matches));
    out << "Basilisk AI simulation\n"
        << "  matches: " << a.matches << " (completed " << a.completed
        << ", unfinished " << a.unfinished << ")\n"
        << "  P1 wins: " << a.p1Wins << "  P2 wins: " << a.p2Wins
        << "  draws: " << a.draws << "\n"
        << "  average rounds: " << std::fixed << std::setprecision(2)
        << static_cast<double>(a.totalRounds) / matches << "\n"
        << "  win causes: Basilisk " << a.basiliskWins << "  extraction " << a.extractionWins << "\n"
        << "  death causes:";
    if (a.deathCauses.empty()) out << " none";
    for (const auto& [cause, count] : a.deathCauses) out << ' ' << cause << ' ' << count;
    out << "\n"
        << "  per-match actions: move " << a.totalMoves / matches << "  search "
        << a.totalSearches / matches << "  shoot " << a.totalShoots / matches
        << "  item " << a.totalItemUses / matches << "\n"
        << "  per-match arrows: hit " << a.totalArrowHits / matches << "  miss "
        << a.totalArrowMisses / matches << "  clashes " << a.totalClashes / matches << "\n"
        << "  throughput: " << (seconds > 0.0 ? matches / seconds : 0.0) << " matches/sec\n";
}

} // namespace basilisk::sim
