#include "basilisk/systems/TurnResolver.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <stdexcept>
#include <unordered_map>

#include "basilisk/Random.hpp"
#include "basilisk/systems/ItemSystem.hpp"
#include "basilisk/systems/SearchSystem.hpp"

namespace basilisk {
namespace {

constexpr std::uint64_t kRoundSeedSalt = 0x9E3779B97F4A7C15ULL;

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

std::uint64_t roundSeed(const MatchState& state) {
    return state.matchSeed ^
           (kRoundSeedSalt + static_cast<std::uint64_t>(state.round) +
            (state.matchSeed << 6U) + (state.matchSeed >> 2U));
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

bool caveOccupiedByLivingPlayer(const MatchState& state, CaveId cave) {
    return std::any_of(
        state.players.begin(),
        state.players.end(),
        [cave](const PlayerState& player) {
            return player.alive && player.cave == cave;
        });
}

std::vector<CaveId> safeBasiliskDestinations(const MatchState& state) {
    std::vector<CaveId> destinations;

    if (!state.world.contains(state.basilisk.cave)) {
        return destinations;
    }

    for (const CaveId cave : state.world.cave(state.basilisk.cave).connections) {
        if (!caveOccupiedByLivingPlayer(state, cave)) {
            destinations.push_back(cave);
        }
    }

    return destinations;
}

void moveBasiliskRandomly(
    MatchState& state,
    RandomGenerator& rng,
    std::vector<GameEvent>& events) {

    auto destinations = safeBasiliskDestinations(state);
    if (destinations.empty()) {
        return;
    }

    const CaveId oldCave = state.basilisk.cave;
    const auto index = static_cast<std::size_t>(
        rng.range(0, static_cast<int>(destinations.size()) - 1));

    state.basilisk.lastCave = oldCave;
    state.basilisk.cave = destinations[index];
    state.basilisk.roundsSinceMove = 0;

    events.push_back(GameEvent{
        GameEventType::BasiliskMoved,
        std::nullopt,
        std::nullopt,
        state.basilisk.cave,
        static_cast<int>(oldCave),
        state.basilisk.behavior
    });
}

std::optional<int> distanceTo(
    const WorldGraph& world,
    CaveId start,
    CaveId target) {

    if (!world.contains(start) || !world.contains(target)) {
        return std::nullopt;
    }

    if (start == target) {
        return 0;
    }

    std::queue<CaveId> frontier;
    std::unordered_map<CaveId, int> distances;
    frontier.push(start);
    distances.emplace(start, 0);

    while (!frontier.empty()) {
        const CaveId current = frontier.front();
        frontier.pop();

        const int currentDistance = distances.at(current);
        for (const CaveId next : world.cave(current).connections) {
            if (distances.contains(next)) {
                continue;
            }

            const int nextDistance = currentDistance + 1;
            if (next == target) {
                return nextDistance;
            }

            distances.emplace(next, nextDistance);
            frontier.push(next);
        }
    }

    return std::nullopt;
}

void moveBasiliskToward(
    MatchState& state,
    CaveId target,
    RandomGenerator& rng,
    std::vector<GameEvent>& events) {

    auto destinations = safeBasiliskDestinations(state);
    if (destinations.empty()) {
        return;
    }

    int bestDistance = std::numeric_limits<int>::max();
    std::vector<CaveId> best;

    for (const CaveId destination : destinations) {
        const auto distance = distanceTo(state.world, destination, target);
        if (!distance.has_value()) {
            continue;
        }

        if (*distance < bestDistance) {
            bestDistance = *distance;
            best.clear();
            best.push_back(destination);
        } else if (*distance == bestDistance) {
            best.push_back(destination);
        }
    }

    if (best.empty()) {
        return;
    }

    const CaveId oldCave = state.basilisk.cave;
    const auto index = static_cast<std::size_t>(
        rng.range(0, static_cast<int>(best.size()) - 1));

    state.basilisk.lastCave = oldCave;
    state.basilisk.cave = best[index];
    state.basilisk.roundsSinceMove = 0;

    events.push_back(GameEvent{
        GameEventType::BasiliskMoved,
        std::nullopt,
        std::nullopt,
        state.basilisk.cave,
        static_cast<int>(oldCave),
        state.basilisk.behavior
    });
}

BasiliskBehavior randomFirstEvadeBehavior(RandomGenerator& rng) {
    switch (rng.range(0, 3)) {
        case 0: return BasiliskBehavior::Restless;
        case 1: return BasiliskBehavior::Lurker;
        case 2: return BasiliskBehavior::Skittish;
        default: return BasiliskBehavior::Territorial;
    }
}

void changeBasiliskBehavior(
    MatchState& state,
    BasiliskBehavior behavior,
    PlayerId actor,
    std::vector<GameEvent>& events) {

    state.basilisk.behavior = behavior;
    state.basilisk.roundsSinceMove = 0;

    events.push_back(GameEvent{
        GameEventType::BasiliskBehaviorChanged,
        actor,
        std::nullopt,
        state.basilisk.cave,
        0,
        behavior
    });
}

void killBasilisk(
    MatchState& state,
    PlayerId shooter,
    std::vector<GameEvent>& events) {

    state.basilisk.alive = false;
    events.push_back(GameEvent{
        GameEventType::BasiliskKilled,
        shooter,
        std::nullopt,
        state.basilisk.cave,
        0,
        state.basilisk.behavior
    });
}

void resolveTrueBasiliskEncounter(
    MatchState& state,
    PlayerId shooter,
    RandomGenerator& rng,
    std::vector<GameEvent>& events) {

    if (!state.basilisk.alive) {
        return;
    }

    ++state.basilisk.trueEncounters;

    events.push_back(GameEvent{
        GameEventType::ArrowReachedBasilisk,
        shooter,
        std::nullopt,
        state.basilisk.cave,
        state.basilisk.trueEncounters,
        state.basilisk.behavior
    });

    if (state.basilisk.trueEncounters == 1) {
        if (rng.chance(3, 4)) {
            killBasilisk(state, shooter, events);
            return;
        }

        events.push_back(GameEvent{
            GameEventType::BasiliskEvaded,
            shooter,
            std::nullopt,
            state.basilisk.cave,
            1,
            state.basilisk.behavior
        });

        if (rng.chance(1, 2)) {
            changeBasiliskBehavior(
                state,
                randomFirstEvadeBehavior(rng),
                shooter,
                events);
        }
        return;
    }

    if (state.basilisk.trueEncounters == 2) {
        if (rng.chance(1, 2)) {
            killBasilisk(state, shooter, events);
            return;
        }

        events.push_back(GameEvent{
            GameEventType::BasiliskEvaded,
            shooter,
            std::nullopt,
            state.basilisk.cave,
            2,
            state.basilisk.behavior
        });

        changeBasiliskBehavior(
            state,
            BasiliskBehavior::Enraged,
            shooter,
            events);
        return;
    }

    killBasilisk(state, shooter, events);
}

int movementInterval(BasiliskBehavior behavior) {
    switch (behavior) {
        case BasiliskBehavior::Restless:
            return 5;
        case BasiliskBehavior::Lurker:
            return 8;
        case BasiliskBehavior::Territorial:
            return 5;
        case BasiliskBehavior::Enraged:
            return 2;
        case BasiliskBehavior::Normal:
        case BasiliskBehavior::Skittish:
            return 0;
    }

    return 0;
}

} // namespace

std::vector<GameEvent> TurnResolver::resolve(
    MatchState& state,
    const std::vector<PlayerAction>& actions) const {

    std::vector<GameEvent> events;
    RandomGenerator rng(roundSeed(state));

    std::vector<PlayerAction> orderedActions = actions;
    std::stable_sort(
        orderedActions.begin(),
        orderedActions.end(),
        [](const PlayerAction& left, const PlayerAction& right) {
            return left.player < right.player;
        });

    // Phase 1: movement.
    for (const auto& action : orderedActions) {
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
            player.cave
        });
    }

