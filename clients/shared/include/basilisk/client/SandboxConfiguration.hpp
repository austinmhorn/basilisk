#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "basilisk/Rules.hpp"
#include "basilisk/Types.hpp"
#include "basilisk/client/ai/AiDecisionEngine.hpp"
#include "basilisk/world/MapGenerator.hpp"

namespace basilisk::client {

inline constexpr std::array<std::size_t, 4> sandboxCaveCounts{30, 40, 50, 60};
inline constexpr std::array<std::uint32_t, 4> sandboxArrowSpawnIntervals{5, 3, 8, 0};
inline constexpr int sandboxMaximumArrowCapacity = 10;

struct SandboxSessionConfig {
    std::size_t hunterCount{2};
    std::size_t humanPlayerCount{1};
    std::size_t caveCount{30};
    std::size_t jackalCount{2};
    std::uint32_t arrowSpawnIntervalRounds{5};
    int startingArrows{3};
    int maxArrows{5};
    ai::AiDifficulty aiDifficulty{ai::AiDifficulty::Medium};
    ai::AiBehavior aiBehavior{ai::AiBehavior::Balanced};
    MapSeed mapSeed{20260816};
    MatchSeed matchSeed{424242};
    ai::AiSeed aiSeed{77};
};

enum class SandboxLobbySlotKind { Host, EmptyHuman, Ai };

struct SandboxLobbySlot {
    std::size_t slot{};
    PlayerId player{};
    SandboxLobbySlotKind kind{SandboxLobbySlotKind::Ai};
};

[[nodiscard]] inline std::size_t sandboxAiCount(
    const SandboxSessionConfig& config) noexcept {
    return config.humanPlayerCount <= config.hunterCount
        ? config.hunterCount - config.humanPlayerCount : 0;
}

[[nodiscard]] inline std::vector<SandboxLobbySlot> sandboxLobbySlots(
    const SandboxSessionConfig& config) {
    std::vector<SandboxLobbySlot> result;
    result.reserve(config.hunterCount);
    for (std::size_t index = 0; index < config.hunterCount; ++index) {
        result.push_back({index + 1, static_cast<PlayerId>(index + 1),
            index == 0 ? SandboxLobbySlotKind::Host :
            (index < config.humanPlayerCount ? SandboxLobbySlotKind::EmptyHuman :
                SandboxLobbySlotKind::Ai)});
    }
    return result;
}

[[nodiscard]] inline std::size_t minimumSandboxCaves(std::size_t hunterCount) {
    return std::max<std::size_t>(30, hunterCount * 10);
}

[[nodiscard]] inline std::size_t defaultSandboxCaves(std::size_t hunterCount) {
    return hunterCount <= 2 ? 30 : 60;
}

[[nodiscard]] inline std::size_t defaultSandboxJackals(std::size_t caveCount) {
    constexpr std::size_t cavesPerJackal = 15;
    return std::max<std::size_t>(1,
        (caveCount + cavesPerJackal - 1) / cavesPerJackal);
}

[[nodiscard]] inline std::size_t maximumSandboxJackals(std::size_t caveCount) {
    return caveCount / 10;
}

[[nodiscard]] inline SandboxSessionConfig defaultSandboxSessionConfig(
    std::size_t hunterCount = 2) {
    SandboxSessionConfig config;
    config.hunterCount = hunterCount;
    config.caveCount = defaultSandboxCaves(hunterCount);
    config.jackalCount = defaultSandboxJackals(config.caveCount);
    return config;
}

[[nodiscard]] inline std::optional<std::string_view> validateSandboxSessionConfig(
    const SandboxSessionConfig& config) {
    if (config.hunterCount < 2 || config.hunterCount > 6)
        return "Sandbox requires 2-6 hunters.";
    if (config.humanPlayerCount < 1 ||
        config.humanPlayerCount > config.hunterCount)
        return "Human players must be between 1 and the hunter count.";
    if (std::find(sandboxCaveCounts.begin(), sandboxCaveCounts.end(),
            config.caveCount) == sandboxCaveCounts.end())
        return "Cave count must be 30, 40, 50, or 60.";
    if (config.caveCount < minimumSandboxCaves(config.hunterCount))
        return "More caves are required for fair hunter spacing.";
    if (config.jackalCount > maximumSandboxJackals(config.caveCount))
        return "Too many Jackals for safe placement.";
    if (std::find(sandboxArrowSpawnIntervals.begin(),
            sandboxArrowSpawnIntervals.end(), config.arrowSpawnIntervalRounds) ==
        sandboxArrowSpawnIntervals.end())
        return "Unsupported arrow spawn frequency.";
    if (config.maxArrows < 0 || config.maxArrows > sandboxMaximumArrowCapacity)
        return "Max arrows must be between 0 and 10.";
    if (config.startingArrows < 0 || config.startingArrows > config.maxArrows)
        return "Starting arrows cannot exceed max arrows.";
    return std::nullopt;
}

[[nodiscard]] inline std::optional<std::string_view>
validateOnlineSandboxSessionConfig(const SandboxSessionConfig& config) {
    if (const auto error = validateSandboxSessionConfig(config)) return error;
    if (config.humanPlayerCount < 2)
        return "Online Sandbox requires at least 2 human players.";
    return std::nullopt;
}

[[nodiscard]] inline ProceduralMapConfig sandboxMapConfig(
    const SandboxSessionConfig& session) {
    ProceduralMapConfig config;
    config.caveCount = session.caveCount;
    config.extraConnections = 8 + (session.caveCount - 30) * 8 / 30;
    config.minDiameter = session.caveCount == 30 ? 6 : 8;
    config.maxDiameter = 30;
    config.maxGenerationAttempts = 512;
    config.jackalCount = session.jackalCount;
    return config;
}

[[nodiscard]] inline ProceduralMapConfig sandboxMapConfig(
    std::size_t hunterCount) {
    return sandboxMapConfig(defaultSandboxSessionConfig(hunterCount));
}

[[nodiscard]] inline Rules sandboxRules(const SandboxSessionConfig& session) {
    Rules rules;
    rules.startingArrows = session.startingArrows;
    rules.maxArrows = session.maxArrows;
    rules.looseArrowSpawnIntervalRounds = session.arrowSpawnIntervalRounds;
    return rules;
}

} // namespace basilisk::client
