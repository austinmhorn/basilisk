#include "basilisk/systems/WorldDangerSystem.hpp"

#include <algorithm>
#include <optional>

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

void createBodyIfMissing(MatchState& state, const PlayerState& player,
                         std::vector<GameEvent>& events) {
    const bool exists = std::any_of(state.bodies.begin(), state.bodies.end(),
        [&](const BodyState& body) { return body.owner == player.id; });
    if (exists) return;

    state.bodies.push_back(BodyState{player.id, player.cave, true});
    events.push_back(GameEvent{
        GameEventType::BodyCreated,
        std::nullopt,
        player.id,
        player.cave
    });
}

void killPlayerInPit(MatchState& state, PlayerState& player,
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

    createBodyIfMissing(state, player, events);
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

std::vector<CaveId> safeKnockoutDestinations(const MatchState& state, const PlayerState& player) {
    std::vector<CaveId> options;

    for (const CaveId cave : state.world.caveIds()) {
        if (cave == player.cave) continue;
        if (state.basilisk.alive && cave == state.basilisk.cave) continue;
        if (isPitCave(state, cave)) continue;
        options.push_back(cave);
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

JackalAttack chooseAttack(const MatchState& state, const PlayerState& player,
                          RandomGenerator& rng) {
    std::vector<JackalAttack> valid;
    if (player.arrows > 0) valid.push_back(JackalAttack::Rob);
    if (state.world.contains(player.cave) && !state.world.cave(player.cave).connections.empty()) {
        valid.push_back(JackalAttack::Scare);
    }
    if (!safeKnockoutDestinations(state, player).empty()) {
        valid.push_back(JackalAttack::Knockout);
    }

    const auto index = static_cast<std::size_t>(
        rng.range(0, static_cast<int>(valid.size()) - 1));
    return valid[index];
}

void attackPlayer(MatchState& state, JackalState& jackal, PlayerState& player,
                  RandomGenerator& rng, std::vector<GameEvent>& events) {
    const JackalAttack attack = chooseAttack(state, player, rng);

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
            break;

        case JackalAttack::Scare: {
            const auto& connections = state.world.cave(player.cave).connections;
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
                player.cave
            });
            break;
        }
    }

    (void)jackal;
}

} // namespace

void WorldDangerSystem::resolvePits(
    MatchState& state,
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
        if (auto* player = randomLivingPlayerInCave(state, jackal.cave, rng)) {
            attackPlayer(state, jackal, *player, rng, events);
            attackedThisPhase = true;
        }

        auto options = jackalMoveOptions(state, jackal);
        if (options.empty()) continue;

        const auto index = static_cast<std::size_t>(
            rng.range(0, static_cast<int>(options.size()) - 1));
        const CaveId oldCave = jackal.cave;
        jackal.lastCave = oldCave;
        jackal.cave = options[index];

        events.push_back(GameEvent{
            GameEventType::JackalMoved,
            std::nullopt,
            std::nullopt,
            jackal.cave,
            static_cast<int>(oldCave)
        });

        if (!attackedThisPhase) {
            if (auto* player = randomLivingPlayerInCave(state, jackal.cave, rng)) {
                attackPlayer(state, jackal, *player, rng, events);
            }
        }
    }
}

} // namespace basilisk
