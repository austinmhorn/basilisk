#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/MatchResult.hpp"
#include "basilisk/MatchState.hpp"
#include "basilisk/Observation.hpp"
#include "basilisk/Rules.hpp"
#include "basilisk/actors/Basilisk.hpp"
#include "basilisk/items/Item.hpp"
#include "basilisk/systems/MatchCoordinator.hpp"
#include "basilisk/systems/SnapshotSystem.hpp"
#include "basilisk/world/MapGenerator.hpp"

using namespace basilisk;

namespace {

enum class Playstyle : std::size_t {
    Hunter = 0,
    Scavenger,
    Opportunist,
    Extractor,
    Count
};

constexpr std::size_t kStyleCount = static_cast<std::size_t>(Playstyle::Count);
constexpr std::size_t kItemCount = 5;

const char* styleName(Playstyle style) {
    switch (style) {
        case Playstyle::Hunter: return "Hunter";
        case Playstyle::Scavenger: return "Scavenger";
        case Playstyle::Opportunist: return "Opportunist";
        case Playstyle::Extractor: return "Extractor";
        case Playstyle::Count: break;
    }
    return "Unknown";
}

const char* itemName(ItemType item) {
    switch (item) {
        case ItemType::HealingDraught: return "Healing Draught";
        case ItemType::OldMinersMap: return "Old Miner's Map";
        case ItemType::SurveyFragment: return "Survey Fragment";
        case ItemType::JackalRepellent: return "Jackal Repellent";
        case ItemType::BloodBait: return "Blood Bait";
    }
    return "Unknown";
}

std::size_t itemIndex(ItemType item) {
    switch (item) {
        case ItemType::HealingDraught: return 0;
        case ItemType::OldMinersMap: return 1;
        case ItemType::SurveyFragment: return 2;
        case ItemType::JackalRepellent: return 3;
        case ItemType::BloodBait: return 4;
    }
    return 0;
}

std::uint64_t mix64(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

Playstyle styleFor(MatchSeed seed, PlayerId player) {
    return static_cast<Playstyle>(mix64(seed ^
        (static_cast<std::uint64_t>(player) * 0xD6E8FEB86659FD93ULL)) % kStyleCount);
}

struct CaveMemory {
    int visits{0};
    int ordinarySearches{0};
    int objectiveSearches{0};
    RoundNumber lastVisitedRound{0};
};

struct BotMemory {
    std::unordered_map<CaveId, CaveMemory> caves;
    std::unordered_set<CaveId> rememberedLooseArrows;
    bool rivalDead{false};
};

struct HuntTiming {
    std::optional<RoundNumber> firstEvadeRound;
    std::optional<RoundNumber> secondEvadeRound;
};

struct StyleStats {
    std::uint64_t assignments{0};
    std::uint64_t wins{0};
    std::uint64_t basiliskWins{0};
    std::uint64_t extractionWins{0};
    std::uint64_t searches{0};
    std::uint64_t shots{0};
    std::uint64_t pvpShots{0};
    std::uint64_t pvpKills{0};
    std::uint64_t deaths{0};
    std::uint64_t basiliskEncounters{0};
    std::uint64_t bodiesFound{0};
    std::uint64_t sigilsAcquired{0};
    std::uint64_t itemsFound{0};
    std::uint64_t itemsUsed{0};
    std::uint64_t roundsAlive{0};
};

struct Stats {
    std::uint64_t matches{0}, completed{0}, stalled{0};
    std::uint64_t basiliskWins{0}, simultaneousBasiliskDraws{0}, extractionWins{0}, draws{0};

    std::uint64_t totalRounds{0}, totalCaves{0}, totalFinalArrows{0};
    std::vector<std::uint64_t> roundSamples;
    std::uint64_t unexploredMoves{0}, knownMoves{0}, frontierMoves{0};

    std::uint64_t pitWarnings{0}, pitInvestigations{0};
    std::uint64_t pitClueSuccesses{0}, pitClueInconclusive{0};
    std::uint64_t knownPitTunnelAvoidances{0}, pitDeaths{0}, mutualPitDraws{0};

    std::uint64_t arrowsFired{0}, arrowMisses{0}, pvpHits{0}, pvpDeaths{0};
    std::uint64_t basiliskContactKills{0}, jackalKnockoutDeaths{0}, unattributedPlayerDeaths{0}, jackalHits{0};
    std::uint64_t basiliskEncounters{0}, basiliskEvades{0};
    std::uint64_t firstKills{0}, secondKills{0}, thirdKills{0};
    std::uint64_t secondEncounterMatches{0}, thirdEncounterMatches{0};
    std::uint64_t restlessAssignments{0}, lurkerAssignments{0}, skittishAssignments{0};
    std::uint64_t territorialAssignments{0}, enragedAssignments{0};

    std::uint64_t evadeRelocations{0};
    std::uint64_t relocationDistanceTotal{0};
    std::vector<std::uint64_t> relocationDistanceSamples;
    std::uint64_t adjacentRelocations{0}, mediumRelocations{0}, longRelocations{0};
    int maxRelocationDistance{0};
    std::uint64_t firstRelocations{0}, firstRelocationDistanceTotal{0};
    std::uint64_t secondRelocations{0}, secondRelocationDistanceTotal{0};
    std::uint64_t firstReacquisitions{0}, firstReacquireRoundsTotal{0};
    std::uint64_t secondReacquisitions{0}, secondReacquireRoundsTotal{0};
    std::array<std::uint64_t, 4> basiliskDeathMatchesByEncounter{};
    std::array<std::uint64_t, 4> basiliskDeathRoundsByEncounter{};

    std::uint64_t bodiesCreated{0}, bodiesFound{0}, sigilsAcquired{0};
    std::uint64_t extractionsActivated{0}, escapeAvailable{0}, escaped{0};
    std::uint64_t objectiveSearches{0}, extractionPathMoves{0};

    std::uint64_t jackalMoves{0}, jackalStuns{0}, jackalRepelled{0};
    std::uint64_t jackalRobberies{0}, jackalScares{0}, jackalKnockouts{0};
    std::uint64_t jackalDamageEvents{0}, jackalDamageTotal{0};

    std::uint64_t looseArrowSpawns{0}, arrowsFound{0}, searches{0};
    std::array<std::uint64_t, kItemCount> itemsFound{};
    std::array<std::uint64_t, kItemCount> itemsUsed{};
    std::uint64_t inventoryFullDrops{0}, exoticCallingCards{0};
    std::uint64_t healedHp{0}, baitInfluencedMoves{0};

    std::uint64_t zeroArrowPlayerRounds{0}, zeroArrowRecoveries{0};
    std::uint64_t looseArrowSightingsAtCapacity{0};
    std::uint64_t rememberedArrowPursuitMoves{0}, rememberedArrowRecoveries{0};
    std::uint64_t rememberedArrowInvalidations{0};
    std::uint64_t stalenessPatrolMoves{0}, stalenessTargetsSelected{0};
    std::uint64_t stalenessTargetAgeTotal{0}, stalenessArrowRecoveries{0};

    std::uint64_t restlessShotsSuppressed{0}, lastArrowPvpShotsSuppressed{0};
    std::uint64_t adjacentBasiliskShots{0}, exactEnragedShots{0}, pvpShots{0};

    std::uint64_t scavengerStockedRounds{0};
    std::uint64_t scavengerResourceSearches{0};
    std::uint64_t scavengerPeriodicSearches{0};

    std::uint64_t stalledAllZeroArrows{0}, stalledLooseArrowsAvailable{0};
    std::uint64_t stalledPitOnlyFrontier{0};
    std::uint64_t stalledZeroArrowHuntersWithReachableArrow{0};
    std::uint64_t nearestReachableArrowDistanceTotal{0};
    std::uint64_t nearestReachableArrowDistanceSamples{0};
    int maxNearestReachableArrowDistance{0};

    std::array<StyleStats, kStyleCount> style{};
    std::array<std::array<std::uint64_t, kStyleCount>, kStyleCount> matchups{};
};

bool hasObs(const PlayerRoundSnapshot& s, ObservationType type) {
    return std::any_of(s.observations.begin(), s.observations.end(),
        [type](const PlayerObservation& o) { return o.type == type; });
}

bool hasAdjacentBasiliskClue(const PlayerRoundSnapshot& s) {
    return hasObs(s, ObservationType::BasiliskNearby) ||
           hasObs(s, ObservationType::BasiliskNearbySubtle);
}

bool hasBasiliskClue(const PlayerRoundSnapshot& s) {
    return hasAdjacentBasiliskClue(s) ||
           hasObs(s, ObservationType::RestlessBasiliskNoise) ||
           hasObs(s, ObservationType::EnragedLastKnownCave);
}

PlayerAction materialize(PlayerId player, const AvailableAction& a) {
    PlayerAction out;
    out.player = player;
    out.type = a.type;
    out.targetCave = a.targetCave;
    out.targetTunnel = a.targetTunnel;
    out.targetItem = a.targetItem;
    out.contextualAction = a.contextualAction;
    return out;
}

const DiscoveredCaveView* caveView(const PlayerRoundSnapshot& s, CaveId cave) {
    const auto it = std::find_if(s.map.caves.begin(), s.map.caves.end(),
        [cave](const DiscoveredCaveView& c) { return c.cave == cave; });
    return it == s.map.caves.end() ? nullptr : &*it;
}

const AvailableAction* actionOfType(const PlayerRoundSnapshot& s, ActionType type) {
    for (const auto& a : s.availableActions) if (a.type == type) return &a;
    return nullptr;
}

std::vector<const AvailableAction*> actionsOfType(const PlayerRoundSnapshot& s, ActionType type) {
    std::vector<const AvailableAction*> out;
    for (const auto& a : s.availableActions) if (a.type == type) out.push_back(&a);
    return out;
}

const AvailableAction* pick(const std::vector<const AvailableAction*>& choices, std::uint64_t salt) {
    if (choices.empty()) return nullptr;
    return choices[static_cast<std::size_t>(salt % choices.size())];
}

const AvailableAction* moveTo(const PlayerRoundSnapshot& s, CaveId cave) {
    for (const auto& a : s.availableActions)
        if (a.type == ActionType::Move && a.targetCave == cave) return &a;
    return nullptr;
}

const AvailableAction* shootTo(const PlayerRoundSnapshot& s, CaveId cave) {
    for (const auto& a : s.availableActions)
        if (a.type == ActionType::Shoot && a.targetCave == cave) return &a;
    return nullptr;
}

const AvailableAction* useItem(const PlayerRoundSnapshot& s, ItemType item) {
    for (const auto& a : s.availableActions)
        if (a.type == ActionType::UseItem && a.targetItem == item) return &a;
    return nullptr;
}

const AvailableAction* firstSurveyAction(const PlayerRoundSnapshot& s) {
    for (const auto& a : s.availableActions)
        if (a.type == ActionType::UseItem && a.targetItem == ItemType::SurveyFragment && a.targetTunnel.has_value())
            return &a;
    return nullptr;
}

std::optional<TunnelView> investigatedPitTunnel(const PlayerRoundSnapshot& s) {
    const auto* view = caveView(s, s.currentCave);
    if (!view) return std::nullopt;
    for (const auto& tunnel : view->exits)
        if (tunnel.strongColdDraft) return tunnel;
    return std::nullopt;
}

bool actionUsesTunnel(const AvailableAction& action, const TunnelView& tunnel) {
    if (action.targetTunnel.has_value() && action.targetTunnel == tunnel.id) return true;
    return tunnel.destination.has_value() && action.targetCave == tunnel.destination;
}

bool dangerousKnownEdge(const PlayerRoundSnapshot& s, CaveId from, CaveId to) {
    const auto* view = caveView(s, from);
    if (!view) return false;
    return std::any_of(view->exits.begin(), view->exits.end(), [&](const TunnelView& tunnel) {
        return tunnel.destination == to && tunnel.strongColdDraft;
    });
}

bool safeKnownConnection(const PlayerRoundSnapshot& s, CaveId from, const TunnelView& tunnel) {
    if (!tunnel.destination.has_value() || tunnel.strongColdDraft) return false;
    return !dangerousKnownEdge(s, *tunnel.destination, from);
}

bool caveHasMeaningfulFrontier(const DiscoveredCaveView& cave) {
    return std::any_of(cave.exits.begin(), cave.exits.end(),
        [](const TunnelView& tunnel) { return !tunnel.destination.has_value() && !tunnel.strongColdDraft; });
}

bool caveHasPitOnlyFrontier(const DiscoveredCaveView& cave) {
    bool anyUnknown = false;
    bool anySafeUnknown = false;
    for (const auto& tunnel : cave.exits) {
        if (tunnel.destination.has_value()) continue;
        anyUnknown = true;
        if (!tunnel.strongColdDraft) anySafeUnknown = true;
    }
    return anyUnknown && !anySafeUnknown;
}

bool hasAnyMeaningfulFrontier(const PlayerRoundSnapshot& s) {
    return std::any_of(s.map.caves.begin(), s.map.caves.end(), caveHasMeaningfulFrontier);
}

bool hasAnyPitOnlyFrontier(const PlayerRoundSnapshot& s) {
    return std::any_of(s.map.caves.begin(), s.map.caves.end(), caveHasPitOnlyFrontier);
}

std::optional<CaveId> safeStepTo(const PlayerRoundSnapshot& s, CaveId target) {
    if (s.currentCave == target) return target;
    std::queue<CaveId> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_set<CaveId> seen;
    q.push(s.currentCave);
    seen.insert(s.currentCave);
    while (!q.empty()) {
        const CaveId cur = q.front(); q.pop();
        const auto* view = caveView(s, cur);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!safeKnownConnection(s, cur, tunnel)) continue;
            const CaveId next = *tunnel.destination;
            if (!seen.insert(next).second) continue;
            parent[next] = cur;
            if (next == target) {
                CaveId step = next;
                while (parent.contains(step) && parent.at(step) != s.currentCave) step = parent.at(step);
                return step;
            }
            q.push(next);
        }
    }
    return std::nullopt;
}

