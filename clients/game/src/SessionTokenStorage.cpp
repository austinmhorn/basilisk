#include "SessionTokenStorage.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace basilisk::game {
namespace {
constexpr const char* storageKey = "basilisk.auth.session.v1";

#if !defined(__EMSCRIPTEN__)
std::optional<std::string> tokenPath() {
    char* directory = SDL_GetPrefPath("Basilisk", "Basilisk");
    if (directory == nullptr) return std::nullopt;
    std::string path = std::string{directory} + "session-token";
    SDL_free(directory);
    return path;
}
#endif
}

std::optional<std::string> SessionTokenStorage::load() {
#if defined(__EMSCRIPTEN__)
    char* value = reinterpret_cast<char*>(EM_ASM_PTR({
        const value = localStorage.getItem(UTF8ToString($0));
        if (!value) return 0;
        const size = lengthBytesUTF8(value) + 1;
        const result = _malloc(size);
        stringToUTF8(value, result, size);
        return result;
    }, storageKey));
    if (value == nullptr) return std::nullopt;
    std::string token{value};
    std::free(value);
    return token.empty() ? std::nullopt : std::optional<std::string>{token};
#else
    const auto path = tokenPath();
    if (!path.has_value()) return std::nullopt;
    std::ifstream input(*path, std::ios::binary);
    std::string token;
    std::getline(input, token);
    return token.empty() ? std::nullopt : std::optional<std::string>{token};
#endif
}

bool SessionTokenStorage::save(const std::string& token, std::string& error) {
    if (token.empty()) { error = "Session token is empty."; return false; }
#if defined(__EMSCRIPTEN__)
    EM_ASM({ localStorage.setItem(UTF8ToString($0), UTF8ToString($1)); },
        storageKey, token.c_str());
#else
    const auto path = tokenPath();
    if (!path.has_value()) { error = SDL_GetError(); return false; }
    std::ofstream output(*path, std::ios::binary | std::ios::trunc);
    if (!output || !(output << token)) {
        error = "Unable to persist authentication session.";
        return false;
    }
#endif
    error.clear();
    return true;
}

bool SessionTokenStorage::clear(std::string& error) {
#if defined(__EMSCRIPTEN__)
    EM_ASM({ localStorage.removeItem(UTF8ToString($0)); }, storageKey);
#else
    const auto path = tokenPath();
    if (!path.has_value()) { error = SDL_GetError(); return false; }
    std::error_code removalError;
    (void)std::filesystem::remove(*path, removalError);
    if (removalError) {
        error = removalError.message();
        return false;
    }
#endif
    error.clear();
    return true;
}

} // namespace basilisk::game
