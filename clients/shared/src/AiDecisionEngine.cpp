#include "basilisk/client/ai/AiDecisionEngine.hpp"

#include <algorithm>
#include <limits>

#include "basilisk/client/RoutePlanner.hpp"

namespace basilisk::client::ai {
namespace {

std::uint64_t mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

double baseUtility(const AvailableAction& action, const PlayerRoundSnapshot& snapshot) {
    switch (action.type) {
        case ActionType::Move: return action.targetTunnel ? 64.0 : 55.0;
        case ActionType::Search: return 61.0;
        case ActionType::Shoot: return snapshot.arrows > 0 ? 57.0 : -1000.0;
        case ActionType::UseItem:
            if (action.targetItem == ItemType::HealingDraught)
                return snapshot.health < snapshot.maxHealth ? 82.0 : 20.0;
            if (action.targetItem == ItemType::SurveyFragment) return 70.0;
            if (action.targetItem == ItemType::OldMinersMap) return 66.0;
            if (action.targetItem == ItemType::OldHuntersMap) return 65.0;
            return 58.0;
        case ActionType::Contextual: return 95.0;
    }
    return 0.0;
}

double behaviorUtility(
    const AvailableAction& action,
    const PlayerRoundSnapshot& snapshot,
    AiBehavior behavior) {
    switch (behavior) {
        case AiBehavior::Balanced: return 0.0;
        case AiBehavior::Explorer:
            if (action.type == ActionType::Search) return 28.0;
            if (action.type == ActionType::Move && action.targetTunnel) return 24.0;
            if (action.targetItem == ItemType::SurveyFragment) return 25.0;
            return 0.0;
        case AiBehavior::Aggressive:
            if (action.type == ActionType::Shoot) return 35.0;
            if (action.type == ActionType::Move) return 9.0;
            return 0.0;
        case AiBehavior::ObjectiveFocused:
            if (action.type == ActionType::Contextual) return 40.0;
            if (snapshot.hasHunterSigil && action.type == ActionType::Move) return 22.0;
            if (action.targetItem == ItemType::OldHuntersMap) return 20.0;
            return 0.0;
        case AiBehavior::Survivalist:
            if (action.targetItem == ItemType::HealingDraught) return 45.0;
            if (action.type == ActionType::Shoot) return -12.0;
            if (snapshot.health * 2 < snapshot.maxHealth && action.type == ActionType::Move)
                return 15.0;
            return 0.0;
        case AiBehavior::Opportunist:
            if (action.type == ActionType::UseItem) return 25.0;
            if (action.type == ActionType::Search) return 18.0;
            if (action.type == ActionType::Shoot) return 12.0;
            return 0.0;
        case AiBehavior::Random: break;
    }
    return 0.0;
}

double noise(const AiConfig& config, RoundNumber round, std::size_t index) {
    const std::uint64_t value = mix(config.seed ^
        (static_cast<std::uint64_t>(config.player) << 17U) ^
        (static_cast<std::uint64_t>(round) << 32U) ^ index ^ 0x4445434953494f4eULL);
    const double unit = static_cast<double>(value % 10001ULL) / 10000.0;
    const double amplitude = config.difficulty == AiDifficulty::Easy ? 42.0 :
        config.difficulty == AiDifficulty::Medium ? 13.0 : 2.0;
    return (unit * 2.0 - 1.0) * amplitude;
}

const TunnelView* actionTunnel(
    const PlayerRoundSnapshot& snapshot, const AvailableAction& action) {
    const auto cave = std::find_if(snapshot.map.caves.begin(), snapshot.map.caves.end(),
        [&](const DiscoveredCaveView& candidate) {
            return candidate.cave == snapshot.currentCave;
        });
    if (cave == snapshot.map.caves.end()) return nullptr;
    const auto tunnel = std::find_if(cave->exits.begin(), cave->exits.end(),
        [&](const TunnelView& candidate) {
            return (action.targetTunnel && candidate.id == *action.targetTunnel) ||
                (action.targetCave && candidate.destination == action.targetCave);
        });
    return tunnel == cave->exits.end() ? nullptr : &*tunnel;
}

bool carelessThisRound(const AiConfig& config, RoundNumber round) {
    if (config.difficulty == AiDifficulty::Hard) return false;
    const std::uint64_t roll = mix(config.seed ^
        (static_cast<std::uint64_t>(config.player) << 13U) ^
        (static_cast<std::uint64_t>(round) << 37U) ^ 0x434152454c455353ULL) % 100ULL;
    return roll < (config.difficulty == AiDifficulty::Easy ? 20ULL : 5ULL);
}

bool confirmedPitMove(
    const PlayerRoundSnapshot& snapshot, const AvailableAction& action,
    const AiKnowledgeState& knowledge) {
    if (action.type != ActionType::Move) return false;
    if (action.targetCave && knowledge.isConfirmedPit(*action.targetCave)) return true;
    if (action.targetTunnel && knowledge.isConfirmedPitExit(
            {snapshot.currentCave, *action.targetTunnel})) return true;
    const TunnelView* tunnel = actionTunnel(snapshot, action);
    return tunnel != nullptr && tunnel->strongColdDraft;
}

bool passesHardSafetyFilter(
    const PlayerRoundSnapshot& snapshot,
    const AvailableAction& action,
    const AiConfig& config,
    const AiKnowledgeState& knowledge) {
    return config.difficulty != AiDifficulty::Hard ||
        !confirmedPitMove(snapshot, action, knowledge);
}

bool uncertainPitMove(
    const PlayerRoundSnapshot& snapshot, const AvailableAction& action,
    const AiKnowledgeState& knowledge) {
    if (action.type != ActionType::Move || confirmedPitMove(snapshot, action, knowledge))
        return false;
    if (action.targetCave) return knowledge.isPitCandidate(*action.targetCave);
    return action.targetTunnel.has_value() && knowledge.pitWarningHere();
}

bool avoidedJackalRoute(
    const PlayerRoundSnapshot& snapshot, const AvailableAction& action,
    const AiKnowledgeState& knowledge) {
    if (action.type != ActionType::Move) return false;
    if (action.targetCave)
        return knowledge.temporarilyAvoids(
            snapshot.currentCave, *action.targetCave, snapshot.round);
    return false;
}

bool isBacktrack(const AvailableAction& action, const AiKnowledgeState& knowledge) {
    return action.type == ActionType::Move && action.targetCave &&
        knowledge.previousCave() == action.targetCave;
}

bool pitUncertaintyBlocksProgress(
    const PlayerRoundSnapshot& snapshot, const AiKnowledgeState& knowledge) {
    if (!knowledge.pitWarningHere() || knowledge.unresolvedPitCandidateCount() == 0)
        return false;
    bool hasMove = false;
    bool hasSafeMove = false;
    for (const AvailableAction& action : snapshot.availableActions) {
        if (action.type != ActionType::Move) continue;
        hasMove = true;
        if (!confirmedPitMove(snapshot, action, knowledge) &&
            !uncertainPitMove(snapshot, action, knowledge)) hasSafeMove = true;
    }
    return hasMove && !hasSafeMove;
}

double awarenessAdjustment(
    const AvailableAction& action, const PlayerRoundSnapshot& snapshot,
    const AiConfig& config, const AiKnowledgeState& knowledge,
    bool careless) {
    const bool confirmedPit = confirmedPitMove(snapshot, action, knowledge);
    if (config.difficulty == AiDifficulty::Hard && confirmedPit)
        return -std::numeric_limits<double>::infinity();
    const bool pitUncertain = uncertainPitMove(snapshot, action, knowledge);
    const bool jackalAvoid = avoidedJackalRoute(snapshot, action, knowledge);
    const bool backtrack = isBacktrack(action, knowledge);
    if (careless) {
        const bool relevantRisk = confirmedPit || pitUncertain || jackalAvoid ||
            (knowledge.basiliskWarningHere() && action.type == ActionType::Move);
        return relevantRisk ? 120.0 : 0.0;
    }

    double adjustment = 0.0;
    if (confirmedPit)
        adjustment -= config.difficulty == AiDifficulty::Easy ? 85.0 : 180.0;
    if (pitUncertain)
        adjustment -= config.difficulty == AiDifficulty::Easy ? 28.0 :
            config.difficulty == AiDifficulty::Medium ? 75.0 : 150.0;

    if (knowledge.basiliskWarningHere() && action.type == ActionType::Move) {
        adjustment -= config.difficulty == AiDifficulty::Easy ? 24.0 :
            config.difficulty == AiDifficulty::Medium ? 62.0 : 105.0;
        if (backtrack) adjustment += config.difficulty == AiDifficulty::Hard ? 58.0 : 28.0;
        if (config.difficulty == AiDifficulty::Hard &&
            knowledge.basiliskWarningStreak() > 1) {
            if (!backtrack)
                adjustment -= 35.0 * static_cast<double>(std::min<std::size_t>(
                    knowledge.basiliskWarningStreak() - 1, 3));
            else if (config.behavior == AiBehavior::Survivalist)
                adjustment += 35.0;
        }
    }
    if (knowledge.basiliskWarningHere() && action.type == ActionType::Shoot &&
        config.behavior == AiBehavior::Aggressive)
        adjustment += config.difficulty == AiDifficulty::Hard ? 80.0 : 25.0;

    if (knowledge.jackalWarningHere() && backtrack)
        adjustment += config.difficulty == AiDifficulty::Easy ? 12.0 :
            config.difficulty == AiDifficulty::Medium ? 42.0 : 62.0;
    if (jackalAvoid)
        adjustment -= config.difficulty == AiDifficulty::Easy ? 10.0 :
            config.difficulty == AiDifficulty::Medium ? 65.0 : 145.0;

    if (config.difficulty == AiDifficulty::Hard && action.type == ActionType::Search &&
        pitUncertaintyBlocksProgress(snapshot, knowledge)) adjustment += 95.0;
    return adjustment;
}

std::size_t basiliskCandidateCount(
    const PlayerRoundSnapshot& snapshot,
    const AiKnowledgeState& knowledge);
bool confirmedPitTarget(
    const PlayerRoundSnapshot& snapshot,
    const AvailableAction& action,
    const AiKnowledgeState& knowledge);
bool searchCanNarrowBasilisk(
    const PlayerRoundSnapshot& snapshot,
    const AiKnowledgeState& knowledge);

double searchInformationAdjustment(
    const AvailableAction& action,
    const PlayerRoundSnapshot& snapshot,
    const AiConfig& config,
    const AiKnowledgeState& knowledge) {
    if (action.type != ActionType::Search) return 0.0;
    const std::size_t repeats = knowledge.repeatedSearchCount();

    double basiliskAdjustment = 0.0;
    if (config.difficulty == AiDifficulty::Hard &&
        knowledge.basiliskWarningHere()) {
        if (searchCanNarrowBasilisk(snapshot, knowledge)) {
            return config.behavior == AiBehavior::ObjectiveFocused ? 155.0 : 125.0;
        }
        if (basiliskCandidateCount(snapshot, knowledge) > 1)
            basiliskAdjustment = -70.0;
    }

    // A blocked Pit investigation is still expected to produce useful
    // player-safe information. Hard AI may keep investigating until that
    // uncertainty is resolved; the other difficulties remain less disciplined.
    if (config.difficulty == AiDifficulty::Hard &&
        pitUncertaintyBlocksProgress(snapshot, knowledge)) return 0.0;

    const double alreadySearchedPenalty = knowledge.hasSearched(snapshot.currentCave)
        ? (config.difficulty == AiDifficulty::Easy ? 8.0 :
            config.difficulty == AiDifficulty::Medium ? 30.0 : 50.0)
        : 0.0;
    if (repeats == 0 && alreadySearchedPenalty == 0.0)
        return basiliskAdjustment;
    const double perRepeat = config.difficulty == AiDifficulty::Easy ? 10.0 :
        config.difficulty == AiDifficulty::Medium ? 28.0 : 45.0;
    const std::size_t capped = std::min<std::size_t>(repeats, 5);
    return basiliskAdjustment - alreadySearchedPenalty -
        perRepeat * static_cast<double>(capped);
}

bool sameAction(const AvailableAction& left, const AvailableAction& right) {
    return left.type == right.type && left.targetCave == right.targetCave &&
        left.targetTunnel == right.targetTunnel &&
        left.targetItem == right.targetItem &&
        left.contextualAction == right.contextualAction;
}

PlayerMapView knownSafeExtractionMap(
    const PlayerRoundSnapshot& snapshot,
    const AiKnowledgeState& knowledge) {
    PlayerMapView safe = snapshot.map;
    safe.caves.erase(std::remove_if(safe.caves.begin(), safe.caves.end(),
        [&](const DiscoveredCaveView& cave) {
            return knowledge.isConfirmedPit(cave.cave);
        }), safe.caves.end());
    for (DiscoveredCaveView& cave : safe.caves) {
        cave.exits.erase(std::remove_if(cave.exits.begin(), cave.exits.end(),
            [&](const TunnelView& tunnel) {
                return (tunnel.destination && knowledge.isConfirmedPit(*tunnel.destination)) ||
                    knowledge.isConfirmedPitExit({cave.cave, tunnel.id}) ||
                    tunnel.strongColdDraft;
            }), cave.exits.end());
    }
    return safe;
}

std::optional<AvailableAction> extractionWinAction(
    const PlayerRoundSnapshot& snapshot,
    const AiConfig& config,
    const AiKnowledgeState& knowledge) {
    if (config.difficulty == AiDifficulty::Hard && snapshot.hasHunterSigil &&
        snapshot.extractionCave) {
        const PlayerMapView safeMap = knownSafeExtractionMap(snapshot, knowledge);
        const auto route = client_navigation::planKnownRoute(
            safeMap, *snapshot.extractionCave);
        if (route.status == client_navigation::KnownRouteStatus::Reachable &&
            route.caves.size() >= 2) {
            const CaveId next = route.caves[1];
            const auto move = std::find_if(snapshot.availableActions.begin(),
                snapshot.availableActions.end(), [&](const AvailableAction& action) {
                    return action.type == ActionType::Move && action.targetCave == next &&
                        !confirmedPitMove(snapshot, action, knowledge);
                });
            if (move != snapshot.availableActions.end()) return *move;
        }
    }

    return std::nullopt;
}

std::optional<AvailableAction> immediateEscapeAction(
    const PlayerRoundSnapshot& snapshot,
    const AiConfig& config) {
    if (config.difficulty != AiDifficulty::Hard || !snapshot.hasHunterSigil ||
        !snapshot.extractionCave) return std::nullopt;
    const auto escape = std::find_if(snapshot.availableActions.begin(),
        snapshot.availableActions.end(), [](const AvailableAction& action) {
            return action.type == ActionType::Contextual &&
                action.contextualAction == ContextualActionType::Escape;
        });
    return escape == snapshot.availableActions.end()
        ? std::nullopt : std::optional<AvailableAction>{*escape};
}

std::size_t shootTargetCount(const PlayerRoundSnapshot& snapshot) {
    return static_cast<std::size_t>(std::count_if(
        snapshot.availableActions.begin(), snapshot.availableActions.end(),
        [](const AvailableAction& action) { return action.type == ActionType::Shoot; }));
}

bool confirmedPitTarget(
    const PlayerRoundSnapshot& snapshot,
    const AvailableAction& action,
    const AiKnowledgeState& knowledge) {
    if (action.type != ActionType::Shoot) return false;
    if (action.targetCave && knowledge.isConfirmedPit(*action.targetCave)) return true;
    return action.targetTunnel && knowledge.isConfirmedPitExit(
        {snapshot.currentCave, *action.targetTunnel});
}

std::size_t basiliskCandidateCount(
    const PlayerRoundSnapshot& snapshot,
    const AiKnowledgeState& knowledge) {
    return static_cast<std::size_t>(std::count_if(
        snapshot.availableActions.begin(), snapshot.availableActions.end(),
        [&](const AvailableAction& action) {
            return action.type == ActionType::Shoot &&
                !confirmedPitTarget(snapshot, action, knowledge) &&
                !knowledge.isDisprovenBasiliskTarget(snapshot.currentCave, action);
        }));
}

bool searchCanNarrowBasilisk(
    const PlayerRoundSnapshot& snapshot,
    const AiKnowledgeState& knowledge) {
    return knowledge.basiliskWarningHere() && knowledge.pitWarningHere() &&
        knowledge.unresolvedPitCandidateCount() > 0 &&
        basiliskCandidateCount(snapshot, knowledge) > 1;
}

std::optional<AvailableAction> hardBasiliskEngagementShot(
    const PlayerRoundSnapshot& snapshot,
    const AiKnowledgeState& knowledge,
    const AiConfig& config) {
    if (config.difficulty != AiDifficulty::Hard || snapshot.arrows <= 0 ||
        !knowledge.basiliskWarningHere()) return std::nullopt;
    std::vector<AvailableAction> plausible;
    for (const AvailableAction& action : snapshot.availableActions) {
        if (action.type == ActionType::Shoot &&
            !confirmedPitTarget(snapshot, action, knowledge))
            plausible.push_back(action);
    }
    if (plausible.empty()) return std::nullopt;

    const auto preferred = std::find_if(plausible.begin(), plausible.end(),
        [&](const AvailableAction& action) {
            return knowledge.isPreferredBasiliskTarget(snapshot.currentCave, action) &&
                !knowledge.isDisprovenBasiliskTarget(snapshot.currentCave, action);
        });
    if (preferred != plausible.end()) return *preferred;

    const auto untried = std::find_if(plausible.begin(), plausible.end(),
        [&](const AvailableAction& action) {
            return !knowledge.isDisprovenBasiliskTarget(snapshot.currentCave, action);
        });
    if (untried != plausible.end()) return *untried;

    // Persistent adjacent evidence means the context is still live even when
    // every current branch has one miss. Reconcile by starting a new
    // deterministic pass rather than dropping back into ordinary roaming.
    return plausible.front();
}

int dualObjectivePriority(
    const PlayerRoundSnapshot& snapshot,
    const AiKnowledgeState& knowledge,
    const AvailableAction& action) {
    const bool survivalCritical = snapshot.health * 4 <= snapshot.maxHealth;
    if (!survivalCritical && action.type == ActionType::Search &&
        ((snapshot.recoverableRivalSigilAvailable &&
             !knowledge.hasCheckedForRecoverableSigil(snapshot.currentCave)) ||
         searchCanNarrowBasilisk(snapshot, knowledge))) return 2;

    if (snapshot.hasHunterSigil && !snapshot.extractionCave) {
        if (action.type == ActionType::UseItem &&
            action.targetItem == ItemType::SurveyFragment) return 2;
        if (action.type == ActionType::Move &&
            (action.targetTunnel ||
             (action.targetCave && !knowledge.isKnownSafe(*action.targetCave))))
            return 3;
    }

    if (!survivalCritical && snapshot.recoverableRivalSigilAvailable &&
        action.type == ActionType::Move &&
        (action.targetTunnel ||
         (action.targetCave &&
          !knowledge.hasCheckedForRecoverableSigil(*action.targetCave)))) return 3;
    return 4;
}

bool hasReasonableSafeMove(
    const PlayerRoundSnapshot& snapshot, const AiKnowledgeState& knowledge) {
    return std::any_of(snapshot.availableActions.begin(), snapshot.availableActions.end(),
        [&](const AvailableAction& action) {
            return action.type == ActionType::Move &&
                !confirmedPitMove(snapshot, action, knowledge) &&
                !uncertainPitMove(snapshot, action, knowledge) &&
                !avoidedJackalRoute(snapshot, action, knowledge);
        });
}

double shootTacticalAdjustment(
    const AvailableAction& action,
    const PlayerRoundSnapshot& snapshot,
    const AiConfig& config,
    const AiKnowledgeState& knowledge) {
    if (action.type != ActionType::Shoot) return 0.0;
    if (snapshot.arrows <= 0) return -std::numeric_limits<double>::infinity();
    if (config.difficulty == AiDifficulty::Hard &&
        knowledge.isDisprovenBasiliskTarget(snapshot.currentCave, action))
        return -std::numeric_limits<double>::infinity();

    const std::size_t targets = shootTargetCount(snapshot);
    const std::size_t basiliskCandidates = basiliskCandidateCount(snapshot, knowledge);
    const bool unambiguous = targets == 1;
    const bool basiliskSupported = knowledge.basiliskWarningHere() &&
        basiliskCandidates == 1 && !confirmedPitTarget(snapshot, action, knowledge);
    const bool rivalSupported = knowledge.rivalWarningHere() && unambiguous;
    const bool jackalSupported = knowledge.jackalWarningHere() && unambiguous;
    const bool anyEvidence = knowledge.basiliskWarningHere() ||
        knowledge.basiliskDistantWarningHere() ||
        knowledge.rivalWarningHere() || knowledge.jackalWarningHere();
    const bool ambiguousBasilisk = knowledge.basiliskWarningHere() &&
        basiliskCandidates > 1;
    const bool safeMove = hasReasonableSafeMove(snapshot, knowledge);

    if (config.difficulty == AiDifficulty::Hard && !anyEvidence)
        return -std::numeric_limits<double>::infinity();

    double adjustment = 0.0;
    const int missingArrows = std::max(0, snapshot.maxArrows - snapshot.arrows);
    if (config.difficulty == AiDifficulty::Easy) {
        adjustment -= 4.0 * missingArrows;
        if (snapshot.arrows == 1) adjustment -= 10.0;
        if (!anyEvidence) adjustment -= 12.0;
    } else if (config.difficulty == AiDifficulty::Medium) {
        adjustment -= 10.0 * missingArrows;
        if (snapshot.arrows == 1) adjustment -= 38.0;
        if (!anyEvidence) adjustment -= 52.0;
    } else {
        adjustment -= 15.0 * missingArrows;
        if (snapshot.arrows == 2) adjustment -= 38.0;
        if (snapshot.arrows == 1) adjustment -= 92.0;
        if (ambiguousBasilisk) adjustment -= 145.0;
    }

    if (basiliskSupported) {
        adjustment += config.difficulty == AiDifficulty::Easy ? 55.0 :
            config.difficulty == AiDifficulty::Medium ? 105.0 : 190.0;
        if (config.behavior == AiBehavior::ObjectiveFocused) adjustment += 45.0;
    }
    if (rivalSupported) {
        adjustment += config.difficulty == AiDifficulty::Easy ? 25.0 : 45.0;
        if (config.behavior == AiBehavior::Aggressive) adjustment += 55.0;
    }
    if (jackalSupported) {
        const bool tacticallyNecessary = !safeMove || snapshot.health * 2 < snapshot.maxHealth;
        if (config.difficulty == AiDifficulty::Easy) adjustment += 18.0;
        else if (!tacticallyNecessary) adjustment -=
            config.difficulty == AiDifficulty::Medium ? 68.0 : 125.0;
        else adjustment += config.difficulty == AiDifficulty::Medium ? 15.0 : 32.0;
    }
    if (config.behavior == AiBehavior::Survivalist && !basiliskSupported)
        adjustment -= 20.0;
    return adjustment;
}

} // namespace

AiBehavior resolveBehavior(AiBehavior requested, AiSeed seed) {
    if (requested != AiBehavior::Random) return requested;
    constexpr AiBehavior choices[]{
        AiBehavior::Balanced, AiBehavior::Explorer, AiBehavior::Aggressive,
        AiBehavior::ObjectiveFocused, AiBehavior::Survivalist,
        AiBehavior::Opportunist};
    return choices[mix(seed ^ 0x4245484156494f52ULL) % std::size(choices)];
}

std::optional<AvailableAction> AiDecisionEngine::choose(
    const PlayerRoundSnapshot& snapshot,
    const AiConfig& config) const {
    return choose(snapshot, config, AiKnowledgeState{});
}

std::optional<AvailableAction> AiDecisionEngine::choose(
    const PlayerRoundSnapshot& snapshot,
    const AiConfig& config,
    const AiKnowledgeState& knowledge) const {
    const AiDecisionEvaluation evaluation = evaluate(snapshot, config, knowledge);
    if (evaluation.actions.empty()) return std::nullopt;
    return evaluation.actions[evaluation.chosenIndex].action;
}

AiDecisionEvaluation AiDecisionEngine::evaluate(
    const PlayerRoundSnapshot& snapshot,
    const AiConfig& config,
    const AiKnowledgeState& knowledge) const {
    AiDecisionEvaluation evaluation;
    evaluation.basiliskCandidates = basiliskCandidateCount(snapshot, knowledge);
    evaluation.basiliskAdjacentEvidence = knowledge.basiliskWarningHere();
    evaluation.basiliskDistantEvidence = knowledge.basiliskDistantWarningHere();
    evaluation.sigilRecoverable = snapshot.recoverableRivalSigilAvailable;
    if (snapshot.availableActions.empty()) return evaluation;
    const AiBehavior behavior = resolveBehavior(config.behavior, config.seed);
    const bool careless = carelessThisRound(config, snapshot.round);
    std::size_t bestIndex = 0;
    double best = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < snapshot.availableActions.size(); ++index) {
        const AvailableAction& action = snapshot.availableActions[index];
        if (!passesHardSafetyFilter(snapshot, action, config, knowledge)) continue;
        const double score = baseUtility(action, snapshot) +
            behaviorUtility(action, snapshot, behavior) +
            awarenessAdjustment(action, snapshot, config, knowledge, careless) +
            searchInformationAdjustment(action, snapshot, config, knowledge) +
            shootTacticalAdjustment(action, snapshot, config, knowledge) +
            noise(config, snapshot.round, index);
        evaluation.actions.push_back({action, score});
        if (score > best) {
            best = score;
            bestIndex = evaluation.actions.size() - 1;
        }
    }
    auto forced = immediateEscapeAction(snapshot, config);
    if (!forced) forced = hardBasiliskEngagementShot(snapshot, knowledge, config);
    if (!forced) forced = extractionWinAction(snapshot, config, knowledge);
    if (forced) {
        const auto selected = std::find_if(evaluation.actions.begin(),
            evaluation.actions.end(), [&](const AiActionUtility& scored) {
                return sameAction(scored.action, *forced);
            });
        if (selected != evaluation.actions.end()) {
            evaluation.chosenIndex = static_cast<std::size_t>(
                std::distance(evaluation.actions.begin(), selected));
            selected->utility = 10000.0;
        }
    }
    else if (config.difficulty == AiDifficulty::Hard) {
        std::optional<std::size_t> objectiveIndex;
        int objectivePriority = 4;
        double objectiveUtility = -std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < evaluation.actions.size(); ++index) {
            const auto& scored = evaluation.actions[index];
            const int priority = dualObjectivePriority(
                snapshot, knowledge, scored.action);
            if (priority < objectivePriority ||
                (priority == objectivePriority && scored.utility > objectiveUtility)) {
                objectivePriority = priority;
                objectiveUtility = scored.utility;
                objectiveIndex = index;
            }
        }
        if (objectiveIndex && objectivePriority < 4) {
            evaluation.chosenIndex = *objectiveIndex;
            evaluation.actions[*objectiveIndex].utility =
                objectivePriority == 1 ? 9000.0 :
                objectivePriority == 2 ? 8000.0 : 7000.0;
        } else evaluation.chosenIndex = bestIndex;
    } else evaluation.chosenIndex = bestIndex;
    return evaluation;
}

const char* difficultyName(AiDifficulty difficulty) noexcept {
    switch (difficulty) {
        case AiDifficulty::Easy: return "EASY";
        case AiDifficulty::Medium: return "MEDIUM";
        case AiDifficulty::Hard: return "HARD";
    }
    return "MEDIUM";
}

const char* behaviorName(AiBehavior behavior) noexcept {
    switch (behavior) {
        case AiBehavior::Balanced: return "BALANCED";
        case AiBehavior::Explorer: return "EXPLORER";
        case AiBehavior::Aggressive: return "AGGRESSIVE";
        case AiBehavior::ObjectiveFocused: return "OBJECTIVE-FOCUSED";
        case AiBehavior::Survivalist: return "SURVIVALIST";
        case AiBehavior::Opportunist: return "OPPORTUNIST";
        case AiBehavior::Random: return "RANDOM";
    }
    return "BALANCED";
}

} // namespace basilisk::client::ai