std::optional<int> safeDistance(const PlayerRoundSnapshot& s, CaveId target) {
    if (s.currentCave == target) return 0;
    std::queue<std::pair<CaveId, int>> q;
    std::unordered_set<CaveId> seen;
    q.push({s.currentCave, 0});
    seen.insert(s.currentCave);
    while (!q.empty()) {
        const auto [cur, distance] = q.front(); q.pop();
        const auto* view = caveView(s, cur);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!safeKnownConnection(s, cur, tunnel)) continue;
            const CaveId next = *tunnel.destination;
            if (!seen.insert(next).second) continue;
            if (next == target) return distance + 1;
            q.push({next, distance + 1});
        }
    }
    return std::nullopt;
}

std::optional<int> worldDistance(const WorldGraph& world, CaveId start, CaveId target) {
    if (!world.contains(start) || !world.contains(target)) return std::nullopt;
    if (start == target) return 0;

    std::queue<std::pair<CaveId, int>> q;
    std::unordered_set<CaveId> seen;
    q.push({start, 0});
    seen.insert(start);

    while (!q.empty()) {
        const auto [current, distance] = q.front(); q.pop();
        for (const CaveId next : world.cave(current).connections) {
            if (!seen.insert(next).second) continue;
            if (next == target) return distance + 1;
            q.push({next, distance + 1});
        }
    }
    return std::nullopt;
}

