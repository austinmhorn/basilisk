#include "TextRenderer.hpp"
#include "ScreenShell.hpp"
#include "MainMenuRenderer.hpp"
#include "AuthScreenRenderer.hpp"
#include "UITheme.hpp"

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
    basilisk::game::AuthScreenGeometry authGeometry;
    authGeometry.back = {40.0, 600.0, 100.0, 38.0};
    assert(basilisk::game::hitTest(authGeometry.back, {80.0, 619.0}));
    assert(!basilisk::game::hitTest(authGeometry.back, {200.0, 619.0}));
    constexpr auto focusedFooter = basilisk::game::ui::footerButtonStyle(true);
    static_assert(focusedFooter.border.r == basilisk::game::ui::Theme::gold.r);
    static_assert(focusedFooter.border.g == basilisk::game::ui::Theme::gold.g);
    static_assert(focusedFooter.border.b == basilisk::game::ui::Theme::gold.b);

    basilisk::game::MainMenuGeometry menuGeometry;
    menuGeometry.buttons = {
        {basilisk::game::MainMenuAction::NextPage, {600.0, 600.0, 40.0, 40.0}},
        {basilisk::game::MainMenuAction::Back, {40.0, 600.0, 100.0, 40.0}},
    };
    assert(basilisk::game::hitTestMainMenu(menuGeometry, {80.0, 620.0}) ==
        basilisk::game::MainMenuAction::Back);
    assert(basilisk::game::hitTestMainMenu(menuGeometry, {620.0, 620.0}) ==
        basilisk::game::MainMenuAction::NextPage);

    const auto emptyQuiver = basilisk::game::hudArrowSectionLayout(0);
    assert(emptyQuiver.slotCount == 0);
    assert(emptyQuiver.contentWidth == 0.0F);
    assert(emptyQuiver.sectionWidth == 78.0F);

    const auto normalQuiver = basilisk::game::hudArrowSectionLayout(5);
    assert(normalQuiver.slotCount == 5);
    assert(normalQuiver.contentWidth == 62.0F);
    assert(normalQuiver.sectionWidth == 78.0F);

    const auto sandboxQuiver = basilisk::game::hudArrowSectionLayout(10);
    assert(sandboxQuiver.slotCount == 10);
    assert(sandboxQuiver.contentWidth == 127.0F);
    assert(sandboxQuiver.sectionWidth == 143.0F);
    const float finalSlotRight = 9.0F *
        (sandboxQuiver.slotWidth + sandboxQuiver.slotSpacing) +
        sandboxQuiver.slotWidth;
    assert(sandboxQuiver.sectionWidth - finalSlotRight == 16.0F);
    assert(basilisk::game::hudArrowSectionLayout(-1).slotCount == 0);

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
