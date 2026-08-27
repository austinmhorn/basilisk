#include "basilisk/systems/WorldDangerSystem.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <queue>

#include "basilisk/systems/SigilPlacementSystem.hpp"

namespace basilisk {
namespace {

bool isPitCave(const MatchState& state, CaveId cave) {
    return std::any_of(state.pits.begin(), state.pits.end(),
        [cave](const PitState& pit) { return pit.active && pit.cave == cave; });
}

bool isJackalStunned(const JackalState& jackal) {
    return std::any_of(jackal.statuses.begin(), jackal.statuses.end(),
        [](const StatusEffect& status) {
            return status.type == StatusEffectType::Stunned && status.remainingApplications > 0;
        });
}

void consumeOneStunPhase(JackalState& jackal) {
    for (auto& status : jackal.statuses) {
        if (status.type == StatusEffectType::Stunned && status.remainingApplications > 0) {
            --status.remainingApplications;
        }
    }

    std::erase_if(jackal.statuses,
        [](const StatusEffect& status) { return status.remainingApplications <= 0; });
}

void killPlayerInPit(
    MatchState& state,
    PlayerState& player,
    std::vector<GameEvent>& events) {

    if (!player.alive || !isPitCave(state, player.cave)) return;

    player.health = 0;
    player.alive = false;

    events.push_back(GameEvent{
        GameEventType::PitTriggered,
        player.id,
        player.id,
        player.cave
    });

    events.push_back(GameEvent{
        GameEventType::PlayerKilled,
        std::nullopt,
        player.id,
        player.cave
    });

    placeSigilsForDeath(state, player, player.cave, events);
}

std::vector<CaveId> jackalMoveOptions(const MatchState& state, const JackalState& jackal) {
    std::vector<CaveId> options;
    if (!state.world.contains(jackal.cave)) return options;

    for (const CaveId cave : state.world.cave(jackal.cave).connections) {
        if (state.basilisk.alive && cave == state.basilisk.cave) continue;
        if (isPitCave(state, cave)) continue;
        options.push_back(cave);
    }

    return options;
}

bool occupiedByLivingHunter(const MatchState& state, CaveId cave) {
    return std::any_of(state.players.begin(), state.players.end(),
        [cave](const PlayerState& player) { return player.alive && player.cave == cave; });
}

bool blockedForSafeGraph(const MatchState& state, CaveId cave) {
    return cave == state.basilisk.cave || isPitCave(state, cave);
}

std::size_t safeDegree(const MatchState& state, CaveId cave) {
    if (!state.world.contains(cave) || blockedForSafeGraph(state, cave)) return 0;
    return static_cast<std::size_t>(std::count_if(
        state.world.cave(cave).connections.begin(),
        state.world.cave(cave).connections.end(),
        [&](CaveId next) { return !blockedForSafeGraph(state, next); }));
}

bool validFleeCave(const MatchState& state, CaveId cave) {
    return state.world.contains(cave) && !blockedForSafeGraph(state, cave) &&
        !occupiedByLivingHunter(state, cave) && safeDegree(state, cave) > 1;
}

std::map<CaveId, int> safeDistancesFrom(const MatchState& state, CaveId origin) {
    std::map<CaveId, int> distances;
    if (!state.world.contains(origin) || blockedForSafeGraph(state, origin)) return distances;
    std::queue<CaveId> pending;
    distances.emplace(origin, 0);
    pending.push(origin);
    while (!pending.empty()) {
        const CaveId cave = pending.front();
        pending.pop();
        const int nextDistance = distances.at(cave) + 1;
        for (const CaveId next : state.world.cave(cave).connections) {
            if (blockedForSafeGraph(state, next)) continue;
            if (distances.emplace(next, nextDistance).second) pending.push(next);
        }
    }
    return distances;
}

std::vector<CaveId> validAdjacentFleeCaves(
    const MatchState& state, const JackalState& jackal) {
    std::vector<CaveId> options;
    if (!state.world.contains(jackal.cave)) return options;
    for (const CaveId cave : state.world.cave(jackal.cave).connections) {
        if (validFleeCave(state, cave)) options.push_back(cave);
    }
    return options;
}

void moveJackal(
    JackalState& jackal, CaveId destination, std::vector<GameEvent>& events) {
    const CaveId oldCave = jackal.cave;
    jackal.lastCave = oldCave;
    jackal.cave = destination;
    events.push_back(GameEvent{
        GameEventType::JackalMoved, std::nullopt, std::nullopt,
        jackal.cave, static_cast<int>(oldCave)});
}

bool moveImmediatelyAfterTheft(
    const MatchState& state, JackalState& jackal, RandomGenerator& rng,
    std::vector<GameEvent>& events) {
    const auto options = validAdjacentFleeCaves(state, jackal);
    if (options.empty()) return false;
    const auto index = static_cast<std::size_t>(
        rng.range(0, static_cast<int>(options.size()) - 1));
    moveJackal(jackal, options[index], events);
    return true;
}

void clearFlee(JackalState& jackal) {
    jackal.fleeOrigin.reset();
    jackal.protectedHunter.reset();
    jackal.fleeRoundsRemaining = 0;
    jackal.lastCave.reset();
}

void performFleeMovement(
    const MatchState& state, JackalState& jackal, RandomGenerator& rng,
    std::vector<GameEvent>& events) {
    auto options = validAdjacentFleeCaves(state, jackal);
    if (!options.empty() && jackal.fleeOrigin.has_value()) {
        const auto distances = safeDistancesFrom(state, *jackal.fleeOrigin);
        int bestDistance = -1;
        for (const CaveId cave : options) {
            const auto found = distances.find(cave);
            if (found != distances.end()) bestDistance = std::max(bestDistance, found->second);
        }
        if (bestDistance >= 0) {
            std::erase_if(options, [&](CaveId cave) {
                const auto found = distances.find(cave);
                return found == distances.end() || found->second != bestDistance;
            });
        }
        if (options.size() > 1 && jackal.lastCave.has_value()) {
            const auto previous = std::find(options.begin(), options.end(), *jackal.lastCave);
            if (previous != options.end()) options.erase(previous);
        }
    }
    if (!options.empty()) {
        const auto index = static_cast<std::size_t>(
            rng.range(0, static_cast<int>(options.size()) - 1));
        moveJackal(jackal, options[index], events);
    }
    if (--jackal.fleeRoundsRemaining <= 0) clearFlee(jackal);
}

std::vector<CaveId> safeKnockoutDestinations(const MatchState& state, const PlayerState& player) {
    std::vector<CaveId> options;

    for (const CaveId cave : state.world.caveIds()) {
        if (cave == player.cave) continue;
        if (state.basilisk.alive && cave == state.basilisk.cave) continue;
        if (isPitCave(state, cave)) continue;
        if (occupiedByLivingHunter(state, cave)) continue;
        options.push_back(cave);
    }

    return options;
}

std::vector<CaveId> scareDestinations(
    const MatchState& state, const PlayerState& player) {
    std::vector<CaveId> options;
    if (!state.world.contains(player.cave)) return options;
    for (const CaveId cave : state.world.cave(player.cave).connections) {
        if (!occupiedByLivingHunter(state, cave)) options.push_back(cave);
    }
    return options;
}

PlayerState* randomLivingPlayerInCave(MatchState& state, CaveId cave, RandomGenerator& rng) {
    std::vector<PlayerState*> candidates;
    for (auto& player : state.players) {
        if (player.alive && player.cave == cave) candidates.push_back(&player);
    }

    if (candidates.empty()) return nullptr;
    const auto index = static_cast<std::size_t>(
        rng.range(0, static_cast<int>(candidates.size()) - 1));
    return candidates[index];
}

enum class JackalAttack {
    Rob,
    Scare,
    Knockout
};

JackalAttack chooseAttack(const MatchState& state, const JackalState& jackal,
                          const PlayerState& player, RandomGenerator& rng) {
    std::vector<JackalAttack> valid;
    const bool theftProtected = jackal.fleeRoundsRemaining > 0 &&
        jackal.protectedHunter == player.id;
    if (player.arrows > 0 && !theftProtected) valid.push_back(JackalAttack::Rob);
    if (!scareDestinations(state, player).empty()) {
        valid.push_back(JackalAttack::Scare);
    }
    if (!safeKnockoutDestinations(state, player).empty()) {
        valid.push_back(JackalAttack::Knockout);
    }

    const auto index = static_cast<std::size_t>(
        rng.range(0, static_cast<int>(valid.size()) - 1));
    return valid[index];
}

void applyJackalKnockoutDamage(
    MatchState& state,
    PlayerState& player,
    RandomGenerator& rng,
    std::vector<GameEvent>& events) {

    if (!state.rules.jackalDamageEnabled || !player.alive) return;

    const int minimum = std::min(state.rules.jackalDamageMin, state.rules.jackalDamageMax);
    const int maximum = std::max(state.rules.jackalDamageMin, state.rules.jackalDamageMax);
    const int damage = rng.range(minimum, maximum);
    if (damage <= 0) return;

    player.health = std::max(0, player.health - damage);
    events.push_back(GameEvent{
        GameEventType::PlayerDamaged,
        std::nullopt,
        player.id,
        player.cave,
        damage
    });

    if (player.health > 0) return;

    player.alive = false;
    events.push_back(GameEvent{
        GameEventType::PlayerKilled,
        std::nullopt,
        player.id,
        player.cave
    });
    placeSigilsForDeath(state, player, player.cave, events);
}

bool attackPlayer(MatchState& state, JackalState& jackal, PlayerState& player,
                  RandomGenerator& rng, std::vector<GameEvent>& events) {
    if (player.jackalRepellentRounds > 0) {
        events.push_back(GameEvent{
            GameEventType::JackalRepelled,
            player.id,
            player.id,
            player.cave,
            player.jackalRepellentRounds,
            std::nullopt,
            ItemType::JackalRepellent
        });
        return false;
    }

    const JackalAttack attack = chooseAttack(state, jackal, player, rng);

    switch (attack) {
        case JackalAttack::Rob:
            --player.arrows;
            events.push_back(GameEvent{
                GameEventType::JackalRobbedArrow,
                std::nullopt,
                player.id,
                player.cave,
                1
            });
            jackal.fleeOrigin = jackal.cave;
            jackal.protectedHunter = player.id;
            jackal.fleeRoundsRemaining = 3;
            moveImmediatelyAfterTheft(state, jackal, rng, events);
            return true;

        case JackalAttack::Scare: {
            const auto connections = scareDestinations(state, player);
            const auto index = static_cast<std::size_t>(
                rng.range(0, static_cast<int>(connections.size()) - 1));
            player.cave = connections[index];

            events.push_back(GameEvent{
                GameEventType::JackalScaredPlayer,
                std::nullopt,
                player.id,
                player.cave
            });

            killPlayerInPit(state, player, events);
            break;
        }

        case JackalAttack::Knockout: {
            auto destinations = safeKnockoutDestinations(state, player);
            const auto index = static_cast<std::size_t>(
                rng.range(0, static_cast<int>(destinations.size()) - 1));
            player.cave = destinations[index];

            events.push_back(GameEvent{
                GameEventType::JackalKnockedOutPlayer,
                std::nullopt,
                player.id,
                player.cave,
                state.rules.jackalDamageEnabled ? state.rules.jackalDamageMin : 0
            });

            applyJackalKnockoutDamage(state, player, rng, events);
            break;
        }
    }
    return false;
}

} // namespace

void WorldDangerSystem::resolvePits(
    MatchState& state,
    RandomGenerator& rng,
    std::vector<GameEvent>& events) {

    for (auto& player : state.players) {
        killPlayerInPit(state, player, events);
    }
}

void WorldDangerSystem::resolveJackals(
    MatchState& state,
    RandomGenerator& rng,
    std::vector<GameEvent>& events) {

    for (auto& jackal : state.jackals) {
        if (isJackalStunned(jackal)) {
            consumeOneStunPhase(jackal);
            continue;
        }

        bool attackedThisPhase = false;
        bool relocatedAfterTheft = false;
        if (auto* player = randomLivingPlayerInCave(state, jackal.cave, rng)) {
            relocatedAfterTheft = attackPlayer(state, jackal, *player, rng, events);
            attackedThisPhase = true;
        }

        if (relocatedAfterTheft) continue;

        if (jackal.fleeRoundsRemaining > 0) {
            performFleeMovement(state, jackal, rng, events);
            continue;
        }

        auto options = jackalMoveOptions(state, jackal);
        if (options.empty()) continue;

        const auto index = static_cast<std::size_t>(
            rng.range(0, static_cast<int>(options.size()) - 1));
        moveJackal(jackal, options[index], events);

        if (!attackedThisPhase) {
            if (auto* player = randomLivingPlayerInCave(state, jackal.cave, rng)) {
                attackPlayer(state, jackal, *player, rng, events);
            }
        }
    }
}

} // namespace basilisk