std::optional<CaveId> nearestFrontierStep(const PlayerRoundSnapshot& s) {
    if (const auto* here = caveView(s, s.currentCave); here && caveHasMeaningfulFrontier(*here))
        return s.currentCave;

    std::queue<CaveId> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_set<CaveId> seen;
    q.push(s.currentCave);
    seen.insert(s.currentCave);
    while (!q.empty()) {
        const CaveId cur = q.front(); q.pop();
        const auto* view = caveView(s, cur);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!safeKnownConnection(s, cur, tunnel)) continue;
            const CaveId next = *tunnel.destination;
            if (!seen.insert(next).second) continue;
            parent[next] = cur;
            const auto* nextView = caveView(s, next);
            if (nextView && caveHasMeaningfulFrontier(*nextView)) {
                CaveId step = next;
                while (parent.contains(step) && parent.at(step) != s.currentCave) step = parent.at(step);
                return step;
            }
            q.push(next);
        }
    }
    return std::nullopt;
}

struct StalenessChoice {
    CaveId target{};
    CaveId firstStep{};
    RoundNumber age{};
};

std::optional<StalenessChoice> oldestSafeCaveStep(const PlayerRoundSnapshot& s, const BotMemory& memory) {
    std::queue<std::pair<CaveId, int>> q;
    std::unordered_map<CaveId, CaveId> parent;
    std::unordered_set<CaveId> seen;
    q.push({s.currentCave, 0});
    seen.insert(s.currentCave);

    std::optional<CaveId> best;
    RoundNumber bestVisited = std::numeric_limits<RoundNumber>::max();
    int bestDistance = std::numeric_limits<int>::max();

    while (!q.empty()) {
        const auto [cur, distance] = q.front(); q.pop();
        if (cur != s.currentCave) {
            const auto it = memory.caves.find(cur);
            const RoundNumber lastVisited = it == memory.caves.end() ? 0 : it->second.lastVisitedRound;
            if (!best.has_value() || lastVisited < bestVisited ||
                (lastVisited == bestVisited && distance < bestDistance) ||
                (lastVisited == bestVisited && distance == bestDistance && cur < *best)) {
                best = cur;
                bestVisited = lastVisited;
                bestDistance = distance;
            }
        }
        const auto* view = caveView(s, cur);
        if (!view) continue;
        for (const auto& tunnel : view->exits) {
            if (!safeKnownConnection(s, cur, tunnel)) continue;
            const CaveId next = *tunnel.destination;
            if (!seen.insert(next).second) continue;
            parent[next] = cur;
            q.push({next, distance + 1});
        }
    }

    if (!best.has_value()) return std::nullopt;
    CaveId step = *best;
    while (parent.contains(step) && parent.at(step) != s.currentCave) step = parent.at(step);
    const RoundNumber age = s.round >= bestVisited ? s.round - bestVisited : 0;
    return StalenessChoice{*best, step, age};
}

std::optional<CaveId> rememberedArrowStep(const PlayerRoundSnapshot& s, const BotMemory& memory) {
    std::optional<CaveId> best;
    int bestDistance = std::numeric_limits<int>::max();
    for (const CaveId cave : memory.rememberedLooseArrows) {
        const auto distance = safeDistance(s, cave);
        if (!distance.has_value()) continue;
        if (*distance < bestDistance || (*distance == bestDistance && (!best.has_value() || cave < *best))) {
            best = cave;
            bestDistance = *distance;
        }
    }
    if (!best.has_value()) return std::nullopt;
    return safeStepTo(s, *best);
}

std::optional<CaveId> exactEnragedTarget(const PlayerRoundSnapshot& s) {
    for (const auto& observation : s.observations) {
        if (observation.type != ObservationType::EnragedLastKnownCave || !observation.cave.has_value()) continue;
        if (shootTo(s, *observation.cave) != nullptr) return *observation.cave;
    }
    return std::nullopt;
}

