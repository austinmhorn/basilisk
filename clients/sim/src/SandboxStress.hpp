#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>

namespace basilisk::sim {

struct SandboxStressConfig {
    std::uint64_t seed{1};
    std::size_t matchesPerPlayerCount{2};
    std::uint32_t maxRounds{1000};
};

struct SandboxStressCountResult {
    std::size_t players{};
    std::size_t matches{};
    std::size_t completed{};
    std::size_t draws{};
    std::size_t basiliskWins{};
    std::size_t extractionWins{};
    std::uint64_t rounds{};
    std::uint64_t clashes{};
    std::uint64_t generationRetries{};
};

struct SandboxStressResult {
    std::map<std::size_t, SandboxStressCountResult> byPlayerCount;
};

[[nodiscard]] SandboxStressResult runSandboxStress(
    const SandboxStressConfig& config,
    std::ostream* progress = nullptr);

} // namespace basilisk::sim
