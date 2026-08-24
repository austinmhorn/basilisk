#include "TextRenderer.hpp"

#include <SDL3/SDL.h>

#include <cassert>
#include <iostream>
#include <string>

namespace {

using basilisk::game::FontWeight;
using basilisk::game::TextRenderer;

void requireDraw(
    TextRenderer& text,
    std::string_view value,
    float pointSize = 18.0F) {

    std::string error;
    const bool drawn = text.drawText(
        value,
        FontWeight::Regular,
        pointSize,
        SDL_Color{255, 255, 255, 255},
        SDL_FPoint{0.0F, 0.0F},
        error);
    if (!drawn) std::cerr << error << '\n';
    assert(drawn);
}

} // namespace

int main() {
    SDL_Surface* surface =
        SDL_CreateSurface(640, 360, SDL_PIXELFORMAT_RGBA32);
    assert(surface != nullptr);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer != nullptr);

    {
        TextRenderer text;
        std::string error;
        assert(text.initialize(renderer, BASILISK_TEST_FONT_DIR, error));

        requireDraw(text, "Stable label", 18.0F);
        requireDraw(text, "Stable label", 18.0F);
        auto stats = text.cacheStats();
        assert(stats.entries == 1);
        assert(stats.misses == 1);
        assert(stats.hits == 1);
        assert(stats.evictions == 0);

        // Quarter-point buckets prevent tiny render-scale jitter from creating
        // a distinct texture for every floating-point size.
        requireDraw(text, "Scaled label", 20.01F);
        requireDraw(text, "Scaled label", 20.10F);
        stats = text.cacheStats();
        assert(stats.entries == 2);
        assert(stats.misses == 2);
        assert(stats.hits == 2);

        constexpr std::size_t kDynamicLabels = 2048;
        for (std::size_t index = 0; index < kDynamicLabels; ++index) {
            requireDraw(text, "Round report " + std::to_string(index));
        }
        stats = text.cacheStats();
        assert(stats.entries == TextRenderer::kCacheCapacity);
        constexpr std::size_t kInitialEntries = 2;
        assert(
            stats.evictions ==
            kDynamicLabels + kInitialEntries - TextRenderer::kCacheCapacity);

        // The oldest dynamic entry was evicted and can be recreated safely
        // without allowing the cache to exceed its bound.
        const auto missesBefore = stats.misses;
        requireDraw(text, "Round report 0");
        stats = text.cacheStats();
        assert(stats.entries == TextRenderer::kCacheCapacity);
        assert(stats.misses == missesBefore + 1);
        assert(
            stats.evictions ==
            kDynamicLabels + kInitialEntries - TextRenderer::kCacheCapacity + 1);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    return 0;
}