std::optional<PlayerAction> chooseAction(
    Playstyle style,
    const PlayerRoundSnapshot& s,
    BotMemory& memory,
    MatchSeed matchSeed,
    Stats& stats,
    bool& stalenessMove) {

    stalenessMove = false;
    if (!s.alive || s.availableActions.empty()) return std::nullopt;

    if (hasObs(s, ObservationType::RivalDied)) memory.rivalDead = true;
    auto& caveMemory = memory.caves[s.currentCave];
    ++caveMemory.visits;
    caveMemory.lastVisitedRound = s.round;

    if (s.looseArrowPresent) {
        const bool newMemory = memory.rememberedLooseArrows.insert(s.currentCave).second;
        if (newMemory && s.arrows >= s.maxArrows) ++stats.looseArrowSightingsAtCapacity;
    } else if (memory.rememberedLooseArrows.erase(s.currentCave) > 0) {
        ++stats.rememberedArrowInvalidations;
    }

    const std::uint64_t salt = mix64(matchSeed ^
        (static_cast<std::uint64_t>(s.round) << 17U) ^ static_cast<std::uint64_t>(s.player));

    for (const auto& action : s.availableActions)
        if (action.type == ActionType::Contextual && action.contextualAction == ContextualActionType::Escape)
            return materialize(s.player, action);

    if (s.health <= 50)
        if (const auto* heal = useItem(s, ItemType::HealingDraught)) return materialize(s.player, *heal);

    if (s.hasHunterSigil && s.extractionCave.has_value()) {
        if (const auto step = safeStepTo(s, *s.extractionCave); step.has_value() && *step != s.currentCave) {
            if (const auto* move = moveTo(s, *step)) {
                ++stats.extractionPathMoves;
                ++stats.knownMoves;
                return materialize(s.player, *move);
            }
        }
    }

    if (hasObs(s, ObservationType::JackalNearby))
        if (const auto* repel = useItem(s, ItemType::JackalRepellent)) return materialize(s.player, *repel);

    if (hasObs(s, ObservationType::PitNearby) && s.temporarilyRevealedPitCaves.empty())
        if (const auto* map = useItem(s, ItemType::OldMinersMap)) return materialize(s.player, *map);

    const bool pitWarning = hasObs(s, ObservationType::PitNearby);
    const auto pitTunnel = investigatedPitTunnel(s);
    if (pitWarning && !pitTunnel.has_value()) {
        if (const auto* search = actionOfType(s, ActionType::Search)) {
            ++stats.pitInvestigations;
            return materialize(s.player, *search);
        }
    }

    const bool extractorMode = style == Playstyle::Extractor && (memory.rivalDead || s.hasHunterSigil);
    if (memory.rivalDead && !s.hasHunterSigil && caveMemory.objectiveSearches == 0) {
        if (const auto* search = actionOfType(s, ActionType::Search)) {
            ++caveMemory.objectiveSearches;
            ++stats.objectiveSearches;
            return materialize(s.player, *search);
        }
    }

    if (!extractorMode && s.arrows > 0 && hasBasiliskClue(s)) {
        if (const auto* bait = useItem(s, ItemType::BloodBait)) return materialize(s.player, *bait);
    }

    if (const auto* survey = firstSurveyAction(s)) {
        const bool scavengerSurvey = style == Playstyle::Scavenger;
        if (scavengerSurvey || hasAnyMeaningfulFrontier(s)) return materialize(s.player, *survey);
    }

    const bool adjacentBasilisk = hasAdjacentBasiliskClue(s);
    const auto exactEnraged = exactEnragedTarget(s);
    const bool restlessOnly = hasObs(s, ObservationType::RestlessBasiliskNoise) && !adjacentBasilisk && !exactEnraged.has_value();
    if (restlessOnly && s.arrows > 0) ++stats.restlessShotsSuppressed;

    if (!extractorMode && exactEnraged.has_value() && s.arrows > 0) {
        if (const auto* shot = shootTo(s, *exactEnraged)) {
            ++stats.exactEnragedShots;
            return materialize(s.player, *shot);
        }
    }

    if (!extractorMode && adjacentBasilisk && s.arrows > 0) {
        const auto shots = actionsOfType(s, ActionType::Shoot);
        if (const auto* shot = pick(shots, salt)) {
            ++stats.adjacentBasiliskShots;
            return materialize(s.player, *shot);
        }
    }

    const bool rivalNearby = hasObs(s, ObservationType::RivalNearby);
    bool allowPvp = false;
    if (!memory.rivalDead && rivalNearby && !adjacentBasilisk && !exactEnraged.has_value()) {
        if (style == Playstyle::Opportunist) allowPvp = s.arrows > 0;
        else if (style == Playstyle::Extractor) allowPvp = s.arrows >= 3;
        else allowPvp = s.arrows >= 2;

        if (!allowPvp && s.arrows == 1) ++stats.lastArrowPvpShotsSuppressed;
    }
    if (allowPvp) {
        const auto shots = actionsOfType(s, ActionType::Shoot);
        if (const auto* shot = pick(shots, salt >> 3U)) {
            ++stats.pvpShots;
            return materialize(s.player, *shot);
        }
    }

    const bool scavengerStocked = style == Playstyle::Scavenger &&
        s.arrows >= 3 && s.health >= 75 && !s.inventory.items.empty();
    if (scavengerStocked) ++stats.scavengerStockedRounds;

    if (style == Playstyle::Scavenger && !s.hasHunterSigil && !pitWarning && caveMemory.ordinarySearches == 0) {
        const bool resourceNeed = s.arrows <= 1 || s.health <= 50 || s.inventory.items.empty();
        const bool periodicFirstVisit = !scavengerStocked && caveMemory.visits == 1 &&
            ((s.currentCave + s.player + s.round) % 4U == 0U);
        if (resourceNeed || periodicFirstVisit) {
            if (const auto* search = actionOfType(s, ActionType::Search)) {
                ++caveMemory.ordinarySearches;
                if (resourceNeed) ++stats.scavengerResourceSearches;
                else ++stats.scavengerPeriodicSearches;
                return materialize(s.player, *search);
            }
        }
    }

    const auto moves = actionsOfType(s, ActionType::Move);
    std::vector<const AvailableAction*> safeMoves;
    for (const auto* move : moves) {
        if (pitTunnel.has_value() && actionUsesTunnel(*move, *pitTunnel)) {
            ++stats.knownPitTunnelAvoidances;
            continue;
        }
        safeMoves.push_back(move);
    }

    std::vector<const AvailableAction*> unknownMoves;
    std::vector<const AvailableAction*> knownMoves;
    for (const auto* move : safeMoves) {
        if (move->targetCave.has_value()) knownMoves.push_back(move);
        else unknownMoves.push_back(move);
    }

    if (!unknownMoves.empty()) {
        if (const auto* move = pick(unknownMoves, salt >> 5U)) {
            ++stats.unexploredMoves;
            return materialize(s.player, *move);
        }
    }

    if (const auto frontier = nearestFrontierStep(s); frontier.has_value() && *frontier != s.currentCave) {
        if (const auto* move = moveTo(s, *frontier)) {
            ++stats.frontierMoves;
            ++stats.knownMoves;
            return materialize(s.player, *move);
        }
    }

    if (s.arrows == 0 && !s.hasHunterSigil) {
        ++stats.zeroArrowPlayerRounds;
        if (const auto step = rememberedArrowStep(s, memory); step.has_value() && *step != s.currentCave) {
            if (const auto* move = moveTo(s, *step)) {
                ++stats.rememberedArrowPursuitMoves;
                ++stats.knownMoves;
                return materialize(s.player, *move);
            }
        }
        if (!hasAnyMeaningfulFrontier(s)) {
            if (const auto stale = oldestSafeCaveStep(s, memory); stale.has_value()) {
                if (const auto* move = moveTo(s, stale->firstStep)) {
                    ++stats.stalenessPatrolMoves;
                    ++stats.stalenessTargetsSelected;
                    stats.stalenessTargetAgeTotal += stale->age;
                    ++stats.knownMoves;
                    stalenessMove = true;
                    return materialize(s.player, *move);
                }
            }
        }
    }

    if (!knownMoves.empty()) {
        if (const auto* move = pick(knownMoves, salt >> 11U)) {
            ++stats.knownMoves;
            return materialize(s.player, *move);
        }
    }

    if (caveMemory.ordinarySearches == 0) {
        if (const auto* search = actionOfType(s, ActionType::Search)) {
            ++caveMemory.ordinarySearches;
            return materialize(s.player, *search);
        }
    }

    return materialize(s.player, s.availableActions.front());
}

