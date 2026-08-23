#include "AuthScreenRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "UITheme.hpp"

namespace basilisk::game {
namespace {
SDL_FRect rect(PresentationRect value) {
    return {static_cast<float>(value.x), static_cast<float>(value.y),
            static_cast<float>(value.width), static_cast<float>(value.height)};
}
void panel(SDL_Renderer* renderer, PresentationRect bounds, SDL_Color fill,
           SDL_Color border) {
    SDL_FRect area = rect(bounds);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &area);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderRect(renderer, &area);
}
void filledCircle(SDL_Renderer* renderer, float centerX, float centerY,
                  float radius, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const int extent = static_cast<int>(std::ceil(radius));
    for (int y = -extent; y <= extent; ++y) {
        const float vertical = static_cast<float>(y);
        const float horizontal = std::sqrt(std::max(
            0.0F, radius * radius - vertical * vertical));
        SDL_RenderLine(renderer, centerX - horizontal, centerY + vertical,
                       centerX + horizontal, centerY + vertical);
    }
}
void filledPill(SDL_Renderer* renderer, PresentationRect bounds, SDL_Color color) {
    const float radius = static_cast<float>(bounds.height * 0.5);
    SDL_FRect center{
        static_cast<float>(bounds.x) + radius, static_cast<float>(bounds.y),
        static_cast<float>(std::max(0.0, bounds.width - bounds.height)),
        static_cast<float>(bounds.height)};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &center);
    filledCircle(renderer, static_cast<float>(bounds.x) + radius,
        static_cast<float>(bounds.y) + radius, radius, color);
    filledCircle(renderer, static_cast<float>(bounds.x + bounds.width) - radius,
        static_cast<float>(bounds.y) + radius, radius, color);
}
void pill(SDL_Renderer* renderer, PresentationRect bounds, SDL_Color fill,
          SDL_Color border) {
    filledPill(renderer, bounds, border);
    constexpr double inset = 1.0;
    filledPill(renderer,
        {bounds.x + inset, bounds.y + inset, bounds.width - inset * 2.0,
         bounds.height - inset * 2.0}, fill);
}
bool label(TextRenderer& text, std::string_view value, FontWeight weight,
           float size, SDL_Color color, double x, double y, std::string& error) {
    return text.drawText(value, weight, size, color,
        SDL_FPoint{static_cast<float>(x), static_cast<float>(y)}, error);
}
bool labelCentered(TextRenderer& text, std::string_view value, FontWeight weight,
                   float size, SDL_Color color, double centerX, double y,
                   std::string& error) {
    const auto measured = text.measureText(value, weight, size, error);
    return measured.has_value() && label(text, value, weight, size, color,
        centerX - static_cast<double>(measured->width) * 0.5, y, error);
}
bool trackedBrand(TextRenderer& text, double centerX, double y, float size,
                  std::string& error) {
    constexpr std::string_view brand = "BASILISK";
    const float tracking = size * 0.28F;
    float width = tracking * static_cast<float>(brand.size() - 1);
    std::vector<TextSize> sizes(brand.size());
    for (std::size_t index = 0; index < brand.size(); ++index) {
        const auto measured = text.measureText(
            brand.substr(index, 1), FontWeight::Bold, size, error);
        if (!measured.has_value()) return false;
        sizes[index] = *measured;
        width += static_cast<float>(measured->width);
    }
    float x = static_cast<float>(centerX) - width * 0.5F;
    for (std::size_t index = 0; index < brand.size(); ++index) {
        if (!text.drawText(brand.substr(index, 1), FontWeight::Bold, size,
                ui::Theme::gold, {x, static_cast<float>(y)}, error)) return false;
        x += static_cast<float>(sizes[index].width) + tracking;
    }
    return true;
}
}

bool hitTest(PresentationRect b, PresentationPoint p) noexcept {
    return p.x >= b.x && p.x <= b.x + b.width &&
           p.y >= b.y && p.y <= b.y + b.height;
}

