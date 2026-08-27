#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "basilisk/Action.hpp"
#include "basilisk/Event.hpp"
#include "basilisk/Types.hpp"

namespace basilisk {

using ClashId = std::uint64_t;

enum class ClashKind {
    MoveToSameCave,
    OppositeTraversal,
    MoveIntoSearch,
    MoveIntoUseItem,
    MoveIntoStationary
};

struct ActiveClash {
    ClashId id{};
    ClashKind kind{ClashKind::MoveToSameCave};
    std::vector<PlayerId> participants;
    std::string challengeWord;
    std::uint64_t remainingMs{};
};

enum class ClashSubmissionResult {
    Rejected,
    Incorrect,
    Resolved
};

struct PendingClashRound {
    ActiveClash clash;
    std::vector<PlayerAction> actions;
    std::vector<std::pair<PlayerId, CaveId>> moveDestinations;
    std::vector<GameEvent> completedEvents;
    PlayerId mover{};
    PlayerId stationary{};
    CaveId contestedCave{};
};

} // namespace basilisk