void collectEvents(
    const std::vector<GameEvent>& events,
    MatchState& state,
    Stats& stats,
    const std::unordered_map<PlayerId, Playstyle>& styles,
    std::unordered_set<PlayerId>& pitDeadPlayers,
    const std::unordered_set<PlayerId>& zeroBefore,
    const std::unordered_set<PlayerId>& stalenessMovers,
    std::unordered_map<PlayerId, BotMemory>& memories,
    HuntTiming& huntTiming) {

    std::unordered_map<PlayerId, PlayerId> pvpAttackerByTarget;
    std::unordered_set<PlayerId> jackalDamageTargets;
    std::optional<int> pendingEvadeEncounter;
    std::optional<CaveId> pendingEvadeOrigin;

    for (const auto& event : events) {
        auto styleStats = [&](PlayerId id) -> StyleStats* {
            const auto it = styles.find(id);
            return it == styles.end() ? nullptr : &stats.style[static_cast<std::size_t>(it->second)];
        };

        switch (event.type) {
            case GameEventType::ArrowFired: ++stats.arrowsFired; break;
            case GameEventType::ArrowMissed: ++stats.arrowMisses; break;
            case GameEventType::ArrowHitPlayer: ++stats.pvpHits; break;
            case GameEventType::PlayerDamaged:
                if (event.actor.has_value() && event.targetPlayer.has_value()) {
                    pvpAttackerByTarget[*event.targetPlayer] = *event.actor;
                } else if (event.targetPlayer.has_value() && jackalDamageTargets.contains(*event.targetPlayer)) {
                    ++stats.jackalDamageEvents;
                    stats.jackalDamageTotal += static_cast<std::uint64_t>(std::max(0, event.amount));
                }
                break;
            case GameEventType::ArrowHitJackal: ++stats.jackalHits; break;
            case GameEventType::ArrowReachedBasilisk:
                ++stats.basiliskEncounters;
                if (event.actor.has_value()) if (auto* ss = styleStats(*event.actor)) ++ss->basiliskEncounters;
                if (event.amount == 2 && huntTiming.firstEvadeRound.has_value()) {
                    const RoundNumber elapsed = state.round >= *huntTiming.firstEvadeRound
                        ? state.round - *huntTiming.firstEvadeRound : 0;
                    stats.firstReacquireRoundsTotal += static_cast<std::uint64_t>(elapsed);
                    ++stats.firstReacquisitions;
                    huntTiming.firstEvadeRound.reset();
                } else if (event.amount == 3 && huntTiming.secondEvadeRound.has_value()) {
                    const RoundNumber elapsed = state.round >= *huntTiming.secondEvadeRound
                        ? state.round - *huntTiming.secondEvadeRound : 0;
                    stats.secondReacquireRoundsTotal += static_cast<std::uint64_t>(elapsed);
                    ++stats.secondReacquisitions;
                    huntTiming.secondEvadeRound.reset();
                }
                break;
            case GameEventType::BasiliskEvaded:
                ++stats.basiliskEvades;
                pendingEvadeEncounter = event.amount;
                pendingEvadeOrigin = event.cave;
                if (event.amount == 1) huntTiming.firstEvadeRound = state.round;
                else if (event.amount == 2) huntTiming.secondEvadeRound = state.round;
                break;
            case GameEventType::BasiliskMoved:
                if (pendingEvadeEncounter.has_value() && pendingEvadeOrigin.has_value() && event.cave.has_value()) {
                    if (const auto distance = worldDistance(state.world, *pendingEvadeOrigin, *event.cave); distance.has_value()) {
                        const auto measured = static_cast<std::uint64_t>(*distance);
                        ++stats.evadeRelocations;
                        stats.relocationDistanceTotal += measured;
                        stats.relocationDistanceSamples.push_back(measured);
                        stats.maxRelocationDistance = std::max(stats.maxRelocationDistance, *distance);
                        if (*distance <= 1) ++stats.adjacentRelocations;
                        else if (*distance <= 3) ++stats.mediumRelocations;
                        else ++stats.longRelocations;

                        if (*pendingEvadeEncounter == 1) {
                            ++stats.firstRelocations;
                            stats.firstRelocationDistanceTotal += measured;
                        } else if (*pendingEvadeEncounter == 2) {
                            ++stats.secondRelocations;
                            stats.secondRelocationDistanceTotal += measured;
                        }
                    }
                    pendingEvadeEncounter.reset();
                    pendingEvadeOrigin.reset();
                }
                break;
            case GameEventType::PitTriggered:
                ++stats.pitDeaths;
                if (event.targetPlayer.has_value()) pitDeadPlayers.insert(*event.targetPlayer);
                break;
            case GameEventType::PitInvestigationSucceeded: ++stats.pitClueSuccesses; break;
            case GameEventType::PitInvestigationInconclusive: ++stats.pitClueInconclusive; break;
            case GameEventType::BodyCreated: ++stats.bodiesCreated; break;
            case GameEventType::BodyFound:
                ++stats.bodiesFound;
                if (event.actor.has_value()) if (auto* ss = styleStats(*event.actor)) ++ss->bodiesFound;
                break;
            case GameEventType::SigilAcquired:
                ++stats.sigilsAcquired;
                if (event.actor.has_value()) if (auto* ss = styleStats(*event.actor)) ++ss->sigilsAcquired;
                break;
            case GameEventType::ExtractionActivated: ++stats.extractionsActivated; break;
            case GameEventType::EscapeAvailable: ++stats.escapeAvailable; break;
            case GameEventType::PlayerEscaped: ++stats.escaped; break;
            case GameEventType::JackalMoved: ++stats.jackalMoves; break;
            case GameEventType::JackalStunned: ++stats.jackalStuns; break;
            case GameEventType::JackalRepelled: ++stats.jackalRepelled; break;
            case GameEventType::JackalRobbedArrow: ++stats.jackalRobberies; break;
            case GameEventType::JackalScaredPlayer: ++stats.jackalScares; break;
            case GameEventType::JackalKnockedOutPlayer:
                ++stats.jackalKnockouts;
                if (event.targetPlayer.has_value() && event.amount > 0)
                    jackalDamageTargets.insert(*event.targetPlayer);
                break;
            case GameEventType::SearchCompleted: ++stats.searches; break;
            case GameEventType::LooseArrowSpawned: ++stats.looseArrowSpawns; break;
            case GameEventType::ArrowFound:
                stats.arrowsFound += static_cast<std::uint64_t>(std::max(0, event.amount));
                if (event.actor.has_value()) {
                    const PlayerId actor = *event.actor;
                    if (zeroBefore.contains(actor)) ++stats.zeroArrowRecoveries;
                    if (stalenessMovers.contains(actor)) ++stats.stalenessArrowRecoveries;
                    if (event.cave.has_value() && memories[actor].rememberedLooseArrows.erase(*event.cave) > 0)
                        ++stats.rememberedArrowRecoveries;
                }
                break;
            case GameEventType::ItemFound:
                if (event.itemType.has_value()) ++stats.itemsFound[itemIndex(*event.itemType)];
                if (event.actor.has_value()) if (auto* ss = styleStats(*event.actor)) ++ss->itemsFound;
                break;
            case GameEventType::ItemUsed:
                if (event.itemType.has_value()) ++stats.itemsUsed[itemIndex(*event.itemType)];
                if (event.actor.has_value()) if (auto* ss = styleStats(*event.actor)) ++ss->itemsUsed;
                break;
            case GameEventType::InventoryFull: ++stats.inventoryFullDrops; break;
            case GameEventType::ExoticCallingCardFound: ++stats.exoticCallingCards; break;
            case GameEventType::PlayerHealed: stats.healedHp += static_cast<std::uint64_t>(std::max(0, event.amount)); break;
            case GameEventType::BasiliskBaitInfluencedMove: ++stats.baitInfluencedMoves; break;
            case GameEventType::BasiliskBehaviorChanged:
                if (event.basiliskBehavior.has_value()) {
                    switch (*event.basiliskBehavior) {
                        case BasiliskBehavior::Restless: ++stats.restlessAssignments; break;
                        case BasiliskBehavior::Lurker: ++stats.lurkerAssignments; break;
                        case BasiliskBehavior::Skittish: ++stats.skittishAssignments; break;
                        case BasiliskBehavior::Territorial: ++stats.territorialAssignments; break;
                        case BasiliskBehavior::Enraged: ++stats.enragedAssignments; break;
                        case BasiliskBehavior::Normal: break;
                    }
                }
                break;
            case GameEventType::PlayerKilled:
                if (event.targetPlayer.has_value()) {
                    const PlayerId dead = *event.targetPlayer;
                    if (auto* ss = styleStats(dead)) ++ss->deaths;

                    const bool pitDeath = pitDeadPlayers.contains(dead);
                    const bool basiliskContact = event.basiliskBehavior == BasiliskBehavior::Enraged;
                    const bool jackalDeath = jackalDamageTargets.contains(dead);

                    std::optional<PlayerId> killer = event.actor;
                    if (!killer.has_value() && !basiliskContact && !jackalDeath) {
                        const auto it = pvpAttackerByTarget.find(dead);
                        if (it != pvpAttackerByTarget.end()) killer = it->second;
                    }

                    if (pitDeath) {
                        // Counted by PitTriggered; do not double-attribute it.
                    } else if (basiliskContact) {
                        ++stats.basiliskContactKills;
                    } else if (jackalDeath) {
                        ++stats.jackalKnockoutDeaths;
                    } else if (killer.has_value()) {
                        ++stats.pvpDeaths;
                        if (auto* ss = styleStats(*killer)) ++ss->pvpKills;
                    } else {
                        ++stats.unattributedPlayerDeaths;
                    }
                }
                break;
            case GameEventType::BasiliskKilled:
                if (state.basilisk.trueEncounters <= 1) ++stats.firstKills;
                else if (state.basilisk.trueEncounters == 2) ++stats.secondKills;
                else ++stats.thirdKills;
                break;
            default: break;
        }
    }
}

