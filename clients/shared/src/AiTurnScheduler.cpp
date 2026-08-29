#include "basilisk/client/ai/AiTurnScheduler.hpp"

#include <utility>

namespace basilisk::client::ai {
namespace {
std::uint64_t mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}
std::uint64_t inRange(std::uint64_t value, std::uint64_t low, std::uint64_t high) {
    return low + mix(value) % (high - low + 1);
}
} // namespace

void AiTurnScheduler::scheduleAction(AvailableAction action, std::uint64_t nowMs,
    const AiConfig& config, RoundNumber round) {
    action_ = std::move(action);
    actionDeadline_ = nowMs + thinkDelayMs(config, round);
}
void AiTurnScheduler::scheduleClash(ClashId clash, std::string response,
    std::uint64_t nowMs, const AiConfig& config) {
    clash_ = ScheduledClashResponse{clash, std::move(response)};
    clashDeadline_ = nowMs + clashDelayMs(config, clash);
}
std::optional<AvailableAction> AiTurnScheduler::takeDueAction(std::uint64_t nowMs) {
    if (!action_ || !actionDeadline_ || nowMs < *actionDeadline_) return std::nullopt;
    auto result = std::move(action_); clearAction(); return result;
}
std::optional<ScheduledClashResponse> AiTurnScheduler::takeDueClash(std::uint64_t nowMs) {
    if (!clash_ || !clashDeadline_ || nowMs < *clashDeadline_) return std::nullopt;
    auto result = std::move(clash_); clearClash(); return result;
}
std::optional<std::uint64_t> AiTurnScheduler::actionDeadline() const noexcept { return actionDeadline_; }
std::optional<std::uint64_t> AiTurnScheduler::clashDeadline() const noexcept { return clashDeadline_; }
void AiTurnScheduler::clearAction() noexcept { action_.reset(); actionDeadline_.reset(); }
void AiTurnScheduler::clearClash() noexcept { clash_.reset(); clashDeadline_.reset(); }
void AiTurnScheduler::clear() noexcept { clearAction(); clearClash(); }
std::uint64_t AiTurnScheduler::thinkDelayMs(const AiConfig& config, RoundNumber round) {
    return inRange(config.seed ^ (static_cast<std::uint64_t>(config.player) << 16U) ^
        (static_cast<std::uint64_t>(round) << 32U) ^ 0x5448494e4bULL, 350, 900);
}
std::uint64_t AiTurnScheduler::clashDelayMs(const AiConfig& config, ClashId clash) {
    std::uint64_t low = 2500, high = 4000;
    if (config.difficulty == AiDifficulty::Easy) { low = 4500; high = 6500; }
    if (config.difficulty == AiDifficulty::Hard) { low = 1400; high = 2200; }
    return inRange(config.seed ^ (static_cast<std::uint64_t>(config.player) << 16U) ^
        clash ^ 0x434c415348ULL, low, high);
}

} // namespace basilisk::client::ai
