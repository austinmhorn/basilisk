#include "basilisk/systems/ObservationSystem.hpp"

#include <algorithm>
#include <optional>
#include <queue>
#include <unordered_map>

#include "ExtractionVisibility.hpp"

namespace basilisk {
namespace {

const PlayerState* findPlayer(const MatchState& state, PlayerId id) {
    const auto it = std::find_if(state.players.begin(), state.players.end(),
        [id](const PlayerState& player) { return player.id == id; });
    return it == state.players.end() ? nullptr : &*it;
}

std::optional<int> distanceBetween(const WorldGraph& world, CaveId start, CaveId target) {
    if (!world.contains(start) || !world.contains(target)) return std::nullopt;
    if (start == target) return 0;
    std::queue<CaveId> frontier;
    std::unordered_map<CaveId, int> distances;
    frontier.push(start);
    distances.emplace(start, 0);
    while (!frontier.empty()) {
        const CaveId current = frontier.front(); frontier.pop();
        const int currentDistance = distances.at(current);
        for (const CaveId next : world.cave(current).connections) {
            if (distances.contains(next)) continue;
            const int nextDistance = currentDistance + 1;
            if (next == target) return nextDistance;
            distances.emplace(next, nextDistance);
            frontier.push(next);
        }
    }
    return std::nullopt;
}

void addEnvironmentalClues(const MatchState& state, const PlayerState& viewer,
                           const std::vector<GameEvent>& events,
                           std::vector<PlayerObservation>& observations) {
    if (!viewer.alive) return;
    for (const auto& player : state.players) {
        if (!player.alive || player.id == viewer.id) continue;
        if (state.world.areConnected(viewer.cave, player.cave)) {
            observations.push_back(PlayerObservation{ObservationType::RivalNearby, viewer.id}); break;
        }
    }
    for (const auto& pit : state.pits) {
        if (pit.active && state.world.areConnected(viewer.cave, pit.cave)) {
            observations.push_back(PlayerObservation{ObservationType::PitNearby, viewer.id}); break;
        }
    }
    for (const auto& jackal : state.jackals) {
        if (state.world.areConnected(viewer.cave, jackal.cave)) {
            observations.push_back(PlayerObservation{ObservationType::JackalNearby, viewer.id}); break;
        }
    }
    if (state.basilisk.alive && state.world.areConnected(viewer.cave, state.basilisk.cave)) {
        observations.push_back(PlayerObservation{
            state.basilisk.behavior == BasiliskBehavior::Lurker ? ObservationType::BasiliskNearbySubtle : ObservationType::BasiliskNearby,
            viewer.id});
    }
    if (state.basilisk.alive && state.basilisk.behavior == BasiliskBehavior::Restless) {
        const bool movedThisRound = std::any_of(events.begin(), events.end(), [](const GameEvent& event) {
            return event.type == GameEventType::BasiliskMoved && event.basiliskBehavior == BasiliskBehavior::Restless;
        });
        if (movedThisRound) {
            const auto distance = distanceBetween(state.world, viewer.cave, state.basilisk.cave);
            if (distance.has_value() && *distance == 2)
                observations.push_back(PlayerObservation{ObservationType::RestlessBasiliskNoise, viewer.id});
        }
    }
    if (state.basilisk.alive && state.basilisk.behavior == BasiliskBehavior::Enraged && state.basilisk.lastCave.has_value())
        observations.push_back(PlayerObservation{ObservationType::EnragedLastKnownCave, viewer.id, state.basilisk.lastCave});
}

void addEventFeedback(const MatchState& state, const PlayerState& viewer,
                      const std::vector<GameEvent>& events,
                      std::vector<PlayerObservation>& observations) {
    bool viewerReachedBasilisk = false;
    bool viewerKilledBasilisk = false;
    int jackalArrowsStolen = 0;
    const bool viewerFellIntoPit = std::any_of(events.begin(), events.end(), [&](const GameEvent& event) {
        return event.type == GameEventType::PitTriggered && event.targetPlayer == viewer.id;
    });
    for (const auto& event : events) {
        if (event.type == GameEventType::ArrowReachedBasilisk && event.actor == viewer.id) viewerReachedBasilisk = true;
        if (event.type == GameEventType::BasiliskKilled && event.actor == viewer.id) viewerKilledBasilisk = true;
        switch (event.type) {
            case GameEventType::ArrowHitPlayer:
                if (event.targetPlayer == viewer.id) observations.push_back(PlayerObservation{ObservationType::ArrowHitYou, viewer.id, event.cave, event.actor, event.amount});
                break;
            case GameEventType::PlayerDamaged:
                if (event.targetPlayer == viewer.id) observations.push_back(PlayerObservation{ObservationType::YouWereDamaged, viewer.id, event.cave, event.actor, event.amount});
                break;
            case GameEventType::PitTriggered:
                if (event.targetPlayer == viewer.id) observations.push_back(PlayerObservation{ObservationType::FellIntoPit, viewer.id, event.cave});
                break;
            case GameEventType::JackalRobbedArrow:
                if (event.targetPlayer == viewer.id) jackalArrowsStolen += event.amount;
                break;
            case GameEventType::JackalScaredPlayer:
                if (event.targetPlayer == viewer.id) observations.push_back(PlayerObservation{ObservationType::JackalScaredYou, viewer.id});
                break;
            case GameEventType::JackalKnockedOutPlayer:
                if (event.targetPlayer == viewer.id) observations.push_back(PlayerObservation{ObservationType::JackalKnockedOutYou, viewer.id});
                break;
            case GameEventType::JackalRepelled:
                if (event.targetPlayer == viewer.id) observations.push_back(PlayerObservation{ObservationType::JackalRepelled, viewer.id});
                break;
            case GameEventType::JackalStunned:
                if (event.actor == viewer.id) observations.push_back(PlayerObservation{ObservationType::JackalStunned, viewer.id});
                break;
            case GameEventType::PitInvestigationSucceeded:
                if (event.actor == viewer.id) { PlayerObservation o{ObservationType::PitInvestigationSucceeded, viewer.id}; o.cave = event.cave; o.tunnel = event.tunnel; observations.push_back(o); }
                break;
            case GameEventType::PitInvestigationInconclusive:
                if (event.actor == viewer.id) observations.push_back(PlayerObservation{ObservationType::PitInvestigationInconclusive, viewer.id, event.cave});
                break;
            case GameEventType::PlayerDisconnected:
                if (event.actor.has_value() && event.actor != viewer.id) observations.push_back(PlayerObservation{ObservationType::RivalDisconnected, viewer.id});
                break;
            case GameEventType::PlayerReconnected:
                if (event.actor.has_value() && event.actor != viewer.id) observations.push_back(PlayerObservation{ObservationType::RivalReconnected, viewer.id});
                break;
            case GameEventType::PlayerReserveExpired:
                if (event.actor.has_value() && event.actor != viewer.id) observations.push_back(PlayerObservation{ObservationType::RivalReserveExpired, viewer.id});
                break;
            case GameEventType::PlayerDisconnectTimedOut:
                if (event.actor.has_value() && event.actor != viewer.id) observations.push_back(PlayerObservation{ObservationType::RivalDisconnectTimedOut, viewer.id});
                break;
            case GameEventType::PlayerKilled:
                if (event.targetPlayer == viewer.id) {
                    if (!viewerFellIntoPit) {
                        const auto type = event.basiliskBehavior.has_value()
                            ? ObservationType::BasiliskFoundYou
                            : ObservationType::YouDied;
                        observations.push_back(PlayerObservation{type, viewer.id, event.cave});
                    }
                }
                else if (event.targetPlayer.has_value() && viewer.alive) observations.push_back(PlayerObservation{ObservationType::RivalDied, viewer.id});
                break;
            case GameEventType::ItemFound:
                if (event.actor == viewer.id) { PlayerObservation o{ObservationType::ItemFound, viewer.id}; o.cave = event.cave; o.itemType = event.itemType; observations.push_back(o); }
                break;
            case GameEventType::OldHuntersMapFound:
                if (event.actor == viewer.id) observations.push_back(PlayerObservation{ObservationType::OldHuntersMapFound, viewer.id, event.cave});
                break;
            case GameEventType::OldHuntersMapRead:
                if (event.actor == viewer.id) observations.push_back(PlayerObservation{ObservationType::OldHuntersMapDistance, viewer.id, event.cave, std::nullopt, event.amount});
                break;
            case GameEventType::ArrowFound:
                if (event.actor == viewer.id) observations.push_back(PlayerObservation{ObservationType::ArrowFound, viewer.id, event.cave, std::nullopt, event.amount});
                break;
            case GameEventType::ExoticCallingCardFound:
                if (event.actor == viewer.id) observations.push_back(PlayerObservation{ObservationType::ExoticCallingCardFound, viewer.id, event.cave});
                break;
            case GameEventType::SigilAcquired:
                if (event.actor == viewer.id) observations.push_back(PlayerObservation{ObservationType::SigilAcquired, viewer.id, event.cave, event.targetPlayer});
                break;
            case GameEventType::ExtractionActivated:
                if (event.actor == viewer.id && isExtractionVisibleTo(state, viewer))
                    observations.push_back(PlayerObservation{
                        ObservationType::ExtractionRevealed,
                        viewer.id,
                        state.extraction.cave});
                break;
            case GameEventType::EscapeAvailable:
                if (event.actor == viewer.id) observations.push_back(PlayerObservation{ObservationType::EscapeAvailable, viewer.id, event.cave});
                break;
            case GameEventType::MatchDrawn:
                observations.push_back(PlayerObservation{ObservationType::MatchDrawn, viewer.id});
                break;
            default: break;
        }
    }
    if (jackalArrowsStolen > 0)
        observations.push_back(PlayerObservation{ObservationType::JackalRobbedYou, viewer.id, std::nullopt, std::nullopt, jackalArrowsStolen});
    if (viewerKilledBasilisk) observations.push_back(PlayerObservation{ObservationType::BasiliskKilled, viewer.id, state.basilisk.cave});
    else if (viewerReachedBasilisk && state.basilisk.alive) observations.push_back(PlayerObservation{ObservationType::BasiliskEvaded, viewer.id});
}

} // namespace

std::vector<PlayerObservation> ObservationSystem::buildForPlayer(const MatchState& state, PlayerId viewerId, const std::vector<GameEvent>& events) {
    const PlayerState* viewer = findPlayer(state, viewerId);
    if (viewer == nullptr) return {};
    std::vector<PlayerObservation> observations;
    addEventFeedback(state, *viewer, events, observations);
    addEnvironmentalClues(state, *viewer, events, observations);
    return observations;
}

} // namespace basilisk