    // Phase 2: ranged attacks.
    struct PendingDamage {
        PlayerId target{};
        PlayerId attacker{};
        CaveId cave{};
        int amount{};
    };

    std::vector<PendingDamage> pendingDamage;

    for (const auto& action : orderedActions) {
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
            *action.targetCave
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
            resolveTrueBasiliskEncounter(state, shooter.id, rng, events);
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
                targetJackal->cave
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
            *action.targetCave
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
                player.cave
            });
        }
    }

    // Phase 3: Search. Static loot is one roll per hunter per cave.
    // Dynamic cave contents will be layered on later and remain discoverable.
    std::optional<CaveId> mostRecentSearchCave;

    for (const auto& action : orderedActions) {
        if (action.type != ActionType::Search) {
            continue;
        }

        auto& player = playerById(state, action.player);
        if (!player.alive) {
            continue;
        }

        mostRecentSearchCave = player.cave;

        auto searchEvents = SearchSystem::search(player, state.rules, rng);
        events.insert(events.end(), searchEvents.begin(), searchEvents.end());

        if (state.basilisk.alive &&
            state.basilisk.behavior == BasiliskBehavior::Skittish &&
            state.world.areConnected(player.cave, state.basilisk.cave)) {
            moveBasiliskRandomly(state, rng, events);
        }
    }

    // Phase 3b: item use. A hunter killed during ranged resolution cannot use
    // a healing item after death; surviving hunters may resolve their item.
    for (const auto& action : orderedActions) {
        if (action.type != ActionType::UseItem || !action.targetItem.has_value()) {
            continue;
        }

        auto& player = playerById(state, action.player);
        if (!player.alive) {
            continue;
        }

        auto itemEvents = ItemSystem::use(player, *action.targetItem, state.rules);
        events.insert(events.end(), itemEvents.begin(), itemEvents.end());
    }

    // Phase 4: Jackal/NPC status advancement.
    for (auto& jackal : state.jackals) {
        advanceJackalStatuses(jackal);
    }

    // Basilisk behavior phase.
    if (state.basilisk.alive) {
        ++state.basilisk.roundsSinceMove;

        const int interval = movementInterval(state.basilisk.behavior);
        if (interval > 0 && state.basilisk.roundsSinceMove >= interval) {
            if (state.basilisk.behavior == BasiliskBehavior::Territorial &&
                mostRecentSearchCave.has_value()) {
                moveBasiliskToward(state, *mostRecentSearchCave, rng, events);
            } else {
                moveBasiliskRandomly(state, rng, events);
            }
        }
    }

    ++state.round;
    return events;
}

} // namespace basilisk
