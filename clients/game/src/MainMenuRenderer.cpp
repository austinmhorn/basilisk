#include "MainMenuRenderer.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

#include "UITheme.hpp"

namespace basilisk::game {
namespace {

bool contains(PresentationRect bounds, PresentationPoint point) {
    return point.x >= bounds.x && point.x <= bounds.x + bounds.width &&
           point.y >= bounds.y && point.y <= bounds.y + bounds.height;
}

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

std::string_view actionLabel(MainMenuAction action) {
    switch (action) {
        case MainMenuAction::StartGame: return "START GAME";
        case MainMenuAction::Leaderboards: return "LEADERBOARDS";
        case MainMenuAction::Settings: return "SETTINGS";
        case MainMenuAction::Exit: return "EXIT";
        case MainMenuAction::FindGame: return "FIND GAME";
        case MainMenuAction::HostGame: return "HOST GAME";
        case MainMenuAction::JoinGame: return "JOIN GAME";
        case MainMenuAction::Back: return "BACK";
        case MainMenuAction::PreviousPage: return "PREVIOUS";
        case MainMenuAction::NextPage: return "NEXT";
    }
    return {};
}

std::string_view pageTitle(MainMenuPage page) {
    switch (page) {
        case MainMenuPage::Main: return "ENTER THE CAVERNS";
        case MainMenuPage::StartGame: return "START GAME";
        case MainMenuPage::Leaderboards: return "TROPHY LEADERBOARD";
        case MainMenuPage::Settings: return "SETTINGS";
    }
    return {};
}

} // namespace

std::optional<std::size_t> hitTestMainMenu(
    const MainMenuGeometry& geometry,
    PresentationPoint point) noexcept {
    for (std::size_t index = 0; index < geometry.buttons.size(); ++index) {
        if (contains(geometry.buttons[index].bounds, point)) return index;
    }
    return std::nullopt;
}

bool renderMainMenu(
    SDL_Renderer* renderer,
    TextRenderer& text,
    const MainMenuState& menu,
    std::optional<std::int64_t> trophyTotal,
    const std::optional<network::LeaderboardPageResponse>& leaderboard,
    MainMenuGeometry& geometry,
    int outputWidth,
    int outputHeight,
    std::string& error) {

    if (renderer == nullptr || outputWidth <= 0 || outputHeight <= 0) {
        error = "Main menu requires a valid render target.";
        return false;
    }
    const double scale = std::max(0.7, std::min(
        static_cast<double>(outputWidth) / 1440.0,
        static_cast<double>(outputHeight) / 900.0));
    const double width = std::min(760.0 * scale, outputWidth * 0.82);
    const double height = std::min(680.0 * scale, outputHeight * 0.84);
    const PresentationRect shell{
        (outputWidth - width) * 0.5,
        (outputHeight - height) * 0.5,
        width,
        height,
    };
    panel(renderer, shell, ui::Theme::surface, ui::Theme::border);
    const double left = shell.x + 48.0 * scale;
    if (!label(text, "BASILISK", FontWeight::Bold,
            static_cast<float>(30.0 * scale), ui::Theme::gold,
            left, shell.y + 40.0 * scale, error) ||
        !label(text, "PLAYER FIELD OPERATIONS", FontWeight::Medium,
            static_cast<float>(10.0 * scale), ui::Theme::muted,
            left, shell.y + 82.0 * scale, error) ||
        !label(text, pageTitle(menu.page()), FontWeight::SemiBold,
            static_cast<float>(17.0 * scale), ui::Theme::text,
            left, shell.y + 125.0 * scale, error)) return false;

    geometry.buttons.clear();
    double buttonY = shell.y + 180.0 * scale;
    if (menu.page() == MainMenuPage::Leaderboards) {
        const std::string trophies = trophyTotal.has_value()
            ? "YOUR TROPHIES  " + std::to_string(*trophyTotal)
            : "YOUR TROPHIES  UNAVAILABLE";
        if (!label(text, trophies, FontWeight::SemiBold,
                static_cast<float>(13.0 * scale), ui::Theme::gold,
                left, buttonY, error)) return false;
        buttonY += 42.0 * scale;
        const std::array headings{"RANK", "HUNTER", "HANDLE", "TROPHIES"};
        const std::array<double, 4> columns{0.0, 90.0, 330.0, 550.0};
        for (std::size_t index = 0; index < headings.size(); ++index) {
            if (!label(text, headings[index], FontWeight::SemiBold,
                    static_cast<float>(9.0 * scale), ui::Theme::muted,
                    left + columns[index] * scale, buttonY, error)) return false;
        }
        buttonY += 28.0 * scale;
        if (!leaderboard.has_value() ||
            leaderboard->offset != menu.leaderboardOffset()) {
            if (!label(text, "Leaderboard data unavailable.", FontWeight::Regular,
                    static_cast<float>(12.0 * scale), ui::Theme::mutedBright,
                    left, buttonY, error)) return false;
        } else if (leaderboard->entries.empty()) {
            if (!label(text, "No ranked hunters on this page.", FontWeight::Regular,
                    static_cast<float>(12.0 * scale), ui::Theme::mutedBright,
                    left, buttonY, error)) return false;
        } else {
            for (const PublicTrophyLeaderboardEntry& entry : leaderboard->entries) {
                char rank[24]{};
                std::snprintf(rank, sizeof(rank), "%zu", entry.rank);
                const std::array values{
                    std::string{rank}, entry.displayName,
                    "@" + entry.handle.value, std::to_string(entry.trophyTotal)};
                for (std::size_t index = 0; index < values.size(); ++index) {
                    if (!label(text, values[index], FontWeight::Medium,
                            static_cast<float>(11.0 * scale), ui::Theme::text,
                            left + columns[index] * scale, buttonY, error))
                        return false;
                }
                buttonY += 30.0 * scale;
            }
        }
        buttonY = shell.y + shell.height - 88.0 * scale;
    } else if (menu.page() == MainMenuPage::StartGame) {
        if (!label(text,
                "Online play options are coming in a future phase.",
                FontWeight::Regular, static_cast<float>(11.0 * scale),
                ui::Theme::mutedBright, left, buttonY, error)) return false;
        buttonY += 44.0 * scale;
    } else if (menu.page() == MainMenuPage::Settings) {
        if (!label(text, "Settings are coming soon.", FontWeight::Regular,
                static_cast<float>(12.0 * scale), ui::Theme::mutedBright,
                left, buttonY, error)) return false;
        buttonY += 54.0 * scale;
    }

    const auto actions = menu.actions();
    const bool compact = menu.page() == MainMenuPage::Leaderboards;
    const double buttonWidth = compact ? 170.0 * scale : 430.0 * scale;
    const double buttonHeight = 48.0 * scale;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const PresentationRect bounds{
            left + (compact ? index * 184.0 * scale : 0.0), buttonY,
            buttonWidth, buttonHeight};
        const bool selected = index == menu.selectedIndex();
        panel(renderer, bounds,
            selected ? ui::Theme::surfaceSoft : ui::Theme::surfaceRaised,
            selected ? ui::Theme::gold : ui::Theme::border);
        if (!label(text, actionLabel(actions[index]), FontWeight::SemiBold,
                static_cast<float>(11.0 * scale),
                selected ? ui::Theme::gold : ui::Theme::text,
                bounds.x + 18.0 * scale, bounds.y + 15.0 * scale, error))
            return false;
        geometry.buttons.push_back({actions[index], bounds});
        if (!compact) buttonY += 62.0 * scale;
    }
    error.clear();
    return true;
}

} // namespace basilisk::game
