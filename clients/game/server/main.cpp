#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "LocalWebSocketMatchServer.hpp"
#include "NetworkEndpointConfig.hpp"

namespace {

std::atomic_bool running{true};

void stopServer(int) { running = false; }

std::vector<basilisk::client::PublicPlayerProfile> profiles() {
    using namespace basilisk;
    return {
        {PlayerId{1}, "Mara Voss", client::CallingCardId{"arrow-right-black"},
         client::EmblemId{"rounded-square-black"}},
        {PlayerId{2}, "Elias Thorn", client::CallingCardId{"honeycomb-flag-white"},
         client::EmblemId{"circle-green"}},
    };
}

bool parseUnsigned(std::string_view text, std::uint64_t& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

std::string canaryDifficulties(std::uint8_t mask) {
    using namespace basilisk::client::ai;
    std::string result;
    const auto append = [&](std::string_view name) {
        if (!result.empty()) result += ',';
        result += name;
    };
    if ((mask & kCanaryEasy) != 0) append("easy");
    if ((mask & kCanaryMedium) != 0) append("medium");
    if ((mask & kCanaryHard) != 0) append("hard");
    return result;
}

} // namespace

int main(int argc, char** argv) {
    basilisk::game::server::LocalWebSocketServerConfig config;
    basilisk::game::NetworkEndpointConfig endpointConfig;
    config.p1Token = "basilisk-p1-local";
    config.p2Token = "basilisk-p2-local";
    config.profiles = profiles();
    std::optional<std::string> trophyDatabase;
    std::optional<std::string> authenticationDatabase;
    std::optional<std::string> trophyMatch;
    std::optional<std::string> p1Account;
    std::optional<std::string> p2Account;
    std::optional<std::string> p1Username;
    std::optional<std::string> p2Username;
    std::optional<std::string> aiShadowOutput;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument != "--bind" && argument != "--port" &&
            argument != "--p1-token" &&
            argument != "--p2-token" && argument != "--map-seed" &&
            argument != "--trophy-db" && argument != "--match-id" &&
            argument != "--auth-db" &&
            argument != "--p1-account" && argument != "--p2-account" &&
            argument != "--p1-username" && argument != "--p2-username" &&
            argument != "--ai-policy" && argument != "--ai-model" &&
            argument != "--ai-shadow-output" &&
            argument != "--ai-canary-percent" &&
            argument != "--ai-canary-difficulties") {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[index]);
            return 2;
        }
        if (++index >= argc) {
            std::fprintf(stderr, "%.*s requires a value\n",
                static_cast<int>(argument.size()), argument.data());
            return 2;
        }
        if (argument == "--bind" || argument == "--port") {
            std::string endpointError;
            if (!basilisk::game::applyNetworkEndpointOption(
                    argument, argv[index], endpointConfig, endpointError)) {
                std::fprintf(stderr, "%s\n", endpointError.c_str());
                return 2;
            }
            config.bindAddress = endpointConfig.bindAddress;
            config.port = endpointConfig.serverPort;
        }
        else if (argument == "--p1-token") config.p1Token = argv[index];
        else if (argument == "--p2-token") config.p2Token = argv[index];
        else if (argument == "--trophy-db") trophyDatabase = argv[index];
        else if (argument == "--auth-db") authenticationDatabase = argv[index];
        else if (argument == "--match-id") trophyMatch = argv[index];
        else if (argument == "--p1-account") p1Account = argv[index];
        else if (argument == "--p2-account") p2Account = argv[index];
        else if (argument == "--p1-username") p1Username = argv[index];
        else if (argument == "--p2-username") p2Username = argv[index];
        else if (argument == "--ai-policy") {
            const auto mode = basilisk::client::ai::parseRuntimeAiPolicyMode(argv[index]);
            if (!mode) {
                std::fprintf(stderr,
                    "--ai-policy must be heuristic, learned, shadow, or canary\n");
                return 2;
            }
            config.aiPolicy.mode = *mode;
        }
        else if (argument == "--ai-model") config.aiPolicy.modelPath = argv[index];
        else if (argument == "--ai-shadow-output") aiShadowOutput = argv[index];
        else if (argument == "--ai-canary-percent") {
            std::uint64_t value = 0;
            if (!parseUnsigned(argv[index], value) || value > 100) {
                std::fprintf(stderr,
                    "--ai-canary-percent must be between 0 and 100\n");
                return 2;
            }
            config.aiPolicy.canaryPercent = static_cast<std::uint8_t>(value);
        }
        else if (argument == "--ai-canary-difficulties") {
            const auto difficulties =
                basilisk::client::ai::parseRuntimeAiCanaryDifficulties(argv[index]);
            if (!difficulties) {
                std::fprintf(stderr,
                    "--ai-canary-difficulties must be a comma-separated subset of easy,medium,hard\n");
                return 2;
            }
            config.aiPolicy.canaryDifficulties = *difficulties;
        }
        else {
            std::uint64_t value = 0;
            if (!parseUnsigned(argv[index], value)) {
                std::fprintf(stderr, "Invalid value for %.*s: %s\n",
                    static_cast<int>(argument.size()), argument.data(), argv[index]);
                return 2;
            }
            config.mapSeed = static_cast<basilisk::MapSeed>(value);
        }
    }
    if (aiShadowOutput.has_value() &&
        (config.aiPolicy.mode == basilisk::client::ai::RuntimeAiPolicyMode::Shadow ||
         config.aiPolicy.mode == basilisk::client::ai::RuntimeAiPolicyMode::Canary)) {
        try {
            config.aiPolicy.telemetry =
                std::make_shared<basilisk::client::ai::AiShadowTelemetry>(
                    *aiShadowOutput, true);
        } catch (const std::exception& error) {
            std::fprintf(stderr,
                "AI telemetry unavailable (%s); falling back to heuristic policy.\n",
                error.what());
            config.aiPolicy.mode =
                basilisk::client::ai::RuntimeAiPolicyMode::Heuristic;
            config.aiPolicy.canaryPercent = 0;
        }
    }

    const bool modelRequired =
        config.aiPolicy.mode == basilisk::client::ai::RuntimeAiPolicyMode::Learned ||
        config.aiPolicy.mode == basilisk::client::ai::RuntimeAiPolicyMode::Shadow ||
        (config.aiPolicy.mode == basilisk::client::ai::RuntimeAiPolicyMode::Canary &&
         config.aiPolicy.canaryPercent > 0);
    std::string modelStatus{"not-requested"};
    if (modelRequired) {
        const basilisk::client::ai::LearnedPolicy modelProbe{config.aiPolicy.modelPath};
        modelStatus = modelProbe.modelLoaded() ? "loaded" : "heuristic-fallback";
        if (!modelProbe.modelLoaded())
            std::fprintf(stderr, "AI model unavailable (%s); learned decisions will use heuristic fallback.\n",
                modelProbe.loadError().c_str());
    }
    std::printf(
        "AI policy mode=%s model=%s model_status=%s canary_percent=%u "
        "canary_difficulties=%s telemetry=%s\n",
        basilisk::client::ai::runtimeAiPolicyModeName(config.aiPolicy.mode),
        config.aiPolicy.modelPath.empty() ? "(none)" : config.aiPolicy.modelPath.c_str(),
        modelStatus.c_str(), static_cast<unsigned>(config.aiPolicy.canaryPercent),
        canaryDifficulties(config.aiPolicy.canaryDifficulties).c_str(),
        config.aiPolicy.telemetry == nullptr || !aiShadowOutput.has_value()
            ? "disabled" : aiShadowOutput->c_str());
    std::fflush(stdout);
    const bool fixedTrophyScoringRequested = trophyMatch.has_value() ||
        p1Username.has_value() || p2Username.has_value();
    if ((p1Account.has_value() || p2Account.has_value()) &&
        !fixedTrophyScoringRequested && !authenticationDatabase.has_value()) {
        std::fprintf(stderr,
            "Player account bindings require --auth-db or trophy persistence.\n");
        return 2;
    }
    if (trophyDatabase.has_value())
        config.trophyDatabasePath = *trophyDatabase;
    if (fixedTrophyScoringRequested) {
        if (!trophyMatch.has_value() || !p1Account.has_value() ||
            !p2Account.has_value()) {
            std::fprintf(stderr,
                "Trophy scoring requires --match-id, --p1-account, and "
                "--p2-account.\n");
            return 2;
        }
        const bool anyPublicProfile = p1Username.has_value() ||
            p2Username.has_value();
        const bool completePublicProfiles = p1Username.has_value() &&
            p2Username.has_value();
        if (trophyDatabase.has_value() != completePublicProfiles ||
            anyPublicProfile != completePublicProfiles) {
            std::fprintf(stderr,
                "SQLite trophy persistence requires --trophy-db, --p1-username, "
                "and --p2-username together.\n");
            return 2;
        }
        config.trophies = basilisk::game::server::LocalServerTrophyConfig{
            basilisk::game::server::TrophyMatchId{std::move(*trophyMatch)},
            basilisk::game::server::AccountIdentity{*p1Account},
            basilisk::game::server::AccountIdentity{*p2Account},
            trophyDatabase.has_value() ? *trophyDatabase : std::string{},
        };
        if (completePublicProfiles) {
            config.trophies->p1PublicProfile =
                basilisk::game::server::PublicAccountProfile{
                    basilisk::game::Username{std::move(*p1Username)},
                };
            config.trophies->p2PublicProfile =
                basilisk::game::server::PublicAccountProfile{
                    basilisk::game::Username{std::move(*p2Username)},
                };
        }
    }

    if (authenticationDatabase.has_value()) {
        if (p1Account.has_value() != p2Account.has_value()) {
            std::fprintf(stderr,
                "Optional auth gameplay bindings require both --p1-account "
                "and --p2-account.\n");
            return 2;
        }
        std::string authError;
        config.authentication =
            basilisk::game::server::SQLiteAccountAuth::open(
                *authenticationDatabase, authError);
        if (config.authentication == nullptr) {
            std::fprintf(stderr, "Unable to open account database: %s\n",
                authError.c_str());
            return 1;
        }
        if (p1Account.has_value()) {
            config.p1AuthenticatedAccount =
                basilisk::game::server::AccountIdentity{*p1Account};
            config.p2AuthenticatedAccount =
                basilisk::game::server::AccountIdentity{*p2Account};
        }
    }

    std::string error;
    auto server = basilisk::game::server::LocalWebSocketMatchServer::start(
        std::move(config), error);
    if (server == nullptr) {
        std::fprintf(stderr, "Unable to start Basilisk server: %s\n", error.c_str());
        return 1;
    }
    std::signal(SIGINT, stopServer);
    std::signal(SIGTERM, stopServer);
    std::printf("Basilisk server listening on ws://%s:%u\n",
        endpointConfig.bindAddress.c_str(), server->port());
    std::fflush(stdout);
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server->advanceTime(100);
    }
    server->stop();
    return 0;
}
