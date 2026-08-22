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
        {PlayerId{1}, "Mara Voss", client::CallingCardId{"ember-field"},
         client::EmblemId{"wayfinder"}},
        {PlayerId{2}, "Elias Thorn", client::CallingCardId{"blue-ward"},
         client::EmblemId{"ward"}},
    };
}

bool parseUnsigned(std::string_view text, std::uint64_t& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
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
    std::optional<std::string> p1PublicHandle;
    std::optional<std::string> p2PublicHandle;
    std::optional<std::string> p1DisplayName;
    std::optional<std::string> p2DisplayName;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument != "--bind" && argument != "--port" &&
            argument != "--p1-token" &&
            argument != "--p2-token" && argument != "--map-seed" &&
            argument != "--trophy-db" && argument != "--match-id" &&
            argument != "--auth-db" &&
            argument != "--p1-account" && argument != "--p2-account" &&
            argument != "--p1-handle" && argument != "--p2-handle" &&
            argument != "--p1-display-name" &&
            argument != "--p2-display-name") {
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
        else if (argument == "--p1-handle") p1PublicHandle = argv[index];
        else if (argument == "--p2-handle") p2PublicHandle = argv[index];
        else if (argument == "--p1-display-name") p1DisplayName = argv[index];
        else if (argument == "--p2-display-name") p2DisplayName = argv[index];
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
    const bool trophiesRequested = trophyDatabase.has_value() ||
        trophyMatch.has_value() || p1PublicHandle.has_value() ||
        p2PublicHandle.has_value() || p1DisplayName.has_value() ||
        p2DisplayName.has_value();
    if ((p1Account.has_value() || p2Account.has_value()) &&
        !trophiesRequested && !authenticationDatabase.has_value()) {
        std::fprintf(stderr,
            "Player account bindings require --auth-db or trophy persistence.\n");
        return 2;
    }
    if (trophiesRequested) {
        if (!trophyMatch.has_value() || !p1Account.has_value() ||
            !p2Account.has_value()) {
            std::fprintf(stderr,
                "Trophy scoring requires --match-id, --p1-account, and "
                "--p2-account.\n");
            return 2;
        }
        const bool anyPublicProfile = p1PublicHandle.has_value() ||
            p2PublicHandle.has_value() || p1DisplayName.has_value() ||
            p2DisplayName.has_value();
        const bool completePublicProfiles = p1PublicHandle.has_value() &&
            p2PublicHandle.has_value() && p1DisplayName.has_value() &&
            p2DisplayName.has_value();
        if (trophyDatabase.has_value() != completePublicProfiles ||
            anyPublicProfile != completePublicProfiles) {
            std::fprintf(stderr,
                "SQLite trophy persistence requires --trophy-db, --p1-handle, "
                "--p2-handle, --p1-display-name, and --p2-display-name together.\n");
            return 2;
        }
        config.trophies = basilisk::game::server::LocalServerTrophyConfig{
            basilisk::game::server::TrophyMatchId{std::move(*trophyMatch)},
            basilisk::game::server::AccountIdentity{*p1Account},
            basilisk::game::server::AccountIdentity{*p2Account},
            trophyDatabase.has_value() ? std::move(*trophyDatabase) : std::string{},
        };
        if (completePublicProfiles) {
            config.trophies->p1PublicProfile =
                basilisk::game::server::PublicAccountProfile{
                    basilisk::game::PublicProfileHandle{
                        std::move(*p1PublicHandle)},
                    std::move(*p1DisplayName),
                };
            config.trophies->p2PublicProfile =
                basilisk::game::server::PublicAccountProfile{
                    basilisk::game::PublicProfileHandle{
                        std::move(*p2PublicHandle)},
                    std::move(*p2DisplayName),
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
