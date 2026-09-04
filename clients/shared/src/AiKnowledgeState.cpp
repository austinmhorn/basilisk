#include "basilisk/client/ai/AiKnowledgeState.hpp"

#include <algorithm>

namespace basilisk::client::ai {
namespace {

void hashValue(std::uint64_t& hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
}

bool hasObservation(const PlayerRoundSnapshot& snapshot, ObservationType type) {
    return std::any_of(snapshot.observations.begin(), snapshot.observations.end(),
        [type](const PlayerObservation& observation) {
            return observation.type == type;
        });
}

std::uint64_t basiliskContextFingerprint(const PlayerRoundSnapshot& snapshot) {
    std::uint64_t hash = static_cast<std::uint64_t>(snapshot.currentCave) << 32U;
    hashValue(hash, hasObservation(snapshot, ObservationType::BasiliskNearby) ? 1U : 0U);
    hashValue(hash, hasObservation(snapshot, ObservationType::BasiliskNearbySubtle) ? 2U : 0U);
    const auto cave = std::find_if(snapshot.map.caves.begin(), snapshot.map.caves.end(),
        [&](const DiscoveredCaveView& candidate) {
            return candidate.cave == snapshot.currentCave;
        });
    if (cave == snapshot.map.caves.end()) return hash;
    for (const TunnelView& tunnel : cave->exits) {
        hashValue(hash, tunnel.id);
        hashValue(hash, tunnel.destination.value_or(0));
    }
    return hash;
}

bool reportsSearchResolution(ObservationType type) {
    switch (type) {
        case ObservationType::CaveAlreadySearched:
        case ObservationType::SearchEmpty:
        case ObservationType::ItemFound:
        case ObservationType::InventoryFull:
        case ObservationType::ArrowFound:
        case ObservationType::ExoticCallingCardFound:
        case ObservationType::OldHuntersMapFound:
        case ObservationType::PitInvestigationSucceeded:
        case ObservationType::PitInvestigationInconclusive:
            return true;
        default:
            return false;
    }
}

} // namespace

std::uint64_t AiKnowledgeState::edgeKey(CaveId a, CaveId b) {
    const auto [low, high] = std::minmax(a, b);
    return (static_cast<std::uint64_t>(low) << 32U) |
        static_cast<std::uint64_t>(high);
}

const DiscoveredCaveView* AiKnowledgeState::currentView(
    const PlayerRoundSnapshot& snapshot) const {
    const auto found = std::find_if(snapshot.map.caves.begin(), snapshot.map.caves.end(),
        [&](const DiscoveredCaveView& cave) {
            return cave.cave == snapshot.currentCave;
        });
    return found == snapshot.map.caves.end() ? nullptr : &*found;
}

void AiKnowledgeState::observe(const PlayerRoundSnapshot& snapshot) {
    if (lastRound_ == snapshot.round) return;
    const bool moved = currentCave_.has_value() && *currentCave_ != snapshot.currentCave;
    if (moved)
        previousCave_ = currentCave_;
    currentCave_ = snapshot.currentCave;
    lastRound_ = snapshot.round;

    // Physical survival in the current cave is proof of safety. Merely seeing
    // a cave in PlayerMapView is not: full-map rules and surveys can reveal a
    // cave without the hunter ever entering it.
    knownSafeCaves_.insert(snapshot.currentCave);
    for (const CaveId cave : snapshot.temporarilyRevealedPitCaves) {
        confirmedPitCaves_.insert(cave);
        knownSafeCaves_.erase(cave);
        pitCandidateCaves_.erase(cave);
    }

    pitWarningHere_ = hasObservation(snapshot, ObservationType::PitNearby);
    const bool basiliskWarning = hasObservation(snapshot, ObservationType::BasiliskNearby) ||
        hasObservation(snapshot, ObservationType::BasiliskNearbySubtle);
    basiliskWarningStreak_ = basiliskWarning ? basiliskWarningStreak_ + 1 : 0;
    basiliskWarningHere_ = basiliskWarning;
    basiliskDistantWarningHere_ =
        hasObservation(snapshot, ObservationType::RestlessBasiliskNoise);
    const std::uint64_t targetContext = basiliskContextFingerprint(snapshot);
    const bool contextChanged = moved ||
        (basiliskTargetContext_.has_value() && *basiliskTargetContext_ != targetContext) ||
        hasObservation(snapshot, ObservationType::BasiliskEvaded) ||
        !basiliskWarningHere_;
    if (contextChanged) {
        disprovenBasiliskCaves_.clear();
        disprovenBasiliskExits_.clear();
        preferredBasiliskCave_.reset();
        preferredBasiliskExit_.reset();
    }
    basiliskTargetContext_ = basiliskWarningHere_
        ? std::optional<std::uint64_t>{targetContext} : std::nullopt;
    if (basiliskWarningHere_ && hasObservation(snapshot, ObservationType::ArrowMissed)) {
        if (pendingShotCave_) disprovenBasiliskCaves_.insert(*pendingShotCave_);
        if (pendingShotExit_) disprovenBasiliskExits_.insert(*pendingShotExit_);
        preferredBasiliskCave_.reset();
        preferredBasiliskExit_.reset();
        if (const DiscoveredCaveView* view = currentView(snapshot); view != nullptr) {
            bool hasPlausible = false;
            bool allDisproven = true;
            for (const TunnelView& tunnel : view->exits) {
                const bool confirmedPit = tunnel.destination
                    ? confirmedPitCaves_.contains(*tunnel.destination)
                    : confirmedPitExits_.contains({snapshot.currentCave, tunnel.id});
                if (confirmedPit || tunnel.strongColdDraft) continue;
                hasPlausible = true;
                const bool disproven = tunnel.destination
                    ? disprovenBasiliskCaves_.contains(*tunnel.destination)
                    : disprovenBasiliskExits_.contains(
                        {snapshot.currentCave, tunnel.id});
                allDisproven = allDisproven && disproven;
            }
            if (hasPlausible && allDisproven) {
                disprovenBasiliskCaves_.clear();
                disprovenBasiliskExits_.clear();
            }
        }
    }
    pendingShotCave_.reset();
    pendingShotExit_.reset();
    jackalWarningHere_ = hasObservation(snapshot, ObservationType::JackalNearby) ||
        hasObservation(snapshot, ObservationType::JackalRobbedYou) ||
        hasObservation(snapshot, ObservationType::JackalScaredYou) ||
        hasObservation(snapshot, ObservationType::JackalKnockedOutYou);
    rivalWarningHere_ = hasObservation(snapshot, ObservationType::RivalNearby);
    const bool searchResolved = std::any_of(
        snapshot.observations.begin(), snapshot.observations.end(),
            [](const PlayerObservation& observation) {
                return reportsSearchResolution(observation.type);
            });
    if (searchResolved)
        searchedCaves_.insert(snapshot.currentCave);
    if (snapshot.recoverableRivalSigilAvailable && !recoverableSigilAvailable_)
        sigilCheckedCaves_.clear();
    if (snapshot.recoverableRivalSigilAvailable && searchResolved)
        sigilCheckedCaves_.insert(snapshot.currentCave);
    if (!snapshot.recoverableRivalSigilAvailable) sigilCheckedCaves_.clear();
    recoverableSigilAvailable_ = snapshot.recoverableRivalSigilAvailable;

    updatePitKnowledge(snapshot);
    updateJackalKnowledge(snapshot);

    const std::uint64_t exploration = explorationFingerprint(snapshot);
    if (!lastExplorationFingerprint_.has_value() ||
        *lastExplorationFingerprint_ != exploration) {
        recentExplorationCaves_.clear();
        explorationVisitCounts_.clear();
        explorationTraversalCounts_.clear();
        searchExplorationRevisions_.clear();
        ++explorationRevision_;
        turnsSinceExplorationProgress_ = 0;
        lastExplorationFingerprint_ = exploration;
    } else {
        ++turnsSinceExplorationProgress_;
    }
    if (moved && previousCave_)
        ++explorationTraversalCounts_[edgeKey(*previousCave_, snapshot.currentCave)];
    ++explorationVisitCounts_[snapshot.currentCave];
    if (recentExplorationCaves_.empty() ||
        recentExplorationCaves_.back() != snapshot.currentCave) {
        recentExplorationCaves_.push_back(snapshot.currentCave);
        if (recentExplorationCaves_.size() > 8)
            recentExplorationCaves_.erase(recentExplorationCaves_.begin());
    }

    const std::uint64_t fingerprint = materialFingerprint(snapshot);
    if (!lastMaterialFingerprint_.has_value() || *lastMaterialFingerprint_ != fingerprint) {
        ++materialRevision_;
        lastMaterialFingerprint_ = fingerprint;
    }
}

std::uint64_t AiKnowledgeState::explorationFingerprint(
    const PlayerRoundSnapshot& snapshot) const {
    std::uint64_t hash = 0x84222325cbf29ce4ULL;
    for (const DiscoveredCaveView& cave : snapshot.map.caves) {
        hashValue(hash, cave.cave);
        hashValue(hash, cave.surveyed);
        for (const TunnelView& tunnel : cave.exits) {
            hashValue(hash, tunnel.id);
            hashValue(hash, tunnel.destination.value_or(0));
            hashValue(hash, tunnel.strongColdDraft);
        }
    }
    for (const CaveId cave : knownSafeCaves_) hashValue(hash, cave);
    for (const CaveId cave : confirmedPitCaves_) hashValue(hash, cave + 0x20000U);
    for (const AiExitKey exit : confirmedPitExits_)
        hashValue(hash, ((static_cast<std::uint64_t>(exit.source) << 32U) | exit.tunnel) ^
            0x8000000000000000ULL);
    return hash;
}

void AiKnowledgeState::recordDecision(const AvailableAction& action) {
    if (action.type == ActionType::Shoot) {
        pendingShotCave_ = action.targetCave;
        pendingShotExit_ = action.targetTunnel
            ? std::optional<AiExitKey>{{currentCave_.value_or(0), *action.targetTunnel}}
            : std::nullopt;
        preferredBasiliskCave_ = pendingShotCave_;
        preferredBasiliskExit_ = pendingShotExit_;
    }
    if (action.type != ActionType::Search) {
        repeatedSearches_ = 0;
        lastSearchRevision_.reset();
        return;
    }
    if (currentCave_) searchExplorationRevisions_[*currentCave_] = explorationRevision_;
    if (lastSearchRevision_ == materialRevision_) ++repeatedSearches_;
    else {
        repeatedSearches_ = 1;
        lastSearchRevision_ = materialRevision_;
    }
}

std::uint64_t AiKnowledgeState::materialFingerprint(
    const PlayerRoundSnapshot& snapshot) const {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    hashValue(hash, snapshot.currentCave);
    hashValue(hash, static_cast<std::uint64_t>(snapshot.health));
    hashValue(hash, static_cast<std::uint64_t>(snapshot.arrows));
    hashValue(hash, snapshot.hasHunterSigil);
    hashValue(hash, snapshot.recoverableRivalSigilAvailable);
    hashValue(hash, snapshot.extractionCave.value_or(0));
    for (const ItemType item : snapshot.inventory.items)
        hashValue(hash, static_cast<std::uint64_t>(item) + 1U);
    for (const DiscoveredCaveView& cave : snapshot.map.caves) {
        hashValue(hash, cave.cave);
        hashValue(hash, cave.surveyed);
        for (const TunnelView& tunnel : cave.exits) {
            hashValue(hash, tunnel.id);
            hashValue(hash, tunnel.destination.value_or(0));
            hashValue(hash, tunnel.strongColdDraft);
        }
    }
    for (const CaveId cave : knownSafeCaves_) hashValue(hash, cave);
    for (const CaveId cave : sigilCheckedCaves_) hashValue(hash, cave + 0x30000U);
    for (const CaveId cave : pitCandidateCaves_) hashValue(hash, cave + 0x10000U);
    for (const CaveId cave : confirmedPitCaves_) hashValue(hash, cave + 0x20000U);
    for (const AiExitKey exit : pitCandidateExits_)
        hashValue(hash, (static_cast<std::uint64_t>(exit.source) << 32U) | exit.tunnel);
    for (const AiExitKey exit : confirmedPitExits_)
        hashValue(hash, ((static_cast<std::uint64_t>(exit.source) << 32U) | exit.tunnel) ^
            0x8000000000000000ULL);
    hashValue(hash, pitWarningHere_);
    hashValue(hash, basiliskWarningHere_);
    hashValue(hash, basiliskDistantWarningHere_);
    hashValue(hash, jackalWarningHere_);
    hashValue(hash, rivalWarningHere_);
    return hash;
}

void AiKnowledgeState::updatePitKnowledge(const PlayerRoundSnapshot& snapshot) {
    const DiscoveredCaveView* cave = currentView(snapshot);
    if (cave == nullptr) return;

    for (const PlayerObservation& observation : snapshot.observations) {
        if (observation.type != ObservationType::PitInvestigationSucceeded ||
            !observation.tunnel.has_value()) continue;
        // Investigation can resolve before a Jackal/Clash relocates the hunter.
        // Tunnel IDs are local to the reported source, not the final position.
        const CaveId source = observation.cave.value_or(snapshot.currentCave);
        const AiExitKey exit{source, *observation.tunnel};
        confirmedPitExits_.insert(exit);
        const auto sourceView = std::find_if(snapshot.map.caves.begin(), snapshot.map.caves.end(),
            [&](const DiscoveredCaveView& candidate) { return candidate.cave == source; });
        if (sourceView == snapshot.map.caves.end()) continue;
        const auto tunnel = std::find_if(sourceView->exits.begin(), sourceView->exits.end(),
            [&](const TunnelView& candidate) { return candidate.id == *observation.tunnel; });
        if (tunnel != sourceView->exits.end() && tunnel->destination.has_value())
            confirmedPitCaves_.insert(*tunnel->destination);
    }
    for (const TunnelView& tunnel : cave->exits) {
        if (tunnel.strongColdDraft) {
            confirmedPitExits_.insert({snapshot.currentCave, tunnel.id});
            if (tunnel.destination) confirmedPitCaves_.insert(*tunnel.destination);
        }
    }

    std::set<CaveId> localCaves;
    std::set<AiExitKey> localExits;
    bool hasConfirmedLocalPit = false;
    for (const TunnelView& tunnel : cave->exits) {
        if (tunnel.destination.has_value()) {
            if (confirmedPitCaves_.contains(*tunnel.destination)) {
                hasConfirmedLocalPit = true;
            } else if (!knownSafeCaves_.contains(*tunnel.destination)) {
                localCaves.insert(*tunnel.destination);
            }
        } else {
            const AiExitKey exit{snapshot.currentCave, tunnel.id};
            if (confirmedPitExits_.contains(exit)) hasConfirmedLocalPit = true;
            else localExits.insert(exit);
        }
    }

    if (pitWarningHere_) {
        if (hasConfirmedLocalPit) {
            unresolvedPitCandidates_ = 0;
            for (const CaveId caveId : localCaves) pitCandidateCaves_.erase(caveId);
            for (const AiExitKey exit : localExits) pitCandidateExits_.erase(exit);
        } else {
            pitCandidateCaves_.insert(localCaves.begin(), localCaves.end());
            pitCandidateExits_.insert(localExits.begin(), localExits.end());
            unresolvedPitCandidates_ = localCaves.size() + localExits.size();
            if (unresolvedPitCandidates_ == 1) {
                if (!localCaves.empty()) confirmedPitCaves_.insert(*localCaves.begin());
                else confirmedPitExits_.insert(*localExits.begin());
                unresolvedPitCandidates_ = 0;
            }
        }
    } else {
        for (const CaveId candidate : localCaves) {
            knownSafeCaves_.insert(candidate);
            pitCandidateCaves_.erase(candidate);
        }
        for (const AiExitKey exit : localExits) pitCandidateExits_.erase(exit);
        unresolvedPitCandidates_ = 0;
    }
    for (const CaveId safe : knownSafeCaves_) pitCandidateCaves_.erase(safe);
}

void AiKnowledgeState::updateJackalKnowledge(const PlayerRoundSnapshot& snapshot) {
    if (!jackalWarningHere_ || !previousCave_.has_value() ||
        *previousCave_ == snapshot.currentCave) return;
    const std::uint64_t traversed = edgeKey(*previousCave_, snapshot.currentCave);
    int& warnings = jackalWarningTraversals_[traversed];
    ++warnings;
    if (warnings >= 2) avoidedKnownEdgesUntil_[traversed] = snapshot.round + 3;
}

std::optional<CaveId> AiKnowledgeState::previousCave() const noexcept { return previousCave_; }
bool AiKnowledgeState::isKnownSafe(CaveId cave) const { return knownSafeCaves_.contains(cave); }
bool AiKnowledgeState::isPitCandidate(CaveId cave) const { return pitCandidateCaves_.contains(cave); }
bool AiKnowledgeState::isConfirmedPit(CaveId cave) const { return confirmedPitCaves_.contains(cave); }
bool AiKnowledgeState::isConfirmedPitExit(AiExitKey exit) const { return confirmedPitExits_.contains(exit); }
bool AiKnowledgeState::isPitCandidateExit(AiExitKey exit) const {
    return pitCandidateExits_.contains(exit);
}
bool AiKnowledgeState::isDisprovenBasiliskTarget(
    CaveId source, const AvailableAction& action) const {
    if (action.targetCave && disprovenBasiliskCaves_.contains(*action.targetCave))
        return true;
    return action.targetTunnel &&
        disprovenBasiliskExits_.contains({source, *action.targetTunnel});
}
bool AiKnowledgeState::isPreferredBasiliskTarget(
    CaveId source, const AvailableAction& action) const {
    if (action.targetCave && preferredBasiliskCave_ == action.targetCave) return true;
    return action.targetTunnel && preferredBasiliskExit_ ==
        std::optional<AiExitKey>{{source, *action.targetTunnel}};
}
bool AiKnowledgeState::hasSearched(CaveId cave) const { return searchedCaves_.contains(cave); }
bool AiKnowledgeState::hasCheckedForRecoverableSigil(CaveId cave) const {
    return sigilCheckedCaves_.contains(cave);
}
bool AiKnowledgeState::pitWarningHere() const noexcept { return pitWarningHere_; }
bool AiKnowledgeState::basiliskWarningHere() const noexcept { return basiliskWarningHere_; }
bool AiKnowledgeState::basiliskDistantWarningHere() const noexcept {
    return basiliskDistantWarningHere_;
}
bool AiKnowledgeState::jackalWarningHere() const noexcept { return jackalWarningHere_; }
bool AiKnowledgeState::rivalWarningHere() const noexcept { return rivalWarningHere_; }
std::size_t AiKnowledgeState::basiliskWarningStreak() const noexcept {
    return basiliskWarningStreak_;
}
std::size_t AiKnowledgeState::unresolvedPitCandidateCount() const noexcept { return unresolvedPitCandidates_; }
std::size_t AiKnowledgeState::repeatedSearchCount() const noexcept {
    return lastSearchRevision_ == materialRevision_ ? repeatedSearches_ : 0;
}
std::uint64_t AiKnowledgeState::materialRevision() const noexcept { return materialRevision_; }

std::size_t AiKnowledgeState::explorationCyclePenalty(CaveId destination) const {
    if (recentExplorationCaves_.size() < 2) return 0;
    std::size_t penalty = 0;
    for (std::size_t index = 0; index + 1 < recentExplorationCaves_.size(); ++index) {
        if (recentExplorationCaves_[index] != destination) continue;
        // Repeated visits make a first hop less attractive; an immediate
        // reversal receives the strongest bounded penalty. The route remains
        // legal when it is the only route to a frontier.
        ++penalty;
        if (index + 2 == recentExplorationCaves_.size())
            penalty += recentExplorationCaves_.size();
    }
    return penalty;
}

std::size_t AiKnowledgeState::turnsSinceExplorationProgress() const noexcept {
    return turnsSinceExplorationProgress_;
}

std::size_t AiKnowledgeState::explorationVisitCount(CaveId cave) const {
    const auto found = explorationVisitCounts_.find(cave);
    return found == explorationVisitCounts_.end() ? 0 : found->second;
}

std::size_t AiKnowledgeState::explorationTraversalCount(CaveId a, CaveId b) const {
    const auto found = explorationTraversalCounts_.find(edgeKey(a, b));
    return found == explorationTraversalCounts_.end() ? 0 : found->second;
}

bool AiKnowledgeState::searchedWithoutExplorationProgress(CaveId cave) const {
    const auto found = searchExplorationRevisions_.find(cave);
    return found != searchExplorationRevisions_.end() &&
        found->second == explorationRevision_;
}

bool AiKnowledgeState::temporarilyAvoids(CaveId a, CaveId b, RoundNumber round) const {
    const auto found = avoidedKnownEdgesUntil_.find(edgeKey(a, b));
    return found != avoidedKnownEdgesUntil_.end() && round <= found->second;
}

} // namespace basilisk::client::ai
