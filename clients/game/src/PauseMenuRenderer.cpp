#include "PauseMenuRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "UITheme.hpp"

namespace basilisk::game {
namespace {

bool contains(PresentationRect bounds, PresentationPoint point) noexcept {
    return point.x >= bounds.x && point.x <= bounds.x + bounds.width &&
        point.y >= bounds.y && point.y <= bounds.y + bounds.height;
}

void filledPill(SDL_Renderer* renderer, PresentationRect bounds, SDL_Color color) {
    const double radius = bounds.height * 0.5;
    const double centerY = bounds.y + radius;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int row = static_cast<int>(std::floor(bounds.y));
         row < static_cast<int>(std::ceil(bounds.y + bounds.height)); ++row) {
        const double sampleY = static_cast<double>(row) + 0.5;
        if (sampleY < bounds.y || sampleY >= bounds.y + bounds.height) continue;
        const double vertical = std::abs(sampleY - centerY);
        const double horizontal = std::sqrt(std::max(
            0.0, radius * radius - vertical * vertical));
        const double inset = radius - horizontal;
        SDL_FRect span{
            static_cast<float>(bounds.x + inset), static_cast<float>(row),
            static_cast<float>(std::max(0.0, bounds.width - inset * 2.0)), 1.0F};
        SDL_RenderFillRect(renderer, &span);
    }
}

void pill(SDL_Renderer* renderer, PresentationRect bounds, SDL_Color fill,
          SDL_Color border) {
    filledPill(renderer, bounds, border);
    filledPill(renderer,
        {bounds.x + 1.0, bounds.y + 1.0,
         std::max(0.0, bounds.width - 2.0),
         std::max(0.0, bounds.height - 2.0)}, fill);
}

bool centeredLabel(
    TextRenderer& text,
    std::string_view value,
    FontWeight weight,
    float size,
    SDL_Color color,
    PresentationRect bounds,
    std::string& error) {
    const auto measured = text.measureText(value, weight, size, error);
    return measured.has_value() && text.drawText(
        value, weight, size, color,
        SDL_FPoint{
            static_cast<float>(bounds.x +
                (bounds.width - measured->width) * 0.5),
            static_cast<float>(bounds.y +
                (bounds.height - measured->height) * 0.5)},
        error);
}

} // namespace

std::optional<std::size_t> hitTestPauseMenu(
    const PauseMenuGeometry& geometry,
    PresentationPoint point) noexcept {
    if (contains(geometry.resumeButton, point)) return 0;
    if (contains(geometry.quitButton, point)) return 1;
    return std::nullopt;
}

bool renderPauseMenu(
    SDL_Renderer* renderer,
    TextRenderer& text,
    const PauseMenuState& pause,
    PauseMenuGeometry& geometry,
    int outputWidth,
    int outputHeight,
    std::string& error) {
    geometry = {};
    if (!pause.active()) return true;
    const double scale = std::max(0.7, std::min(
        static_cast<double>(outputWidth) / 1440.0,
        static_cast<double>(outputHeight) / 900.0));
    SDL_SetRenderDrawColor(renderer, 4, 6, 8, 190);
    SDL_FRect overlay{0.0F, 0.0F,
        static_cast<float>(outputWidth), static_cast<float>(outputHeight)};
    SDL_RenderFillRect(renderer, &overlay);

    const PresentationRect panel{
        (outputWidth - 430.0 * scale) * 0.5,
        (outputHeight - 250.0 * scale) * 0.5,
        430.0 * scale,
        250.0 * scale,
    };
    SDL_FRect panelRect{static_cast<float>(panel.x), static_cast<float>(panel.y),
        static_cast<float>(panel.width), static_cast<float>(panel.height)};
    SDL_SetRenderDrawColor(renderer, ui::Theme::surface.r, ui::Theme::surface.g,
        ui::Theme::surface.b, ui::Theme::surface.a);
    SDL_RenderFillRect(renderer, &panelRect);
    SDL_SetRenderDrawColor(renderer, ui::Theme::border.r, ui::Theme::border.g,
        ui::Theme::border.b, ui::Theme::border.a);
    SDL_RenderRect(renderer, &panelRect);

    if (!centeredLabel(text, "PAUSED", FontWeight::Bold,
            static_cast<float>(24.0 * scale), ui::Theme::gold,
            {panel.x, panel.y + 28.0 * scale, panel.width, 36.0 * scale}, error))
        return false;

    geometry.resumeButton = {panel.x + 42.0 * scale,
        panel.y + 92.0 * scale, panel.width - 84.0 * scale, 44.0 * scale};
    geometry.quitButton = {panel.x + 42.0 * scale,
        panel.y + 156.0 * scale, panel.width - 84.0 * scale, 44.0 * scale};
    const bool resumeSelected = pause.selectedIndex() == 0;
    pill(renderer, geometry.resumeButton,
        resumeSelected ? ui::Theme::surfaceSoft : ui::Theme::surfaceRaised,
        resumeSelected ? ui::Theme::gold : ui::Theme::border);
    pill(renderer, geometry.quitButton,
        !resumeSelected ? ui::Theme::surfaceSoft : ui::Theme::surfaceRaised,
        !resumeSelected ? ui::Theme::red : ui::Theme::border);
    return centeredLabel(text, "RESUME", FontWeight::SemiBold,
               static_cast<float>(12.0 * scale),
               resumeSelected ? ui::Theme::gold : ui::Theme::text,
               geometry.resumeButton, error) &&
           centeredLabel(text, "QUIT GAME", FontWeight::SemiBold,
               static_cast<float>(12.0 * scale),
               !resumeSelected ? ui::Theme::red : ui::Theme::mutedBright,
               geometry.quitButton, error);
}

} // namespace basilisk::game