void diagnoseStall(const MatchState& state, const std::vector<GameEvent>& events, Stats& stats) {
    bool allZero = true;
    bool anyLiving = false;
    bool pitOnly = false;
    for (const auto& player : state.players) {
        if (!player.alive) continue;
        anyLiving = true;
        allZero &= player.arrows == 0;
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, events);
        pitOnly |= hasAnyPitOnlyFrontier(snapshot) && !hasAnyMeaningfulFrontier(snapshot);

        if (player.arrows == 0) {
            std::optional<int> nearest;
            for (const CaveId arrow : state.looseArrows) {
                const auto distance = safeDistance(snapshot, arrow);
                if (distance.has_value() && (!nearest.has_value() || *distance < *nearest)) nearest = *distance;
            }
            if (nearest.has_value()) {
                ++stats.stalledZeroArrowHuntersWithReachableArrow;
                stats.nearestReachableArrowDistanceTotal += static_cast<std::uint64_t>(*nearest);
                ++stats.nearestReachableArrowDistanceSamples;
                stats.maxNearestReachableArrowDistance = std::max(stats.maxNearestReachableArrowDistance, *nearest);
            }
        }
    }
    if (anyLiving && allZero) ++stats.stalledAllZeroArrows;
    if (!state.looseArrows.empty()) ++stats.stalledLooseArrowsAvailable;
    if (pitOnly) ++stats.stalledPitOnlyFrontier;
}

void runOne(MapSeed mapSeed, MatchSeed matchSeed, std::uint64_t maxRounds, Stats& stats) {
    auto state = MapGenerator::generate(mapSeed, matchSeed);
    MatchCoordinator coordinator(state);
    std::unordered_map<PlayerId, Playstyle> styles;
    std::unordered_map<PlayerId, BotMemory> memories;
    std::unordered_set<PlayerId> pitDeadPlayers;
    std::vector<GameEvent> previousEvents;
    HuntTiming huntTiming;
    bool countedSecond = false, countedThird = false;

    for (const auto& player : state.players) {
        const Playstyle style = styleFor(matchSeed, player.id);
        styles[player.id] = style;
        ++stats.style[static_cast<std::size_t>(style)].assignments;
    }
    if (state.players.size() >= 2) {
        const auto a = static_cast<std::size_t>(styles[state.players[0].id]);
        const auto b = static_cast<std::size_t>(styles[state.players[1].id]);
        ++stats.matchups[a][b];
    }

    while (state.result.status == MatchStatus::Active && state.round <= maxRounds) {
        std::vector<PlayerAction> selected;
        std::unordered_set<PlayerId> zeroBefore;
        std::unordered_set<PlayerId> stalenessMovers;

        for (const auto& player : state.players) {
            if (!player.alive) continue;
            ++stats.style[static_cast<std::size_t>(styles[player.id])].roundsAlive;
            const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
            if (snapshot.arrows == 0) zeroBefore.insert(player.id);
            if (hasObs(snapshot, ObservationType::PitNearby)) ++stats.pitWarnings;

            bool stalenessMove = false;
            const auto action = chooseAction(styles[player.id], snapshot, memories[player.id], matchSeed, stats, stalenessMove);
            if (!action.has_value()) continue;
            selected.push_back(*action);
            auto& ss = stats.style[static_cast<std::size_t>(styles[player.id])];
            if (action->type == ActionType::Search) ++ss.searches;
            if (action->type == ActionType::Shoot) {
                ++ss.shots;
                if (hasObs(snapshot, ObservationType::RivalNearby)) ++ss.pvpShots;
            }
            if (stalenessMove) stalenessMovers.insert(player.id);
        }

        if (selected.empty()) break;
        bool submitOk = true;
        for (const auto& action : selected) submitOk &= coordinator.submitAction(action);
        if (!submitOk) break;
        bool lockOk = true;
        for (const auto& action : selected) lockOk &= coordinator.lockAction(action.player);
        if (!lockOk) break;

        previousEvents = coordinator.authoritativeEvents();
        collectEvents(previousEvents, state, stats, styles, pitDeadPlayers, zeroBefore, stalenessMovers, memories, huntTiming);

        if (!countedSecond && state.basilisk.trueEncounters >= 2) {
            ++stats.secondEncounterMatches;
            countedSecond = true;
        }
        if (!countedThird && state.basilisk.trueEncounters >= 3) {
            ++stats.thirdEncounterMatches;
            countedThird = true;
        }
    }

    ++stats.matches;
    const auto rounds = std::min<std::uint64_t>(state.round, maxRounds);
    stats.totalRounds += rounds;
    stats.roundSamples.push_back(rounds);
    for (const auto& player : state.players) {
        const auto snapshot = SnapshotSystem::buildForPlayer(state, player.id, previousEvents);
        stats.totalCaves += snapshot.map.caves.size();
        stats.totalFinalArrows += static_cast<std::uint64_t>(std::max(0, player.arrows));
    }

    if (state.result.status != MatchStatus::Completed) {
        ++stats.stalled;
        diagnoseStall(state, previousEvents, stats);
        return;
    }

    if (state.result.outcome == MatchOutcome::BasiliskKilled ||
        state.result.outcome == MatchOutcome::SimultaneousBasiliskKill) {
        const auto encounter = static_cast<std::size_t>(std::clamp(state.basilisk.trueEncounters, 1, 3));
        ++stats.basiliskDeathMatchesByEncounter[encounter];
        stats.basiliskDeathRoundsByEncounter[encounter] += rounds;
    }

    ++stats.completed;
    switch (state.result.outcome) {
        case MatchOutcome::BasiliskKilled:
            ++stats.basiliskWins;
            if (state.result.winner.has_value()) {
                auto& ss = stats.style[static_cast<std::size_t>(styles[*state.result.winner])];
                ++ss.wins;
                ++ss.basiliskWins;
            }
            break;
        case MatchOutcome::SimultaneousBasiliskKill:
            ++stats.simultaneousBasiliskDraws;
            break;
        case MatchOutcome::EscapedWithSigil:
            ++stats.extractionWins;
            if (state.result.winner.has_value()) {
                auto& ss = stats.style[static_cast<std::size_t>(styles[*state.result.winner])];
                ++ss.wins;
                ++ss.extractionWins;
            }
            break;
        case MatchOutcome::Draw:
            ++stats.draws;
            if (pitDeadPlayers.size() >= 2) ++stats.mutualPitDraws;
            break;
        case MatchOutcome::None: break;
    }
}

void printPercent(const char* label, std::uint64_t value, std::uint64_t total) {
    const double pct = total == 0 ? 0.0 : 100.0 * static_cast<double>(value) / static_cast<double>(total);
    std::cout << label << ": " << value << " (" << pct << "%)\n";
}

std::uint64_t percentile(std::vector<std::uint64_t> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1))];
}

double rate(std::uint64_t numerator, std::uint64_t denominator) {
    return denominator == 0 ? 0.0 : 100.0 * static_cast<double>(numerator) / static_cast<double>(denominator);
}

