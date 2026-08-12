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

        const auto target = std::find_if(
            state.players.begin(),
            state.players.end(),
            [&](const PlayerState& player) {
                return player.id != shooter.id &&
                       player.alive &&
                       player.cave == *action.targetCave;
            });

        if (target == state.players.end()) {
            events.push_back(GameEvent{
                GameEventType::ArrowMissed,
                shooter.id,
                std::nullopt,
                *action.targetCave,
                0
            });
            continue;
        }

        pendingDamage.push_back(PendingDamage{
            target->id,
            shooter.id,
            target->cave,
            state.rules.arrowDamage
        });

        events.push_back(GameEvent{
            GameEventType::ArrowHitPlayer,
            shooter.id,
            target->id,
            target->cave,
            state.rules.arrowDamage
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

    ++state.round;
    return events;
}

} // namespace basilisk
