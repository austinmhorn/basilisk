#include "DebugMapProvider.hpp"

#include <algorithm>
#include <cassert>
#include <set>
#include <utility>

#include "basilisk/MatchState.hpp"

namespace basilisk::game::debug {
DebugMapTruth buildDebugMapTruth(
    const MatchState& state, const PlayerMapLayout& layout) {
    DebugMapTruth truth;
    truth.fullBounds = layout.positionedBounds();
    for (const CaveId cave : state.world.caveIds()) {
        if (const auto position = layout.cavePosition(cave))
            truth.cavePositions.emplace(cave, *position);
    }
    std::set<PhysicalTunnel> tunnels;
    for (const CaveId source : state.world.caveIds()) {
        for (const CaveId destination : state.world.cave(source).connections) {
            const auto [first, second] = std::minmax(source, destination);
            tunnels.insert({first, second});
        }
    }
    truth.tunnels.assign(tunnels.begin(), tunnels.end());
    return truth;
}

DebugGameplayTruth buildDebugGameplayTruth(
    const MatchState& state, std::span<const DebugHunterLabel> hunters) {
    DebugGameplayTruth truth;
    truth.basiliskCave = state.basilisk.cave;
    truth.basiliskAlive = state.basilisk.alive;
    truth.basiliskBehavior = state.basilisk.behavior;
    truth.basiliskLastCave = state.basilisk.lastCave;
    truth.basiliskEncounterCount = state.basilisk.trueEncounters;
    truth.basiliskRoundsSinceMove = state.basilisk.roundsSinceMove;
    truth.territorialSearchTarget = state.mostRecentSearchCave;
    for (const PitState& pit : state.pits) truth.pitCaves.push_back(pit.cave);
    for (const JackalState& jackal : state.jackals)
        truth.jackalCaves.push_back(jackal.cave);
    for (const DebugHunterLabel& label : hunters) {
        const auto player = std::find_if(state.players.begin(), state.players.end(),
            [&](const PlayerState& candidate) { return candidate.id == label.player; });
        if (player != state.players.end() && player->alive) {
            assert(state.world.contains(player->cave) &&
                "Living debug hunter has no authoritative cave");
            truth.hunters.push_back({
                player->id, player->cave, label.label, player->health, player->arrows});
        }
    }
    if (state.result.status == MatchStatus::Active) {
        for (const BodyState& body : state.bodies) {
            if (body.sigilAvailable) {
                truth.sigils.push_back({
                    body.owner, body.sigilCave.value_or(body.cave),
                    DebugGameplayTruth::SigilState::OnMap, std::nullopt});
            }
        }
        for (const PlayerState& player : state.players) {
            if (player.heldSigilFrom.has_value()) {
                truth.sigils.push_back({
                    *player.heldSigilFrom, player.cave,
                    DebugGameplayTruth::SigilState::Carried, player.id});
            }
        }
    }
    return truth;
}

DebugMapProvider::DebugMapProvider(
    DebugMapTruth mapTruth,
    GameplayTruthSource gameplayTruthSource,
    BehaviorControlSource behaviorControlSource,
    ItemGrantSource itemGrantSource,
    KillPlayerSource killPlayerSource,
    ParticipantSource participantSource)
    : mapTruth_(std::move(mapTruth)),
      gameplayTruthSource_(std::move(gameplayTruthSource)),
      behaviorControlSource_(std::move(behaviorControlSource)),
      itemGrantSource_(std::move(itemGrantSource)),
      killPlayerSource_(std::move(killPlayerSource)),
      participantSource_(std::move(participantSource)) {}

const DebugMapTruth& DebugMapProvider::mapTruth() const noexcept {
    return mapTruth_;
}

DebugGameplayTruth DebugMapProvider::gameplayTruth() const {
    return gameplayTruthSource_ == nullptr
        ? DebugGameplayTruth{}
        : gameplayTruthSource_();
}

bool DebugMapProvider::cycleBasiliskBehavior() {
    if (behaviorControlSource_ == nullptr) return false;
    BasiliskBehavior next = BasiliskBehavior::Normal;
    switch (gameplayTruth().basiliskBehavior) {
        case BasiliskBehavior::Normal: next = BasiliskBehavior::Restless; break;
        case BasiliskBehavior::Restless: next = BasiliskBehavior::Lurker; break;
        case BasiliskBehavior::Lurker: next = BasiliskBehavior::Skittish; break;
        case BasiliskBehavior::Skittish: next = BasiliskBehavior::Territorial; break;
        case BasiliskBehavior::Territorial: next = BasiliskBehavior::Enraged; break;
        case BasiliskBehavior::Enraged: next = BasiliskBehavior::Normal; break;
    }
    return behaviorControlSource_(next);
}

bool DebugMapProvider::grantItem(ItemType item) {
    const auto roster = participants();
    return !roster.empty() && grantItem(roster.front().player, item);
}

bool DebugMapProvider::grantItem(PlayerId player, ItemType item) {
    return itemGrantSource_ != nullptr && itemGrantSource_(player, item);
}

bool DebugMapProvider::killPlayer(DebugKillTarget target) {
    const auto roster = participants();
    if (roster.empty()) return false;
    const auto selected = target == DebugKillTarget::Host
        ? roster.begin()
        : std::find_if(std::next(roster.begin()), roster.end(),
            [](const DebugParticipant&) { return true; });
    return selected != roster.end() && killPlayer(selected->player);
}

bool DebugMapProvider::killPlayer(PlayerId player) {
    return killPlayerSource_ != nullptr && killPlayerSource_(player);
}

bool DebugMapProvider::killControlAvailable() const noexcept {
    return killPlayerSource_ != nullptr;
}

std::vector<DebugParticipant> DebugMapProvider::participants() const {
    return participantSource_ == nullptr
        ? std::vector<DebugParticipant>{}
        : participantSource_();
}

void DebugMapRevealState::toggle() noexcept {
    revealed_ = !revealed_;
}

bool DebugMapRevealState::revealed() const noexcept {
    return revealed_;
}

} // namespace basilisk::game::debug