void printReport(const Stats& stats, std::uint64_t maxRounds) {
    const Rules rules{};
    const double avgRounds = stats.matches ? static_cast<double>(stats.totalRounds) / stats.matches : 0.0;
    const double avgCaves = stats.matches ? static_cast<double>(stats.totalCaves) / (stats.matches * 2.0) : 0.0;
    const double avgArrows = stats.matches ? static_cast<double>(stats.totalFinalArrows) / (stats.matches * 2.0) : 0.0;

    std::cout << "BEWARE THE BASILISK V2 - SIMULATION REPORT (BOT V3.8 JACKAL DAMAGE)\n";
    std::cout << "Matches: " << stats.matches << " | max rounds/match: " << maxRounds
              << " | loose-arrow cap: " << rules.maxLooseArrows
              << " | spawn cadence: every " << rules.looseArrowSpawnIntervalRounds << " rounds"
              << " | PvP arrow damage: " << rules.arrowDamage
              << " | heal: " << rules.healingAmount
              << " | Jackal knockout damage: " << rules.jackalDamageMin << " HP\n\n";

    std::cout << "OUTCOMES\n";
    printPercent("Completed", stats.completed, stats.matches);
    printPercent("Stalled at round cap", stats.stalled, stats.matches);
    printPercent("Basilisk-defeat wins", stats.basiliskWins, stats.matches);
    printPercent("Simultaneous Basilisk-defeat draws", stats.simultaneousBasiliskDraws, stats.matches);
    printPercent("Extraction wins", stats.extractionWins, stats.matches);
    printPercent("Other draws", stats.draws, stats.matches);

    std::cout << "\nMATCH LENGTH / EXPLORATION\n";
    std::cout << "Average rounds: " << avgRounds << '\n';
    std::cout << "Median rounds: " << percentile(stats.roundSamples, .50) << '\n';
    std::cout << "P90 rounds: " << percentile(stats.roundSamples, .90) << '\n';
    std::cout << "P95 rounds: " << percentile(stats.roundSamples, .95) << '\n';
    std::cout << "Minimum rounds: " << percentile(stats.roundSamples, 0.0) << '\n';
    std::cout << "Maximum rounds: " << percentile(stats.roundSamples, 1.0) << '\n';
    std::cout << "Average caves discovered/hunter: " << avgCaves << '\n';
    std::cout << "Frontier-seeking moves: " << stats.frontierMoves << '\n';
    std::cout << "Unexplored moves: " << stats.unexploredMoves << '\n';
    std::cout << "Known-route moves: " << stats.knownMoves << '\n';

    std::cout << "\nPLAYSTYLE PERFORMANCE\n";
    for (std::size_t i = 0; i < kStyleCount; ++i) {
        const auto style = static_cast<Playstyle>(i);
        const auto& s = stats.style[i];
        std::cout << styleName(style)
                  << " | assigned=" << s.assignments
                  << " wins=" << s.wins << " (" << std::fixed << std::setprecision(1) << rate(s.wins, s.assignments) << "%)"
                  << " basilisk=" << s.basiliskWins
                  << " extraction=" << s.extractionWins
                  << " searches=" << s.searches
                  << " shots=" << s.shots
                  << " pvpShots=" << s.pvpShots
                  << " pvpKills=" << s.pvpKills
                  << " deaths=" << s.deaths
                  << " basiliskEncounters=" << s.basiliskEncounters
                  << " bodiesFound=" << s.bodiesFound
                  << " sigils=" << s.sigilsAcquired
                  << " itemsFound=" << s.itemsFound
                  << " itemsUsed=" << s.itemsUsed
                  << " avgRoundsAlive=" << (s.assignments ? static_cast<double>(s.roundsAlive) / s.assignments : 0.0)
                  << '\n';
    }

    std::cout << "\nMATCHUP COUNTS (rows=P1, columns=P2)\n";
    std::cout << "             Hunter Scavenger Opportunist Extractor\n";
    for (std::size_t row = 0; row < kStyleCount; ++row) {
        std::cout << std::setw(11) << styleName(static_cast<Playstyle>(row)) << ' ';
        for (std::size_t col = 0; col < kStyleCount; ++col) std::cout << std::setw(10) << stats.matchups[row][col];
        std::cout << '\n';
    }

    std::cout << "\nSCAVENGER CONVERSION TELEMETRY\n";
    std::cout << "Stocked conversion player-rounds: " << stats.scavengerStockedRounds << '\n';
    std::cout << "Resource-need searches: " << stats.scavengerResourceSearches << '\n';
    std::cout << "Periodic first-visit searches: " << stats.scavengerPeriodicSearches << '\n';

    std::cout << "\nPIT / HAZARD TELEMETRY\n";
    std::cout << "Pit warning player-rounds: " << stats.pitWarnings << '\n';
    std::cout << "Investigation actions: " << stats.pitInvestigations << '\n';
    std::cout << "Directional successes: " << stats.pitClueSuccesses << '\n';
    std::cout << "Inconclusive results: " << stats.pitClueInconclusive << '\n';
    std::cout << "Known dangerous-tunnel moves rejected: " << stats.knownPitTunnelAvoidances << '\n';
    std::cout << "Pit deaths: " << stats.pitDeaths << '\n';
    std::cout << "Mutual-Pit draws: " << stats.mutualPitDraws << '\n';

    std::cout << "\nBASILISK TELEMETRY\n";
    std::cout << "True-encounter arrows: " << stats.basiliskEncounters << '\n';
    std::cout << "Evades: " << stats.basiliskEvades << '\n';
    std::cout << "First/second/third encounter Basilisk defeats: " << stats.firstKills << '/' << stats.secondKills << '/' << stats.thirdKills << '\n';
    std::cout << "Matches reaching second/third encounter: " << stats.secondEncounterMatches << '/' << stats.thirdEncounterMatches << '\n';
    std::cout << "Restless/Lurker/Skittish/Territorial/Enraged assignments: "
              << stats.restlessAssignments << '/' << stats.lurkerAssignments << '/' << stats.skittishAssignments << '/'
              << stats.territorialAssignments << '/' << stats.enragedAssignments << '\n';
    std::cout << "Enraged Basilisk contact kills: " << stats.basiliskContactKills << '\n';

    const double avgRelocation = stats.evadeRelocations
        ? static_cast<double>(stats.relocationDistanceTotal) / stats.evadeRelocations : 0.0;
    const double avgFirstRelocation = stats.firstRelocations
        ? static_cast<double>(stats.firstRelocationDistanceTotal) / stats.firstRelocations : 0.0;
    const double avgSecondRelocation = stats.secondRelocations
        ? static_cast<double>(stats.secondRelocationDistanceTotal) / stats.secondRelocations : 0.0;
    const double avgFirstReacquire = stats.firstReacquisitions
        ? static_cast<double>(stats.firstReacquireRoundsTotal) / stats.firstReacquisitions : 0.0;
    const double avgSecondReacquire = stats.secondReacquisitions
        ? static_cast<double>(stats.secondReacquireRoundsTotal) / stats.secondReacquisitions : 0.0;

    std::cout << "\nEVADE RELOCATION / HUNT-LENGTH TELEMETRY\n";
    std::cout << "Immediate evade relocations: " << stats.evadeRelocations << '\n';
    std::cout << "Average relocation distance: " << avgRelocation << " caves\n";
    std::cout << "Median relocation distance: " << percentile(stats.relocationDistanceSamples, .50) << '\n';
    std::cout << "P90 relocation distance: " << percentile(stats.relocationDistanceSamples, .90) << '\n';
    std::cout << "Maximum relocation distance: " << stats.maxRelocationDistance << '\n';
    std::cout << "Adjacent / 2-3 cave / 4+ cave relocations: "
              << stats.adjacentRelocations << '/' << stats.mediumRelocations << '/' << stats.longRelocations << '\n';
    std::cout << "First-evade average relocation distance: " << avgFirstRelocation << " caves\n";
    std::cout << "Second-evade average relocation distance: " << avgSecondRelocation << " caves\n";
    std::cout << "First-evade reacquisitions measured: " << stats.firstReacquisitions << '/' << stats.firstRelocations << '\n';
    std::cout << "Average rounds first evade -> next true encounter: " << avgFirstReacquire << '\n';
    std::cout << "Second-evade reacquisitions measured: " << stats.secondReacquisitions << '/' << stats.secondRelocations << '\n';
    std::cout << "Average rounds second evade -> next true encounter: " << avgSecondReacquire << '\n';
    std::cout << "Basilisk-death matches by 1/2/3 encounters: "
              << stats.basiliskDeathMatchesByEncounter[1] << '/'
              << stats.basiliskDeathMatchesByEncounter[2] << '/'
              << stats.basiliskDeathMatchesByEncounter[3] << '\n';
    std::cout << "Average rounds for 1/2/3-encounter Basilisk-death matches: ";
    for (std::size_t encounter = 1; encounter <= 3; ++encounter) {
        if (encounter > 1) std::cout << '/';
        const auto count = stats.basiliskDeathMatchesByEncounter[encounter];
        const double average = count
            ? static_cast<double>(stats.basiliskDeathRoundsByEncounter[encounter]) / count : 0.0;
        std::cout << average;
    }
    std::cout << '\n';

    std::cout << "\nPLAYER DEATH ATTRIBUTION\n";
    std::cout << "Pit deaths: " << stats.pitDeaths << '\n';
    std::cout << "Enraged Basilisk contact kills: " << stats.basiliskContactKills << '\n';
    std::cout << "Jackal knockout deaths: " << stats.jackalKnockoutDeaths << '\n';
    std::cout << "PvP deaths: " << stats.pvpDeaths << '\n';
    std::cout << "Other/unattributed player deaths: " << stats.unattributedPlayerDeaths << '\n';

    std::cout << "\nOBJECTIVE TELEMETRY\n";
    std::cout << "Bodies created/found: " << stats.bodiesCreated << '/' << stats.bodiesFound << '\n';
    std::cout << "Body discovery rate: " << rate(stats.bodiesFound, stats.bodiesCreated) << "%\n";
    std::cout << "Sigils acquired: " << stats.sigilsAcquired << '\n';
    std::cout << "Objective-driven searches: " << stats.objectiveSearches << '\n';
    std::cout << "Extractions activated: " << stats.extractionsActivated << '\n';
    std::cout << "Extraction path moves: " << stats.extractionPathMoves << '\n';
    std::cout << "Escape available / players escaped: " << stats.escapeAvailable << '/' << stats.escaped << '\n';
    std::cout << "Sigil -> escape conversion: " << rate(stats.escaped, stats.sigilsAcquired) << "%\n";

    std::cout << "\nJACKAL TELEMETRY\n";
    std::cout << "Moves: " << stats.jackalMoves << '\n';
    std::cout << "Robberies/scares/knockouts/stuns: " << stats.jackalRobberies << '/' << stats.jackalScares << '/'
              << stats.jackalKnockouts << '/' << stats.jackalStuns << '\n';
    std::cout << "Repelled attacks: " << stats.jackalRepelled << '\n';
    std::cout << "Jackal arrow hits: " << stats.jackalHits << '\n';

    std::cout << "\nJACKAL DAMAGE TELEMETRY\n";
    std::cout << "Knockout damage events: " << stats.jackalDamageEvents << '\n';
    std::cout << "Total knockout damage: " << stats.jackalDamageTotal << '\n';
    std::cout << "Average knockout damage: "
              << (stats.jackalDamageEvents ? static_cast<double>(stats.jackalDamageTotal) / stats.jackalDamageEvents : 0.0)
              << '\n';
    std::cout << "Jackal knockout deaths: " << stats.jackalKnockoutDeaths << '\n';

    std::cout << "\nSEARCH LOOT / ITEM TELEMETRY\n";
    for (std::size_t i = 0; i < kItemCount; ++i) {
        const auto item = static_cast<ItemType>(i);
        std::cout << itemName(item) << " found/used: " << stats.itemsFound[i] << '/' << stats.itemsUsed[i] << '\n';
    }
    std::cout << "Inventory-full rejected drops: " << stats.inventoryFullDrops << '\n';
    std::cout << "Exotic Calling Card prototype triggers: " << stats.exoticCallingCards << '\n';
    std::cout << "Total HP restored: " << stats.healedHp << '\n';
    std::cout << "Blood Bait influenced Basilisk moves: " << stats.baitInfluencedMoves << '\n';

    std::cout << "\nAMMO ECONOMY / RECOVERY TELEMETRY\n";
    std::cout << "Loose arrows spawned: " << stats.looseArrowSpawns << '\n';
    std::cout << "Arrows found: " << stats.arrowsFound << " (" << rate(stats.arrowsFound, stats.looseArrowSpawns) << "% of spawned)\n";
    std::cout << "Arrows fired: " << stats.arrowsFired << '\n';
    std::cout << "Average arrows remaining/hunter: " << avgArrows << '\n';
    std::cout << "Zero-arrow player-rounds: " << stats.zeroArrowPlayerRounds << '\n';
    std::cout << "Zero-arrow recoveries: " << stats.zeroArrowRecoveries << '\n';
    std::cout << "Loose arrows spotted while at capacity: " << stats.looseArrowSightingsAtCapacity << '\n';
    std::cout << "Remembered-arrow pursuit moves/recoveries/invalidations: "
              << stats.rememberedArrowPursuitMoves << '/' << stats.rememberedArrowRecoveries << '/' << stats.rememberedArrowInvalidations << '\n';
    std::cout << "Staleness-patrol moves: " << stats.stalenessPatrolMoves << '\n';
    std::cout << "Staleness targets selected: " << stats.stalenessTargetsSelected << '\n';
    std::cout << "Average oldest-target age: " << (stats.stalenessTargetsSelected ?
        static_cast<double>(stats.stalenessTargetAgeTotal) / stats.stalenessTargetsSelected : 0.0) << '\n';
    std::cout << "Arrow recoveries during staleness patrol: " << stats.stalenessArrowRecoveries << '\n';

    std::cout << "\nSHOT DISCIPLINE / PVP TELEMETRY\n";
    std::cout << "Restless-noise shots suppressed: " << stats.restlessShotsSuppressed << '\n';
    std::cout << "Last-arrow PvP shots suppressed: " << stats.lastArrowPvpShotsSuppressed << '\n';
    std::cout << "Adjacent-Basilisk shots: " << stats.adjacentBasiliskShots << '\n';
    std::cout << "Exact Enraged shots: " << stats.exactEnragedShots << '\n';
    std::cout << "PvP shots taken: " << stats.pvpShots << '\n';
    std::cout << "PvP hits/deaths: " << stats.pvpHits << '/' << stats.pvpDeaths << '\n';
    std::cout << "PvP hit -> death conversion: " << rate(stats.pvpDeaths, stats.pvpHits) << "%\n";
    std::cout << "Arrow misses: " << stats.arrowMisses << '\n';

    std::cout << "\nSTALL / CONVERGENCE TELEMETRY\n";
    std::cout << "Stalled matches: " << stats.stalled << '\n';
    std::cout << "Stalled with all living hunters at zero arrows: " << stats.stalledAllZeroArrows << '\n';
    std::cout << "Stalled with loose arrows still on map: " << stats.stalledLooseArrowsAvailable << '\n';
    std::cout << "Stalled with only Pit frontier remaining: " << stats.stalledPitOnlyFrontier << '\n';
    std::cout << "Stalled zero-arrow hunters with safely reachable arrows: " << stats.stalledZeroArrowHuntersWithReachableArrow << '\n';
    const double avgNearest = stats.nearestReachableArrowDistanceSamples ?
        static_cast<double>(stats.nearestReachableArrowDistanceTotal) / stats.nearestReachableArrowDistanceSamples : 0.0;
    std::cout << "Average nearest reachable arrow distance at cap: " << avgNearest << '\n';
    std::cout << "Maximum nearest reachable arrow distance at cap: " << stats.maxNearestReachableArrowDistance << '\n';
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t matches = 1000, maxRounds = 250;
    MapSeed firstMapSeed = 100000;
    MatchSeed firstMatchSeed = 500000;
    if (argc > 1) matches = std::stoull(argv[1]);
    if (argc > 2) maxRounds = std::stoull(argv[2]);
    if (argc > 3) firstMapSeed = static_cast<MapSeed>(std::stoull(argv[3]));
    if (argc > 4) firstMatchSeed = static_cast<MatchSeed>(std::stoull(argv[4]));

    Stats stats;
    for (std::uint64_t i = 0; i < matches; ++i)
        runOne(firstMapSeed + static_cast<MapSeed>(i), firstMatchSeed + static_cast<MatchSeed>(i), maxRounds, stats);

    printReport(stats, maxRounds);
    return 0;
}
