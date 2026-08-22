#include "AuthScreenRenderer.hpp"

#include <algorithm>
#include <array>

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
bool label(TextRenderer& text, std::string_view value, FontWeight weight,
           float size, SDL_Color color, double x, double y, std::string& error) {
    return text.drawText(value, weight, size, color,
        SDL_FPoint{static_cast<float>(x), static_cast<float>(y)}, error);
}
}

bool hitTest(PresentationRect b, PresentationPoint p) noexcept {
    return p.x >= b.x && p.x <= b.x + b.width &&
           p.y >= b.y && p.y <= b.y + b.height;
}

bool renderAuthScreen(
    SDL_Renderer* renderer, TextRenderer& text, const AuthScreenState& state,
    AuthScreenGeometry& geometry, int outputWidth, int outputHeight,
    std::string& error) {
    if (renderer == nullptr || outputWidth <= 0 || outputHeight <= 0) return false;
    const double scale = std::max(0.75, std::min(
        outputWidth / 1440.0, outputHeight / 900.0));
    const double height = (state.mode() == AuthMode::SignIn ? 500.0 : 650.0) * scale;
    const PresentationRect shell{outputWidth * .5 - 330.0 * scale,
        outputHeight * .5 - height * .5, 660.0 * scale, height};
    panel(renderer, shell, ui::Theme::surface, ui::Theme::border);
    const double left = shell.x + 54.0 * scale;
    if (!label(text, "BASILISK", FontWeight::Bold, 30.0F * scale,
            ui::Theme::gold, left, shell.y + 42.0 * scale, error) ||
        !label(text, state.mode() == AuthMode::SignIn ? "SIGN IN" : "CREATE ACCOUNT",
            FontWeight::SemiBold, 18.0F * scale, ui::Theme::text,
            left, shell.y + 104.0 * scale, error)) return false;
    geometry.fields.clear();
    struct Field { AuthField id; const char* label; const std::string* value; };
    const std::array fields{
        Field{AuthField::Login, "LOGIN", &state.login()},
        Field{AuthField::Password, "PASSWORD", &state.password()},
        Field{AuthField::PublicHandle, "PUBLIC HANDLE", &state.publicHandle()},
        Field{AuthField::DisplayName, "DISPLAY NAME", &state.displayName()}};
    double y = shell.y + 154.0 * scale;
    const std::size_t count = state.mode() == AuthMode::SignIn ? 2 : 4;
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
    panel(renderer, geometry.submit, ui::Theme::surfaceSoft, ui::Theme::gold);
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
