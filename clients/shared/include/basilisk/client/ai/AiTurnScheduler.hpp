#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "basilisk/Clash.hpp"
#include "basilisk/ClientSnapshot.hpp"
#include "basilisk/client/ai/AiDecisionEngine.hpp"

namespace basilisk::client::ai {

struct ScheduledClashResponse {
    ClashId clash{};
    std::string response;
};

class AiTurnScheduler {
public:
    void scheduleAction(
        AvailableAction action,
        std::uint64_t nowMs,
        const AiConfig& config,
        RoundNumber round);
    void scheduleClash(
        ClashId clash,
        std::string response,
        std::uint64_t nowMs,
        const AiConfig& config);

    [[nodiscard]] std::optional<AvailableAction> takeDueAction(
        std::uint64_t nowMs);
    [[nodiscard]] std::optional<ScheduledClashResponse> takeDueClash(
        std::uint64_t nowMs);
    [[nodiscard]] std::optional<std::uint64_t> actionDeadline() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> clashDeadline() const noexcept;
    void clearAction() noexcept;
    void clearClash() noexcept;
    void clear() noexcept;

    [[nodiscard]] static std::uint64_t thinkDelayMs(
        const AiConfig& config,
        RoundNumber round);
    [[nodiscard]] static std::uint64_t clashDelayMs(
        const AiConfig& config,
        ClashId clash);

private:
    std::optional<AvailableAction> action_;
    std::optional<std::uint64_t> actionDeadline_;
    std::optional<ScheduledClashResponse> clash_;
    std::optional<std::uint64_t> clashDeadline_;
};

} // namespace basilisk::client::ai
