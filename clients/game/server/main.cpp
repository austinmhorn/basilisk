#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "LocalWebSocketMatchServer.hpp"

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
    config.p1Token = "basilisk-p1-local";
    config.p2Token = "basilisk-p2-local";
    config.profiles = profiles();
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument != "--port" && argument != "--p1-token" &&
            argument != "--p2-token" && argument != "--map-seed") {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[index]);
            return 2;
        }
        if (++index >= argc) {
            std::fprintf(stderr, "%.*s requires a value\n",
                static_cast<int>(argument.size()), argument.data());
            return 2;
        }
        if (argument == "--p1-token") config.p1Token = argv[index];
        else if (argument == "--p2-token") config.p2Token = argv[index];
        else {
            std::uint64_t value = 0;
            if (!parseUnsigned(argv[index], value) ||
                (argument == "--port" && (value == 0 || value > 65535))) {
                std::fprintf(stderr, "Invalid value for %.*s: %s\n",
                    static_cast<int>(argument.size()), argument.data(), argv[index]);
                return 2;
            }
            if (argument == "--port") config.port = static_cast<std::uint16_t>(value);
            else config.mapSeed = static_cast<basilisk::MapSeed>(value);
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
    std::printf("Basilisk server listening on ws://127.0.0.1:%u\n", server->port());
    std::fflush(stdout);
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server->advanceTime(100);
    }
    server->stop();
    return 0;
}
