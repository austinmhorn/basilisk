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
#include "basilisk/systems/WorldDangerSystem.hpp"

namespace basilisk {
namespace {

constexpr std::uint64_t kRoundSeedSalt = 0x9E3779B97F4A7C15ULL;

PlayerState& playerById(MatchState& state, PlayerId id) {
    const auto it = std::find_if(state.players.begin(), state.players.end(),
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
    const auto it = std::find_if(jackal.statuses.begin(), jackal.statuses.end(),
        [](const StatusEffect& status) { return status.type == StatusEffectType::Stunned; });
    if (it == jackal.statuses.end()) {
        jackal.statuses.push_back(StatusEffect{StatusEffectType::Stunned, applications});
    } else {
        it->remainingApplications = applications;
    }
}

bool caveOccupiedByLivingPlayer(const MatchState& state, CaveId cave) {
    return std::any_of(state.players.begin(), state.players.end(),
        [cave](const PlayerState& player) { return player.alive && player.cave == cave; });
}

bool caveContainsActivePit(const MatchState& state, CaveId cave) {
    return std::any_of(state.pits.begin(), state.pits.end(),
        [cave](const PitState& pit) { return pit.active && pit.cave == cave; });
}

std::optional<int> distanceTo(const WorldGraph& world, CaveId start, CaveId target) {
    if (!world.contains(start) || !world.contains(target)) return std::nullopt;
    if (start == target) return 0;

    std::queue<CaveId> frontier;
    std::unordered_map<CaveId, int> distances;
    frontier.push(start);
    distances.emplace(start, 0);

    while (!frontier.empty()) {
        const CaveId current = frontier.front();
        frontier.pop();
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

std::vector<CaveId> safeBasiliskDestinations(const MatchState& state) {
    std::vector<CaveId> destinations;
    if (!state.world.contains(state.basilisk.cave)) return destinations;
    for (const CaveId cave : state.world.cave(state.basilisk.cave).connections) {
        if (!caveOccupiedByLivingPlayer(state, cave) && !caveContainsActivePit(state, cave)) {
            destinations.push_back(cave);
        }
    }
    return destinations;
}

std::vector<CaveId> safeBasiliskEvadeDestinations(const MatchState& state) {
    std::vector<CaveId> destinations;
    for (const CaveId cave : state.world.caveIds()) {
        if (cave == state.basilisk.cave) continue;
        if (caveContainsActivePit(state, cave)) continue;
        if (caveOccupiedByLivingPlayer(state, cave)) continue;
        destinations.push_back(cave);
    }
    return destinations;
}

void emitBasiliskMove(MatchState& state, CaveId destination, std::vector<GameEvent>& events) {
    const CaveId oldCave = state.basilisk.cave;
    state.basilisk.lastCave = oldCave;
    state.basilisk.cave = destination;
    state.basilisk.roundsSinceMove = 0;
    events.push_back(GameEvent{GameEventType::BasiliskMoved, std::nullopt, std::nullopt,
        destination, static_cast<int>(oldCave), state.basilisk.behavior});
}

void moveBasiliskRandomly(MatchState& state, RandomGenerator& rng, std::vector<GameEvent>& events) {
    auto destinations = safeBasiliskDestinations(state);
    if (destinations.empty()) return;
    const auto index = static_cast<std::size_t>(rng.range(0, static_cast<int>(destinations.size()) - 1));
    emitBasiliskMove(state, destinations[index], events);
}

void relocateBasiliskAfterEvade(MatchState& state, RandomGenerator& rng,
                                std::vector<GameEvent>& events) {
    auto destinations = safeBasiliskEvadeDestinations(state);
    if (destinations.empty()) return;
    const auto index = static_cast<std::size_t>(rng.range(0, static_cast<int>(destinations.size()) - 1));
    emitBasiliskMove(state, destinations[index], events);
}

void moveBasiliskToward(MatchState& state, CaveId target, RandomGenerator& rng,
                        std::vector<GameEvent>& events) {
    auto destinations = safeBasiliskDestinations(state);
    int bestDistance = std::numeric_limits<int>::max();
    std::vector<CaveId> best;

    for (const CaveId destination : destinations) {
        const auto distance = distanceTo(state.world, destination, target);
        if (!distance.has_value()) continue;
        if (*distance < bestDistance) {
            bestDistance = *distance;
            best = {destination};
        } else if (*distance == bestDistance) {
            best.push_back(destination);
        }
    }
    if (best.empty()) return;
    const auto index = static_cast<std::size_t>(rng.range(0, static_cast<int>(best.size()) - 1));
    emitBasiliskMove(state, best[index], events);
}

BasiliskBehavior randomFirstEvadeBehavior(RandomGenerator& rng) {
    switch (rng.range(0, 3)) {
        case 0: return BasiliskBehavior::Restless;
        case 1: return BasiliskBehavior::Lurker;
        case 2: return BasiliskBehavior::Skittish;
        default: return BasiliskBehavior::Territorial;
    }
}

void changeBasiliskBehavior(MatchState& state, BasiliskBehavior behavior,
                            std::optional<PlayerId> actor,
                            std::vector<GameEvent>& events) {
    state.basilisk.behavior = behavior;
    state.basilisk.roundsSinceMove = 0;
    events.push_back(GameEvent{GameEventType::BasiliskBehaviorChanged, actor, std::nullopt,
        state.basilisk.cave, 0, behavior});
}

void recordSingleBasiliskKill(MatchState& state, PlayerId shooter,
                              std::vector<GameEvent>& events) {
    state.basilisk.alive = false;
    state.result.status = MatchStatus::Completed;
    state.result.outcome = MatchOutcome::BasiliskKilled;
    state.result.winner = shooter;
    events.push_back(GameEvent{GameEventType::BasiliskKilled, shooter, std::nullopt,
        state.basilisk.cave, 0, state.basilisk.behavior});
}

bool basiliskKillRoll(int encounter, RandomGenerator& rng) {
    if (encounter == 1) return rng.chance(3, 5);
    if (encounter == 2) return rng.chance(1, 2);
    return true;
}

void resolveBasiliskShotBatch(MatchState& state, const std::vector<PlayerId>& shooters,
                              RandomGenerator& rng, std::vector<GameEvent>& events) {
    if (!state.basilisk.alive || shooters.empty()) return;

    ++state.basilisk.trueEncounters;
    const int encounter = state.basilisk.trueEncounters;

    std::vector<PlayerId> successfulShooters;
    for (const PlayerId shooter : shooters) {
        events.push_back(GameEvent{GameEventType::ArrowReachedBasilisk, shooter, std::nullopt,
            state.basilisk.cave, encounter, state.basilisk.behavior});
        if (basiliskKillRoll(encounter, rng)) {
            successfulShooters.push_back(shooter);
        }
    }

    if (successfulShooters.size() == 1) {
        recordSingleBasiliskKill(state, successfulShooters.front(), events);
        return;
    }

    if (successfulShooters.size() > 1) {
        state.basilisk.alive = false;
        state.result.status = MatchStatus::Completed;
        state.result.outcome = MatchOutcome::SimultaneousBasiliskKill;
        state.result.winner.reset();

        for (const PlayerId shooter : successfulShooters) {
            events.push_back(GameEvent{GameEventType::BasiliskKilled, shooter, std::nullopt,
                state.basilisk.cave, 0, state.basilisk.behavior});
        }
        events.push_back(GameEvent{GameEventType::MatchDrawn});
        return;
    }

    events.push_back(GameEvent{GameEventType::BasiliskEvaded, std::nullopt, std::nullopt,
        state.basilisk.cave, encounter, state.basilisk.behavior});
    relocateBasiliskAfterEvade(state, rng, events);

    if (encounter == 1) {
        if (rng.chance(1, 2)) {
            changeBasiliskBehavior(state, randomFirstEvadeBehavior(rng), std::nullopt, events);
        }
        return;
    }

    if (encounter == 2) {
        changeBasiliskBehavior(state, BasiliskBehavior::Enraged, std::nullopt, events);
    }
}

int movementInterval(BasiliskBehavior behavior) {
    switch (behavior) {
        case BasiliskBehavior::Restless: return 5;
        case BasiliskBehavior::Lurker: return 8;
        case BasiliskBehavior::Territorial: return 5;
        case BasiliskBehavior::Enraged: return 2;
        case BasiliskBehavior::Normal:
        case BasiliskBehavior::Skittish: return 0;
    }
    return 0;
}

void createBodyIfMissing(MatchState& state, const PlayerState& player,
                         std::vector<GameEvent>& events) {
    const bool exists = std::any_of(state.bodies.begin(), state.bodies.end(),
        [&](const BodyState& body) { return body.owner == player.id; });
    if (exists) return;
    state.bodies.push_back(BodyState{player.id, player.cave, true, player.cave});
    events.push_back(GameEvent{GameEventType::BodyCreated, std::nullopt, player.id, player.cave});
}

CaveId chooseExtractionCave(const MatchState& state, CaveId from) {
    CaveId best = from;
    int bestDistance = -1;
    for (const CaveId cave : state.world.caveIds()) {
        if (cave == from || caveContainsActivePit(state, cave)) continue;
        const auto distance = distanceTo(state.world, from, cave);
        if (!distance.has_value()) continue;
        if (*distance > bestDistance || (*distance == bestDistance && cave < best)) {
            bestDistance = *distance;
            best = cave;
        }
    }
    return best;
}

void discoverDynamicBodyContents(MatchState& state, PlayerState& player,
                                 std::vector<GameEvent>& events) {
    for (auto& body : state.bodies) {
        if (!body.sigilAvailable || body.owner == player.id) continue;

        const CaveId sigilCave = body.sigilCave.value_or(body.cave);
        if (sigilCave != player.cave) continue;

        if (body.cave == player.cave) {
            events.push_back(GameEvent{GameEventType::BodyFound, player.id, body.owner, body.cave});
        }

        body.sigilAvailable = false;
        player.heldSigilFrom = body.owner;
        events.push_back(GameEvent{GameEventType::SigilAcquired, player.id, body.owner, sigilCave});

        state.extraction.active = true;
        state.extraction.sigilHolder = player.id;
        state.extraction.cave = chooseExtractionCave(state, player.cave);
        events.push_back(GameEvent{GameEventType::ExtractionActivated, player.id, std::nullopt,
            state.extraction.cave});
        return;
    }
}

void resolveEscape(MatchState& state, PlayerState& player, std::vector<GameEvent>& events) {
    if (state.result.status != MatchStatus::Active || !player.alive ||
        !state.extraction.active || !state.extraction.cave.has_value() ||
        !state.extraction.sigilHolder.has_value() ||
        *state.extraction.sigilHolder != player.id ||
        !player.heldSigilFrom.has_value() || player.cave != *state.extraction.cave) {
        return;
    }

    state.result.status = MatchStatus::Completed;
    state.result.outcome = MatchOutcome::EscapedWithSigil;
    state.result.winner = player.id;
    events.push_back(GameEvent{GameEventType::PlayerEscaped, player.id, std::nullopt, player.cave});
}

void resolveMutualDeathDraw(MatchState& state, std::vector<GameEvent>& events) {
    if (state.result.status != MatchStatus::Active || state.players.size() < 2) return;
    const bool anyAlive = std::any_of(state.players.begin(), state.players.end(),
        [](const PlayerState& player) { return player.alive; });
    if (anyAlive) return;
    state.result.status = MatchStatus::Completed;
    state.result.outcome = MatchOutcome::Draw;
    state.result.winner.reset();
    events.push_back(GameEvent{GameEventType::MatchDrawn});
}

} // namespace

std::vector<GameEvent> TurnResolver::resolve(MatchState& state,
                                             const std::vector<PlayerAction>& actions) const {
    std::vector<GameEvent> events;
    if (state.result.status == MatchStatus::Completed) return events;

    RandomGenerator rng(roundSeed(state));
    std::vector<PlayerAction> orderedActions = actions;
    std::stable_sort(orderedActions.begin(), orderedActions.end(),
        [](const PlayerAction& a, const PlayerAction& b) { return a.player < b.player; });

    // 1. Movement resolves before shooting.
    for (const auto& action : orderedActions) {
        if (action.type != ActionType::Move) continue;
        auto& player = playerById(state, action.player);
        if (!player.alive || !action.targetCave.has_value() ||
            !state.world.areConnected(player.cave, *action.targetCave)) continue;
        player.cave = *action.targetCave;
        events.push_back(GameEvent{GameEventType::PlayerMoved, player.id, std::nullopt, player.cave});
        if (state.extraction.active && state.extraction.cave == player.cave &&
            state.extraction.sigilHolder == player.id && player.heldSigilFrom.has_value()) {
            events.push_back(GameEvent{GameEventType::EscapeAvailable, player.id, std::nullopt, player.cave});
        }
    }

    // 2. Ranged attacks. PvP damage and Basilisk shots are accumulated before
    // outcomes are applied so packet/order priority cannot decide a winner.
    struct PendingDamage { PlayerId target; PlayerId attacker; CaveId cave; int amount; };
    std::vector<PendingDamage> pendingDamage;
    std::vector<PlayerId> basiliskShooters;

    for (const auto& action : orderedActions) {
        if (action.type != ActionType::Shoot) continue;
        auto& shooter = playerById(state, action.player);
        if (!shooter.alive || shooter.arrows <= 0 || !action.targetCave.has_value() ||
            !state.world.areConnected(shooter.cave, *action.targetCave)) continue;

        --shooter.arrows;
        events.push_back(GameEvent{GameEventType::ArrowFired, shooter.id, std::nullopt, *action.targetCave});

        const auto targetPlayer = std::find_if(state.players.begin(), state.players.end(),
            [&](const PlayerState& player) {
                return player.id != shooter.id && player.alive && player.cave == *action.targetCave;
            });
        if (targetPlayer != state.players.end()) {
            pendingDamage.push_back({targetPlayer->id, shooter.id, targetPlayer->cave, state.rules.arrowDamage});
            events.push_back(GameEvent{GameEventType::ArrowHitPlayer, shooter.id, targetPlayer->id,
                targetPlayer->cave, state.rules.arrowDamage});
            continue;
        }

        if (state.basilisk.alive && state.basilisk.cave == *action.targetCave) {
            basiliskShooters.push_back(shooter.id);
            continue;
        }

        const auto targetJackal = std::find_if(state.jackals.begin(), state.jackals.end(),
            [&](const JackalState& jackal) { return jackal.cave == *action.targetCave; });
        if (targetJackal != state.jackals.end()) {
            applyOrRefreshStun(*targetJackal, state.rules.jackalStunPhases);
            events.push_back(GameEvent{GameEventType::ArrowHitJackal, shooter.id, std::nullopt, targetJackal->cave});
            events.push_back(GameEvent{GameEventType::JackalStunned, shooter.id, std::nullopt,
                targetJackal->cave, state.rules.jackalStunPhases});
            continue;
        }

        events.push_back(GameEvent{GameEventType::ArrowMissed, shooter.id, std::nullopt, *action.targetCave});
    }

    resolveBasiliskShotBatch(state, basiliskShooters, rng, events);

    std::unordered_map<PlayerId, PlayerId> lethalAttackerByTarget;
    for (const auto& damage : pendingDamage) {
        auto& target = playerById(state, damage.target);
        target.health = std::max(0, target.health - damage.amount);
        events.push_back(GameEvent{GameEventType::PlayerDamaged, damage.attacker, target.id,
            damage.cave, damage.amount});
        if (target.health <= 0) lethalAttackerByTarget[target.id] = damage.attacker;
    }

    for (auto& player : state.players) {
        if (player.alive && player.health <= 0) {
            player.alive = false;
            std::optional<PlayerId> killer;
            const auto killerIt = lethalAttackerByTarget.find(player.id);
            if (killerIt != lethalAttackerByTarget.end()) killer = killerIt->second;
            events.push_back(GameEvent{GameEventType::PlayerKilled, killer, player.id, player.cave});
            createBodyIfMissing(state, player, events);
        }
    }

    // Basilisk victory (including simultaneous Basilisk kill) was recorded
    // during the shot phase and has precedence over PvP terminal outcomes.
    if (state.result.status != MatchStatus::Completed) {
        resolveMutualDeathDraw(state, events);
    }

    // 3. Search resolves only for survivors. Dynamic bodies and detached
    // Sigils remain searchable even when static cave loot has been consumed.
    std::optional<CaveId> mostRecentSearchCave;
    for (const auto& action : orderedActions) {
        if (action.type != ActionType::Search) continue;
        auto& player = playerById(state, action.player);
        if (!player.alive) continue;

        mostRecentSearchCave = player.cave;
        discoverDynamicBodyContents(state, player, events);
        auto searchEvents = SearchSystem::search(player, state.rules, rng);
        events.insert(events.end(), searchEvents.begin(), searchEvents.end());

        if (state.basilisk.alive && state.basilisk.behavior == BasiliskBehavior::Skittish &&
            state.world.areConnected(player.cave, state.basilisk.cave)) {
            moveBasiliskRandomly(state, rng, events);
        }
    }

    // 4. Item use.
    for (const auto& action : orderedActions) {
        if (action.type != ActionType::UseItem || !action.targetItem.has_value()) continue;
        auto& player = playerById(state, action.player);
        if (!player.alive) continue;
        auto itemEvents = ItemSystem::use(player, *action.targetItem, state.rules);
        events.insert(events.end(), itemEvents.begin(), itemEvents.end());
    }

    // 5. Environmental hazards. A hunter can still complete already-committed
    // earlier phases before the Pit resolves, matching the simultaneous model.
    WorldDangerSystem::resolvePits(state, rng, events);
    if (state.result.status == MatchStatus::Active) {
        resolveMutualDeathDraw(state, events);
    }

    // 6. Jackal/NPC phase. Stunned Jackals consume one suppressed NPC phase;
    // active Jackals move and can rob, scare, or knock out a hunter.
    WorldDangerSystem::resolveJackals(state, rng, events);
    if (state.result.status == MatchStatus::Active) {
        resolveMutualDeathDraw(state, events);
    }

    // 7. Basilisk behavior phase.
    if (state.basilisk.alive && state.result.status == MatchStatus::Active) {
        ++state.basilisk.roundsSinceMove;
        const int interval = movementInterval(state.basilisk.behavior);
        if (interval > 0 && state.basilisk.roundsSinceMove >= interval) {
            const auto target = mostRecentSearchCave.has_value()
                ? mostRecentSearchCave : state.mostRecentSearchCave;
            if (state.basilisk.behavior == BasiliskBehavior::Territorial && target.has_value()) {
                moveBasiliskToward(state, *target, rng, events);
            } else {
                moveBasiliskRandomly(state, rng, events);
            }
        }
    }

    // 8. Contextual escape resolves only after hazards/NPCs. Reaching the exit
    // is not enough: the Sigil holder must survive the round and still be there.
    if (state.result.status == MatchStatus::Active) {
        for (const auto& action : orderedActions) {
            if (action.type != ActionType::Contextual ||
                action.contextualAction != ContextualActionType::Escape) continue;
            auto& player = playerById(state, action.player);
            resolveEscape(state, player, events);
        }
    }

    if (mostRecentSearchCave.has_value()) state.mostRecentSearchCave = mostRecentSearchCave;
    ++state.round;
    return events;
}

} // namespace basilisk