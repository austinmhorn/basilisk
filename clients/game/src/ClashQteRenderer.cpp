#include "ClashQteRenderer.hpp"
#include <algorithm>
#include "UITheme.hpp"
namespace basilisk::game {
bool renderClashQte(SDL_Renderer* renderer, TextRenderer& text,
    const network::ClashStarted& clash, std::string_view input, int width, int height,
    std::string& error) {
    const double scale = std::max(0.7, std::min(width / 1440.0, height / 900.0));
    SDL_SetRenderDrawColor(renderer, 4, 6, 8, 205);
    SDL_FRect overlay{0, 0, static_cast<float>(width), static_cast<float>(height)};
    SDL_RenderFillRect(renderer, &overlay);
    SDL_FRect panel{static_cast<float>((width - 520 * scale) * .5),
        static_cast<float>((height - 290 * scale) * .5), static_cast<float>(520 * scale),
        static_cast<float>(290 * scale)};
    SDL_SetRenderDrawColor(renderer, ui::Theme::surface.r, ui::Theme::surface.g,
        ui::Theme::surface.b, ui::Theme::surface.a); SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, ui::Theme::gold.r, ui::Theme::gold.g,
        ui::Theme::gold.b, ui::Theme::gold.a); SDL_RenderRect(renderer, &panel);
    auto centered = [&](std::string_view value, FontWeight weight, float size,
                        SDL_Color color, float y) {
        const auto measured = text.measureText(value, weight, size, error);
        return measured && text.drawText(value, weight, size, color,
            {panel.x + (panel.w - measured->width) * .5F, y}, error);
    };
    if (!centered("HUNTER CLASH", FontWeight::Bold, static_cast<float>(22 * scale),
                  ui::Theme::gold, panel.y + static_cast<float>(30 * scale)) ||
        !centered(clash.challengeWord, FontWeight::Bold, static_cast<float>(38 * scale),
                  ui::Theme::text, panel.y + static_cast<float>(82 * scale))) return false;
    SDL_FRect field{panel.x + static_cast<float>(55 * scale), panel.y + static_cast<float>(158 * scale),
        panel.w - static_cast<float>(110 * scale), static_cast<float>(48 * scale)};
    SDL_SetRenderDrawColor(renderer, ui::Theme::surfaceRaised.r, ui::Theme::surfaceRaised.g,
        ui::Theme::surfaceRaised.b, 255); SDL_RenderFillRect(renderer, &field);
    SDL_SetRenderDrawColor(renderer, ui::Theme::border.r, ui::Theme::border.g,
        ui::Theme::border.b, 255); SDL_RenderRect(renderer, &field);
    if (!input.empty() && !text.drawText(input, FontWeight::Medium,
        static_cast<float>(18 * scale), ui::Theme::text,
        {field.x + static_cast<float>(14 * scale), field.y + static_cast<float>(12 * scale)}, error)) return false;
    return centered("TYPE THE WORD · ENTER TO SUBMIT", FontWeight::Medium,
        static_cast<float>(11 * scale), ui::Theme::mutedBright,
        panel.y + static_cast<float>(230 * scale));
}
}
