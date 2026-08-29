#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "basilisk/ClientSnapshot.hpp"

namespace basilisk::client::ai {

struct AiExitKey {
    CaveId source{};
    TunnelId tunnel{};
    auto operator<=>(const AiExitKey&) const = default;
};

// Persistent AI memory derived exclusively from successive player snapshots.
class AiKnowledgeState {
public:
    void observe(const PlayerRoundSnapshot& snapshot);
    void recordDecision(const AvailableAction& action);

    [[nodiscard]] std::optional<CaveId> previousCave() const noexcept;
    [[nodiscard]] bool isKnownSafe(CaveId cave) const;
    [[nodiscard]] bool isPitCandidate(CaveId cave) const;
    [[nodiscard]] bool isConfirmedPit(CaveId cave) const;
    [[nodiscard]] bool isConfirmedPitExit(AiExitKey exit) const;
    [[nodiscard]] bool isDisprovenBasiliskTarget(
        CaveId source, const AvailableAction& action) const;
    [[nodiscard]] bool isPreferredBasiliskTarget(
        CaveId source, const AvailableAction& action) const;
    [[nodiscard]] bool hasSearched(CaveId cave) const;
    [[nodiscard]] bool hasCheckedForRecoverableSigil(CaveId cave) const;
    [[nodiscard]] bool pitWarningHere() const noexcept;
    [[nodiscard]] bool basiliskWarningHere() const noexcept;
    [[nodiscard]] bool basiliskDistantWarningHere() const noexcept;
    [[nodiscard]] bool jackalWarningHere() const noexcept;
    [[nodiscard]] bool rivalWarningHere() const noexcept;
    [[nodiscard]] std::size_t basiliskWarningStreak() const noexcept;
    [[nodiscard]] bool temporarilyAvoids(CaveId a, CaveId b, RoundNumber round) const;
    [[nodiscard]] std::size_t unresolvedPitCandidateCount() const noexcept;
    [[nodiscard]] std::size_t repeatedSearchCount() const noexcept;
    [[nodiscard]] std::uint64_t materialRevision() const noexcept;

private:
    static std::uint64_t edgeKey(CaveId a, CaveId b);
    const DiscoveredCaveView* currentView(const PlayerRoundSnapshot& snapshot) const;
    void updatePitKnowledge(const PlayerRoundSnapshot& snapshot);
    void updateJackalKnowledge(const PlayerRoundSnapshot& snapshot);
    [[nodiscard]] std::uint64_t materialFingerprint(
        const PlayerRoundSnapshot& snapshot) const;

    std::optional<RoundNumber> lastRound_;
    std::optional<CaveId> currentCave_;
    std::optional<CaveId> previousCave_;
    std::set<CaveId> knownSafeCaves_;
    std::set<CaveId> pitCandidateCaves_;
    std::set<AiExitKey> pitCandidateExits_;
    std::set<CaveId> confirmedPitCaves_;
    std::set<AiExitKey> confirmedPitExits_;
    std::set<CaveId> disprovenBasiliskCaves_;
    std::set<AiExitKey> disprovenBasiliskExits_;
    std::set<CaveId> searchedCaves_;
    std::set<CaveId> sigilCheckedCaves_;
    std::map<std::uint64_t, int> jackalWarningTraversals_;
    std::map<std::uint64_t, RoundNumber> avoidedKnownEdgesUntil_;
    bool pitWarningHere_{false};
    bool basiliskWarningHere_{false};
    bool basiliskDistantWarningHere_{false};
    bool jackalWarningHere_{false};
    bool rivalWarningHere_{false};
    std::size_t basiliskWarningStreak_{0};
    std::size_t unresolvedPitCandidates_{0};
    std::uint64_t materialRevision_{0};
    std::optional<std::uint64_t> lastMaterialFingerprint_;
    std::optional<std::uint64_t> lastSearchRevision_;
    std::size_t repeatedSearches_{0};
    bool recoverableSigilAvailable_{false};
    std::optional<CaveId> pendingShotCave_;
    std::optional<AiExitKey> pendingShotExit_;
    std::optional<CaveId> preferredBasiliskCave_;
    std::optional<AiExitKey> preferredBasiliskExit_;
    std::optional<std::uint64_t> basiliskTargetContext_;
};

} // namespace basilisk::client::ai
