#include "AiSimulation.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <memory>

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

const char* actionTypeName(ActionType type) {
    switch (type) {
        case ActionType::Move: return "move";
        case ActionType::Search: return "search";
        case ActionType::Shoot: return "shoot";
        case ActionType::UseItem: return "use_item";
        case ActionType::Contextual: return "contextual";
    }
    return "unknown";
}

void writeOptional(std::ostream& out, const auto& value) {
    if (value) out << *value; else out << "null";
}

void writeAction(std::ostream& out, const EncodedAction& encoded) {
    const auto& action = encoded.action;
    out << "{\"schemaVersion\":" << kAiActionSchemaVersion
        << ",\"legalIndex\":" << encoded.legalIndex
        << ",\"type\":\"" << actionTypeName(action.type) << "\",\"targetCave\":";
    writeOptional(out, action.targetCave);
    out << ",\"targetTunnel\":"; writeOptional(out, action.targetTunnel);
    out << ",\"targetItem\":";
    if (action.targetItem) out << static_cast<int>(*action.targetItem); else out << "null";
    out << ",\"contextualAction\":";
    if (action.contextualAction) out << static_cast<int>(*action.contextualAction); else out << "null";
    out << '}';
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
    AgentSpec requested;
    PolicyKind policyKind{PolicyKind::Heuristic};
    client::ai::AiConfig config;
    client::ai::AiKnowledgeState knowledge;
    std::unique_ptr<AgentPolicy> policy;
    std::optional<EncodedAction> previousAction;
    struct Pending {
        RoundNumber round{};
        AgentObservation observation;
        AgentDecision decision;
        EncodedAction chosen;
    };
    std::optional<Pending> pending;
};

AgentObservation makeObservation(const PlayerRoundSnapshot& snapshot,
    const client::ai::AiKnowledgeState& knowledge,
    const std::optional<EncodedAction>& previousAction,
    const client::ai::AiConfig& config) {
    return client::ai::makePolicyObservation(
        snapshot, knowledge, config, previousAction);
}

RewardComponents rewardFor(const MatchState& state, PlayerId player) {
    RewardComponents reward;
    if (state.result.status == MatchStatus::Completed) {
        if (state.result.outcome == MatchOutcome::Draw || !state.result.winner)
            reward.draw = 0.0;
        else if (state.result.winner == player) reward.win = 1.0;
        else reward.loss = -1.0;
        return reward;
    }
    const auto it = std::find_if(state.players.begin(), state.players.end(),
        [&](const PlayerState& candidate) { return candidate.id == player; });
    if (it != state.players.end() && !it->alive) reward.loss = -1.0;
    return reward;
}

void flushPending(const SimulationConfig& config, const EpisodeTelemetry& episode,
    const MatchState& state, Agent& agent, AgentObservation next) {
    if (!agent.pending || config.transitionSink == nullptr) return;
    const bool alive = std::any_of(state.players.begin(), state.players.end(),
        [&](const PlayerState& player) { return player.id == agent.config.player && player.alive; });
    Transition transition;
    transition.simulationSeed = episode.simulationSeed;
    transition.episodeIndex = episode.episodeIndex;
    transition.mapSeed = episode.mapSeed;
    transition.matchSeed = episode.matchSeed;
    transition.round = agent.pending->round;
    transition.player = agent.config.player;
    transition.policy = agent.policyKind;
    transition.config = agent.requested;
    transition.resolvedBehavior = agent.config.behavior;
    transition.observation = std::move(agent.pending->observation);
    transition.decision = std::move(agent.pending->decision);
    transition.chosenAction = agent.pending->chosen;
    transition.reward = rewardFor(state, agent.config.player);
    transition.nextObservation = std::move(next);
    transition.terminal = state.result.status == MatchStatus::Completed || !alive;
    transition.outcome = state.result.outcome;
    transition.winner = state.result.winner;
    config.transitionSink->write(transition);
    agent.pending.reset();
}

} // namespace

AgentDecision RandomPolicy::select(const AgentObservation& observation,
    const client::ai::AiConfig&) {
    if (observation.legalActions.empty()) return {0, "no-action"};
    return {static_cast<std::size_t>(rng_.range(
        0, static_cast<int>(observation.legalActions.size()) - 1)), "uniform-random"};
}

