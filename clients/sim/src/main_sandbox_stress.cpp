#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "SandboxStress.hpp"

namespace {

std::uint64_t number(std::string_view value, const char* option) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(std::string{value}, &consumed);
    if (consumed != value.size())
        throw std::invalid_argument(std::string{"Invalid value for "} + option);
    return parsed;
}

} // namespace

int main(int argc, char** argv) {
    basilisk::sim::SandboxStressConfig config;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option{argv[index]};
        if (option == "--matches-per-count" && index + 1 < argc) {
            config.matchesPerPlayerCount = static_cast<std::size_t>(
                number(argv[++index], "--matches-per-count"));
        } else if (option == "--seed" && index + 1 < argc) {
            config.seed = number(argv[++index], "--seed");
        } else if (option == "--max-rounds" && index + 1 < argc) {
            config.maxRounds = static_cast<std::uint32_t>(
                number(argv[++index], "--max-rounds"));
        } else if (option == "--help") {
            std::cout << "Usage: BasiliskSandboxStress [--matches-per-count N]"
                         " [--seed N] [--max-rounds N]\n";
            return EXIT_SUCCESS;
        } else {
            std::cerr << "Unknown or incomplete option: " << option << '\n';
            return EXIT_FAILURE;
        }
    }
    try {
        const auto started = std::chrono::steady_clock::now();
        const auto result = basilisk::sim::runSandboxStress(config, &std::cerr);
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        std::size_t total = 0;
        for (const auto& [players, count] : result.byPlayerCount) {
            total += count.matches;
            std::cout << players << " players: " << count.completed << "/"
                << count.matches << " completed, " << count.draws << " draws, "
                << count.basiliskWins << " Basilisk wins, "
                << count.extractionWins << " extraction wins, "
                << count.rounds << " rounds, " << count.clashes << " clashes, "
                << count.generationRetries << " generation retries\n";
        }
        std::cout << "total: " << total << " matches in " << seconds
            << "s (" << (seconds == 0.0 ? 0.0 : total / seconds) << " matches/s)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
