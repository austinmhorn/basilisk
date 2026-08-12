#include "basilisk/systems/TurnResolver.hpp"

#include <algorithm>
#include <stdexcept>

namespace basilisk {
namespace {

PlayerState& playerById(MatchState& state, PlayerId id) {
    const auto it = std::find_if(
        state.players.begin(),
        state.players.end(),
        [id](const PlayerState& player) { return player.id == id; });

    if (it == state.players.end()) {
        throw std::invalid_argument("Action references an unknown player.");
    }

    return *it;
}

void applyOrRefreshStun(JackalState& jackal, int applications) {
    const auto it = std::find_if(
        jackal.statuses.begin(),
        jackal.statuses.end(),
        [](const StatusEffect& status) {
            return status.type == StatusEffectType::Stunned;
        });

    if (it == jackal.statuses.end()) {
        jackal.statuses.push_back(StatusEffect{
            StatusEffectType::Stunned,
            applications
        });
        return;
    }

    it->remainingApplications = applications;
}

void advanceJackalStatuses(JackalState& jackal) {
    for (auto& status : jackal.statuses) {
        if (status.type == StatusEffectType::Stunned && status.remainingApplications > 0) {
            --status.remainingApplications;
        }
    }

    std::erase_if(jackal.statuses, [](const StatusEffect& status) {
        return status.remainingApplications <= 0;
    });
}

} // namespace

std::vector<GameEvent> TurnResolver::resolve(
    MatchState& state,
    const std::vector<PlayerAction>& actions) const {

    std::vector<GameEvent> events;

    // Phase 1: movement. All legal moves are applied before any shooting.
    for (const auto& action : actions) {
        if (action.type != ActionType::Move) {
            continue;
        }

        auto& player = playerById(state, action.player);
        if (!player.alive || !action.targetCave.has_value()) {
            continue;
        }

        if (!state.world.areConnected(player.cave, *action.targetCave)) {
            continue;
        }

        player.cave = *action.targetCave;
        events.push_back(GameEvent{
            GameEventType::PlayerMoved,
            player.id,
            std::nullopt,
            player.cave,
            0
        });
    }

    // Phase 2: ranged attacks. Damage is accumulated first so lethal
    // simultaneous shots cannot cancel another already-locked shot.
    struct PendingDamage {
        PlayerId target{};
        PlayerId attacker{};
        CaveId cave{};
        int amount{};
    };

    std::vector<PendingDamage> pendingDamage;

    for (const auto& action : actions) {
        if (action.type != ActionType::Shoot) {
            continue;
        }

        auto& shooter = playerById(state, action.player);
        if (!shooter.alive || shooter.arrows <= 0 || !action.targetCave.has_value()) {
            continue;
        }

        if (!state.world.areConnected(shooter.cave, *action.targetCave)) {
            continue;
        }

        --shooter.arrows;
        events.push_back(GameEvent{
            GameEventType::ArrowFired,
            shooter.id,
            std::nullopt,
            *action.targetCave,
            0
        });

        const auto targetPlayer = std::find_if(
            state.players.begin(),
            state.players.end(),
            [&](const PlayerState& player) {
                return player.id != shooter.id &&
                       player.alive &&
                       player.cave == *action.targetCave;
            });

        if (targetPlayer != state.players.end()) {
            pendingDamage.push_back(PendingDamage{
                targetPlayer->id,
                shooter.id,
                targetPlayer->cave,
                state.rules.arrowDamage
            });

            events.push_back(GameEvent{
                GameEventType::ArrowHitPlayer,
                shooter.id,
                targetPlayer->id,
                targetPlayer->cave,
                state.rules.arrowDamage
            });
            continue;
        }

        if (state.basilisk.alive && state.basilisk.cave == *action.targetCave) {
            // The exact Basilisk hit/evasion rule is intentionally deferred.
            // We record that the arrow reached the Basilisk's cave without
            // inventing an outcome that has not been designed yet.
            events.push_back(GameEvent{
                GameEventType::ArrowReachedBasilisk,
                shooter.id,
                std::nullopt,
                state.basilisk.cave,
                0
            });
            continue;
        }

        const auto targetJackal = std::find_if(
            state.jackals.begin(),
            state.jackals.end(),
            [&](const JackalState& jackal) {
                return jackal.cave == *action.targetCave;
            });

        if (targetJackal != state.jackals.end()) {
            applyOrRefreshStun(*targetJackal, state.rules.jackalStunPhases);

            events.push_back(GameEvent{
                GameEventType::ArrowHitJackal,
                shooter.id,
                std::nullopt,
                targetJackal->cave,
                0
            });

            events.push_back(GameEvent{
                GameEventType::JackalStunned,
                shooter.id,
                std::nullopt,
                targetJackal->cave,
                state.rules.jackalStunPhases
            });
            continue;
        }

        events.push_back(GameEvent{
            GameEventType::ArrowMissed,
            shooter.id,
            std::nullopt,
            *action.targetCave,
            0
        });
    }

    for (const auto& damage : pendingDamage) {
        auto& target = playerById(state, damage.target);
        target.health = std::max(0, target.health - damage.amount);

        events.push_back(GameEvent{
            GameEventType::PlayerDamaged,
            damage.attacker,
            target.id,
            damage.cave,
            damage.amount
        });
    }

    for (auto& player : state.players) {
        if (player.alive && player.health <= 0) {
            player.alive = false;
            events.push_back(GameEvent{
                GameEventType::PlayerKilled,
                std::nullopt,
                player.id,
                player.cave,
                0
            });
        }
    }

    // Phase 3: search. A hunter killed during the attack phase cannot search.
    for (const auto& action : actions) {
        if (action.type != ActionType::Search) {
            continue;
        }

        auto& player = playerById(state, action.player);
        if (!player.alive) {
            continue;
        }

        events.push_back(GameEvent{
            GameEventType::SearchCompleted,
            player.id,
            std::nullopt,
            player.cave,
            0
        });
    }

    // Phase 4 placeholder: Jackal/NPC phase. Movement and attacks are not yet
    // implemented, but stun durations already advance according to the agreed
    // NPC-phase semantics. A Jackal shot this round therefore consumes the
    // first of its three suppressed NPC phases here.
    for (auto& jackal : state.jackals) {
        advanceJackalStatuses(jackal);
    }

    ++state.round;
    return events;
}

} // namespace basilisk