const EncodedAction& resolveDecision(const AgentObservation& observation,
    const AgentDecision& decision, const client::ai::AiConfig& config) {
    return client::ai::resolvePolicyDecision(
        observation, decision, config);
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
    const PolicyKind policyKinds[] = {config.p1Policy, config.p2Policy};
    if (state.players.size() != 2) throw std::runtime_error("AI simulation currently requires two generated hunters");
    for (std::size_t index = 0; index < state.players.size(); ++index) {
        const PlayerId id = state.players[index].id;
        const std::uint64_t aiSeed = mix(config.seed ^ mix(episodeIndex + 1U) ^ mix(id));
        const auto resolved = client::ai::resolveBehavior(specs[index].behavior, aiSeed);
        std::unique_ptr<AgentPolicy> policy;
        if (policyKinds[index] == PolicyKind::Random)
            policy = std::make_unique<RandomPolicy>(mix(aiSeed ^ 0x504f4c494359ULL));
        else policy = std::make_unique<HeuristicPolicy>();
        Agent agent;
        agent.requested = specs[index];
        agent.policyKind = policyKinds[index];
        agent.config = {specs[index].difficulty, resolved, id, aiSeed};
        agent.policy = std::move(policy);
        agents.emplace(id, std::move(agent));
        episode.players.push_back({id, specs[index], resolved, aiSeed});
    }

    std::vector<GameEvent> previousEvents;
    while (state.result.status == MatchStatus::Active && state.round <= config.maxRounds) {
        const RoundNumber before = state.round;
        bool submittedAny = false;
        for (const auto& playerState : state.players) {
            auto& agent = agents.at(playerState.id);
            const auto snapshot = SnapshotSystem::buildForPlayer(
                state, playerState.id, previousEvents);
            agent.knowledge.observe(snapshot);
            AgentObservation observation = makeObservation(
                snapshot, agent.knowledge, agent.previousAction, agent.config);
            flushPending(config, episode, state, agent, observation);
            if (!playerState.alive) continue;
            const AgentDecision decision = agent.policy->select(observation, agent.config);
            const EncodedAction selected = client::ai::resolvePolicyDecision(
                observation, decision, agent.config);
            if (!coordinator.submitAction(commandFor(playerState.id, selected.action)))
                throw std::runtime_error("MatchCoordinator rejected a legal AI action");
            agent.knowledge.recordDecision(selected.action);
            agent.pending = Agent::Pending{state.round, observation, decision, selected};
            agent.previousAction = selected;
            auto& metrics = *std::find_if(episode.players.begin(), episode.players.end(),
                [&](const PlayerTelemetry& value) { return value.player == playerState.id; });
            switch (selected.action.type) {
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
        if (state.result.status == MatchStatus::Completed) {
            for (const auto& playerState : state.players) {
                auto& agent = agents.at(playerState.id);
                auto snapshot = SnapshotSystem::buildForPlayer(
                    state, playerState.id, previousEvents);
                agent.knowledge.observe(snapshot);
                flushPending(config, episode, state, agent,
                    makeObservation(snapshot, agent.knowledge,
                        agent.previousAction, agent.config));
            }
        }
        if (state.result.status == MatchStatus::Active && state.round == before)
            throw std::runtime_error("Authoritative AI round did not advance");
    }

    // A configured max-round guard is a dataset boundary, not a gameplay
    // terminal. Link any final pending decision to the latest safe snapshot.
    for (const auto& playerState : state.players) {
        auto& agent = agents.at(playerState.id);
        if (!agent.pending) continue;
        auto snapshot = SnapshotSystem::buildForPlayer(
            state, playerState.id, previousEvents);
        agent.knowledge.observe(snapshot);
        flushPending(config, episode, state, agent,
            makeObservation(snapshot, agent.knowledge,
                agent.previousAction, agent.config));
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

const char* policyName(PolicyKind policy) noexcept {
    return policy == PolicyKind::Random ? "random" : "heuristic";
}

std::string observationJson(const AgentObservation& observation) {
    const auto& snapshot = observation.sourceSnapshot;
    const auto& knowledge = observation.knowledge;
    std::ostringstream out;
    out << "{\"schemaVersion\":" << observation.schemaVersion
        << ",\"round\":" << snapshot.round << ",\"player\":" << snapshot.player
        << ",\"health\":" << snapshot.health << ",\"maxHealth\":" << snapshot.maxHealth
        << ",\"arrows\":" << snapshot.arrows << ",\"maxArrows\":" << snapshot.maxArrows
        << ",\"alive\":" << (snapshot.alive ? "true" : "false")
        << ",\"currentCave\":" << snapshot.currentCave << ",\"inventory\":[";
    for (std::size_t index = 0; index < snapshot.inventory.items.size(); ++index) {
        if (index) out << ',';
        out << static_cast<int>(snapshot.inventory.items[index]);
    }
    out << "],\"inventoryCapacity\":" << snapshot.inventory.capacity
        << ",\"map\":[";
    for (std::size_t caveIndex = 0; caveIndex < snapshot.map.caves.size(); ++caveIndex) {
        if (caveIndex) out << ',';
        const auto& cave = snapshot.map.caves[caveIndex];
        out << "{\"cave\":" << cave.cave << ",\"surveyed\":"
            << (cave.surveyed ? "true" : "false") << ",\"confirmedPit\":"
            << (observation.knowledgeState.isConfirmedPit(cave.cave) ? "true" : "false")
            << ",\"pitCandidate\":"
            << (observation.knowledgeState.isPitCandidate(cave.cave) ? "true" : "false")
            << ",\"exits\":[";
        for (std::size_t exitIndex = 0; exitIndex < cave.exits.size(); ++exitIndex) {
            if (exitIndex) out << ',';
            const auto& exit = cave.exits[exitIndex];
            out << "{\"tunnel\":" << exit.id << ",\"destination\":";
            writeOptional(out, exit.destination);
            out << ",\"strongColdDraft\":" << (exit.strongColdDraft ? "true" : "false") << '}';
        }
        out << "]}";
    }
    out << "],\"observations\":[";
    for (std::size_t index = 0; index < snapshot.observations.size(); ++index) {
        if (index) out << ',';
        const auto& item = snapshot.observations[index];
        out << "{\"type\":" << static_cast<int>(item.type) << ",\"cave\":";
        writeOptional(out, item.cave);
        out << ",\"otherPlayer\":"; writeOptional(out, item.otherPlayer);
        out << ",\"amount\":" << item.amount << ",\"item\":";
        if (item.itemType) out << static_cast<int>(*item.itemType); else out << "null";
        out << ",\"tunnel\":"; writeOptional(out, item.tunnel);
        out << '}';
    }
    out << "],\"knowledge\":{\"previousCave\":";
    writeOptional(out, knowledge.previousCave);
    out << ",\"pitWarning\":" << (knowledge.pitWarning ? "true" : "false")
        << ",\"basiliskAdjacentWarning\":" << (knowledge.basiliskAdjacentWarning ? "true" : "false")
        << ",\"basiliskDistantWarning\":" << (knowledge.basiliskDistantWarning ? "true" : "false")
        << ",\"jackalWarning\":" << (knowledge.jackalWarning ? "true" : "false")
        << ",\"rivalWarning\":" << (knowledge.rivalWarning ? "true" : "false")
        << ",\"basiliskCandidateCount\":" << knowledge.basiliskCandidateCount
        << ",\"unresolvedPitCandidates\":" << knowledge.unresolvedPitCandidates
        << ",\"repeatedSearches\":" << knowledge.repeatedSearches
        << ",\"materialRevision\":" << knowledge.materialRevision << '}'
        << ",\"objective\":{\"recoverableSigil\":"
        << (snapshot.recoverableRivalSigilAvailable ? "true" : "false")
        << ",\"hasSigil\":" << (snapshot.hasHunterSigil ? "true" : "false")
        << ",\"extractionCave\":";
    writeOptional(out, snapshot.extractionCave);
    out << "},\"previousAction\":";
    if (observation.previousAction) writeAction(out, *observation.previousAction); else out << "null";
    out << ",\"legalActions\":[";
    for (std::size_t index = 0; index < observation.legalActions.size(); ++index) {
        if (index) out << ',';
        writeAction(out, observation.legalActions[index]);
    }
    out << "]}";
    return out.str();
}

std::string transitionJson(const Transition& transition) {
    std::ostringstream out;
    out << "{\"schemaVersion\":" << transition.schemaVersion
        << ",\"simulationSeed\":" << transition.simulationSeed
        << ",\"episodeIndex\":" << transition.episodeIndex
        << ",\"mapSeed\":" << transition.mapSeed
        << ",\"matchSeed\":" << transition.matchSeed
        << ",\"round\":" << transition.round << ",\"player\":" << transition.player
        << ",\"policy\":\"" << policyName(transition.policy)
        << "\",\"difficulty\":\"" << client::ai::difficultyName(transition.config.difficulty)
        << "\",\"requestedBehavior\":\"" << client::ai::behaviorName(transition.config.behavior)
        << "\",\"resolvedBehavior\":\"" << client::ai::behaviorName(transition.resolvedBehavior)
        << "\",\"observation\":" << observationJson(transition.observation)
        << ",\"decision\":{\"legalActionIndex\":" << transition.decision.legalActionIndex
        << ",\"metadata\":\"" << transition.decision.policyMetadata << "\"}"
        << ",\"chosenAction\":";
    writeAction(out, transition.chosenAction);
    out << ",\"reward\":{\"total\":" << transition.reward.total()
        << ",\"win\":" << transition.reward.win << ",\"loss\":" << transition.reward.loss
        << ",\"draw\":" << transition.reward.draw << "}"
        << ",\"nextObservation\":" << observationJson(transition.nextObservation)
        << ",\"terminal\":" << (transition.terminal ? "true" : "false")
        << ",\"outcome\":\"" << outcomeName(transition.outcome) << "\",\"winner\":";
    writeOptional(out, transition.winner);
    out << '}';
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