bool renderAuthScreen(
    SDL_Renderer* renderer, TextRenderer& text, SvgTextureManager& svgTextures,
    const AuthScreenState& state, AuthScreenGeometry& geometry,
    int outputWidth, int outputHeight, std::string& error) {
    if (renderer == nullptr || outputWidth <= 0 || outputHeight <= 0) return false;
    const double scale = std::max(0.75, std::min(
        outputWidth / 1440.0, outputHeight / 900.0));
    const double height = (state.mode() == AuthMode::SignIn ? 690.0 : 840.0) * scale;
    const PresentationRect shell{outputWidth * .5 - 330.0 * scale,
        outputHeight * .5 - height * .5, 660.0 * scale, height};
    panel(renderer, shell, ui::Theme::surface, ui::Theme::border);
    const double left = shell.x + 54.0 * scale;
    const double centerX = shell.x + shell.width * 0.5;
    constexpr double heroDiameter = 104.0;
    const float heroSize = static_cast<float>(heroDiameter * scale);
    const float heroCenterY = static_cast<float>(shell.y + 91.0 * scale);
    filledCircle(renderer, static_cast<float>(centerX), heroCenterY,
        heroSize * 0.5F, ui::Theme::gold);
    const SDL_FRect iconBounds{
        static_cast<float>(centerX) - heroSize * 0.32F,
        heroCenterY - heroSize * 0.32F,
        heroSize * 0.64F,
        heroSize * 0.64F};
    if (!svgTextures.drawAuthoredAspectFit(
            SvgAssetId::ObjectiveBasilisk, iconBounds, 1.0F, error) ||
        !trackedBrand(text, centerX, shell.y + 157.0 * scale,
            static_cast<float>(42.0 * scale), error) ||
        !labelCentered(text,
            state.mode() == AuthMode::SignIn ? "SIGN IN" : "CREATE ACCOUNT",
            FontWeight::SemiBold, 18.0F * scale, ui::Theme::text,
            centerX, shell.y + 221.0 * scale, error)) return false;
    geometry.fields.clear();
    struct Field { AuthField id; const char* label; const std::string* value; };
    const std::array fields{
        Field{AuthField::Email, "EMAIL", &state.email()},
        Field{AuthField::Password, "PASSWORD", &state.password()},
        Field{AuthField::Username, "USERNAME", &state.username()}};
    double y = shell.y + 265.0 * scale;
    const std::size_t count = state.mode() == AuthMode::SignIn ? 2 : 3;
    for (std::size_t index = 0; index < count; ++index) {
        const Field& field = fields[index];
        if (!label(text, field.label, FontWeight::SemiBold, 9.0F * scale,
                ui::Theme::muted, left, y, error)) return false;
        const PresentationRect bounds{left, y + 21.0 * scale,
            552.0 * scale, 48.0 * scale};
        panel(renderer, bounds, ui::Theme::surfaceRaised,
            state.field() == field.id ? ui::Theme::gold : ui::Theme::border);
        const std::string shown = field.id == AuthField::Password
            ? std::string(field.value->size(), '*') : *field.value;
        if (!label(text, shown.empty() ? " " : shown, FontWeight::Regular,
                12.0F * scale, ui::Theme::text,
                bounds.x + 15.0 * scale, bounds.y + 14.0 * scale, error)) return false;
        geometry.fields.push_back({field.id, bounds});
        y += 89.0 * scale;
    }
    geometry.submit = {left, y, 552.0 * scale, 50.0 * scale};
    pill(renderer, geometry.submit, ui::Theme::surfaceSoft, ui::Theme::gold);
    if (!label(text, state.waiting() ? "AUTHENTICATING..." :
            (state.mode() == AuthMode::SignIn ? "SIGN IN" : "CREATE ACCOUNT"),
            FontWeight::SemiBold, 11.0F * scale, ui::Theme::gold,
            left + 18.0 * scale, y + 16.0 * scale, error)) return false;
    geometry.switchMode = {left, y + 62.0 * scale, 552.0 * scale, 34.0 * scale};
    if (!label(text, state.mode() == AuthMode::SignIn
            ? "NEED AN ACCOUNT?  CREATE ACCOUNT"
            : "ALREADY HAVE AN ACCOUNT?  SIGN IN",
            FontWeight::Medium, 10.0F * scale, ui::Theme::mutedBright,
            left, geometry.switchMode.y + 8.0 * scale, error)) return false;
    if (!state.error().empty() && !label(text, state.error(), FontWeight::Medium,
            10.0F * scale, ui::Theme::red, left,
            geometry.switchMode.y + 45.0 * scale, error)) return false;
    error.clear();
    return true;
}
} // namespace basilisk::game
