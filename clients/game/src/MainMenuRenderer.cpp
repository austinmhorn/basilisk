#include "MainMenuRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

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
    const float x = static_cast<float>(bounds.x);
    const float y = static_cast<float>(bounds.y);
    const float width = static_cast<float>(bounds.width);
    const float height = static_cast<float>(bounds.height);
    const float radius = height * 0.5F;
    SDL_FRect center{x + radius, y, std::max(0.0F, width - height), height};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &center);
    filledCircle(renderer, x + radius, y + radius, radius, color);
    filledCircle(renderer, x + width - radius, y + radius, radius, color);
}

void pill(SDL_Renderer* renderer, PresentationRect bounds, SDL_Color fill,
          SDL_Color border) {
    filledPill(renderer, bounds, border);
    constexpr double inset = 1.0;
    const PresentationRect interior{
        bounds.x + inset, bounds.y + inset,
        std::max(0.0, bounds.width - inset * 2.0),
        std::max(0.0, bounds.height - inset * 2.0),
    };
    filledPill(renderer, interior, fill);
}

void filledRoundedRect(SDL_Renderer* renderer, PresentationRect bounds,
                       double radius, SDL_Color color) {
    radius = std::clamp(radius, 0.0,
        std::min(bounds.width, bounds.height) * 0.5);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_FRect horizontal{
        static_cast<float>(bounds.x + radius), static_cast<float>(bounds.y),
        static_cast<float>(std::max(0.0, bounds.width - radius * 2.0)),
        static_cast<float>(bounds.height),
    };
    SDL_FRect vertical{
        static_cast<float>(bounds.x), static_cast<float>(bounds.y + radius),
        static_cast<float>(bounds.width),
        static_cast<float>(std::max(0.0, bounds.height - radius * 2.0)),
    };
    SDL_RenderFillRect(renderer, &horizontal);
    SDL_RenderFillRect(renderer, &vertical);
    const float r = static_cast<float>(radius);
    filledCircle(renderer, static_cast<float>(bounds.x + radius),
        static_cast<float>(bounds.y + radius), r, color);
    filledCircle(renderer, static_cast<float>(bounds.x + bounds.width - radius),
        static_cast<float>(bounds.y + radius), r, color);
    filledCircle(renderer, static_cast<float>(bounds.x + radius),
        static_cast<float>(bounds.y + bounds.height - radius), r, color);
    filledCircle(renderer,
        static_cast<float>(bounds.x + bounds.width - radius),
        static_cast<float>(bounds.y + bounds.height - radius), r, color);
}

void roundedPanel(SDL_Renderer* renderer, PresentationRect bounds, double radius,
                  SDL_Color fill, SDL_Color border) {
    filledRoundedRect(renderer, bounds, radius, border);
    constexpr double inset = 1.0;
    filledRoundedRect(renderer,
        PresentationRect{bounds.x + inset, bounds.y + inset,
            std::max(0.0, bounds.width - inset * 2.0),
            std::max(0.0, bounds.height - inset * 2.0)},
        std::max(0.0, radius - inset), fill);
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
    if (!measured.has_value()) return false;
    return label(text, value, weight, size, color,
        centerX - static_cast<double>(measured->width) * 0.5, y, error);
}

bool trackedLabelCentered(TextRenderer& text, std::string_view value,
                          FontWeight weight, float size, SDL_Color color,
                          double centerX, double y, float trackingEm,
                          std::string& error) {
    if (value.empty()) return true;
    const float tracking = size * trackingEm;
    float width = tracking * static_cast<float>(value.size() - 1);
    std::vector<TextSize> sizes(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto measured = text.measureText(value.substr(index, 1), weight, size, error);
        if (!measured.has_value()) return false;
        sizes[index] = *measured;
        width += static_cast<float>(measured->width);
    }
    float x = static_cast<float>(centerX) - width * 0.5F;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (!text.drawText(value.substr(index, 1), weight, size, color,
                SDL_FPoint{x, static_cast<float>(y)}, error)) return false;
        x += static_cast<float>(sizes[index].width) + tracking;
    }
    return true;
}

std::string_view actionLabel(MainMenuAction action) {
    switch (action) {
        case MainMenuAction::StartGame: return "START GAME";
        case MainMenuAction::Leaderboards: return "LEADERBOARDS";
        case MainMenuAction::Settings: return "SETTINGS";
        case MainMenuAction::EditProfile: return "EDIT";
        case MainMenuAction::Exit: return "EXIT";
        case MainMenuAction::FindGame: return "FIND GAME";
        case MainMenuAction::HostGame: return "HOST GAME";
        case MainMenuAction::JoinGame: return "JOIN GAME";
        case MainMenuAction::Back: return "BACK";
        case MainMenuAction::PreviousPage: return "PREVIOUS";
        case MainMenuAction::NextPage: return "NEXT";
        case MainMenuAction::Logout: return "LOG OUT";
        case MainMenuAction::SubmitLobbyCode: return "JOIN";
        case MainMenuAction::CancelLobby: return "CANCEL";
        case MainMenuAction::CancelFindMatch: return "CANCEL";
    }
    return {};
}

std::string_view pageTitle(MainMenuPage page) {
    switch (page) {
        case MainMenuPage::Main: return "ENTER THE CAVERNS";
        case MainMenuPage::StartGame: return "START GAME";
        case MainMenuPage::Leaderboards: return "TROPHY LEADERBOARD";
        case MainMenuPage::Settings: return "SETTINGS";
        case MainMenuPage::Cosmetics: return "COSMETICS";
        case MainMenuPage::HostLobby: return "HOST GAME";
        case MainMenuPage::JoinLobby: return "JOIN GAME";
        case MainMenuPage::MatchReady: return "MATCH READY";
        case MainMenuPage::FindMatch: return "FIND GAME";
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
    SvgTextureManager& svgTextures,
    const MainMenuState& menu,
    std::optional<std::int64_t> trophyTotal,
    const std::optional<PublicAccountProfile>& authenticatedProfile,
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
    const double centerX = shell.x + shell.width * 0.5;
    if (menu.page() == MainMenuPage::Main) {
        constexpr double heroDiameter = 104.0;
        const float heroSize = static_cast<float>(heroDiameter * scale);
        const float heroCenterY = static_cast<float>(shell.y + 91.0 * scale);
        filledCircle(renderer, static_cast<float>(centerX), heroCenterY,
                     heroSize * 0.5F, ui::Theme::gold);
        const SDL_FRect iconBounds{
            static_cast<float>(centerX) - heroSize * 0.32F,
            heroCenterY - heroSize * 0.32F,
            heroSize * 0.64F,
            heroSize * 0.64F,
        };
        if (!svgTextures.drawAuthoredAspectFit(
                SvgAssetId::ObjectiveBasilisk, iconBounds, 1.0F, error) ||
            !trackedLabelCentered(text, "BASILISK", FontWeight::Bold,
                static_cast<float>(42.0 * scale), ui::Theme::gold,
                centerX, shell.y + 157.0 * scale, 0.28F, error) ||
            !labelCentered(text, pageTitle(menu.page()), FontWeight::SemiBold,
                static_cast<float>(13.0 * scale), ui::Theme::text,
                centerX, shell.y + 221.0 * scale, error)) return false;
    } else if (!trackedLabelCentered(text, "BASILISK", FontWeight::Bold,
                   static_cast<float>(30.0 * scale), ui::Theme::gold,
                   shell.x + 131.0 * scale, shell.y + 40.0 * scale, 0.28F, error) ||
               !label(text, "PLAYER FIELD OPERATIONS", FontWeight::Medium,
                   static_cast<float>(10.0 * scale), ui::Theme::muted,
                   left, shell.y + 82.0 * scale, error) ||
               !label(text, pageTitle(menu.page()), FontWeight::SemiBold,
                   static_cast<float>(17.0 * scale), ui::Theme::text,
                   left, shell.y + 125.0 * scale, error)) {
        return false;
    }
    if (menu.page() != MainMenuPage::Main && authenticatedProfile.has_value()) {
        const std::string identity = authenticatedProfile->displayName +
            "  @" + authenticatedProfile->handle.value;
        if (!label(text, identity, FontWeight::Medium,
                static_cast<float>(10.0 * scale), ui::Theme::mutedBright,
                shell.x + shell.width - 300.0 * scale,
                shell.y + 50.0 * scale, error)) return false;
    }

    geometry.buttons.clear();
    double buttonY = shell.y +
        (menu.page() == MainMenuPage::Main ? 275.0 : 180.0) * scale;
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
    } else if (menu.page() == MainMenuPage::Cosmetics) {
        if (!label(text, "Customize your calling card and emblem.",
                FontWeight::Medium, static_cast<float>(13.0 * scale),
                ui::Theme::text, left, buttonY, error) ||
            !label(text, "Cosmetic loadouts are coming soon.",
                FontWeight::Regular, static_cast<float>(11.0 * scale),
                ui::Theme::mutedBright, left, buttonY + 34.0 * scale, error))
            return false;
        buttonY += 92.0 * scale;
    } else if (menu.page() == MainMenuPage::HostLobby) {
        if (!menu.lobbyCode().empty()) {
            if (!label(text, "LOBBY CODE", FontWeight::SemiBold,
                    static_cast<float>(10.0 * scale), ui::Theme::muted,
                    left, buttonY, error) ||
                !label(text, menu.lobbyCode(), FontWeight::Bold,
                    static_cast<float>(32.0 * scale), ui::Theme::gold,
                    left, buttonY + 30.0 * scale, error) ||
                !label(text, "Waiting for player...", FontWeight::Medium,
                    static_cast<float>(13.0 * scale), ui::Theme::mutedBright,
                    left, buttonY + 82.0 * scale, error)) return false;
        } else if (!label(text, "Creating lobby...", FontWeight::Medium,
                static_cast<float>(13.0 * scale), ui::Theme::mutedBright,
                left, buttonY, error)) return false;
        buttonY += 140.0 * scale;
    } else if (menu.page() == MainMenuPage::JoinLobby) {
        if (!label(text, "LOBBY CODE", FontWeight::SemiBold,
                static_cast<float>(10.0 * scale), ui::Theme::muted,
                left, buttonY, error)) return false;
        const PresentationRect input{left, buttonY + 24.0 * scale,
            430.0 * scale, 54.0 * scale};
        panel(renderer, input, ui::Theme::surfaceRaised, ui::Theme::gold);
        if (!label(text, menu.lobbyCode().empty() ? "ENTER CODE" : menu.lobbyCode(),
                FontWeight::SemiBold, static_cast<float>(16.0 * scale),
                menu.lobbyCode().empty() ? ui::Theme::muted : ui::Theme::text,
                input.x + 18.0 * scale, input.y + 16.0 * scale, error)) return false;
        if (!menu.lobbyError().empty() && !label(text, menu.lobbyError(),
                FontWeight::Medium, static_cast<float>(10.0 * scale),
                ui::Theme::red, left, buttonY + 91.0 * scale, error)) return false;
        buttonY += 130.0 * scale;
    } else if (menu.page() == MainMenuPage::MatchReady) {
        if (!label(text, "MATCH READY", FontWeight::Bold,
                static_cast<float>(28.0 * scale), ui::Theme::gold,
                left, buttonY, error) ||
            !label(text, "Lobby " + menu.lobbyCode(), FontWeight::Medium,
                static_cast<float>(13.0 * scale), ui::Theme::text,
                left, buttonY + 48.0 * scale, error) ||
            !label(text, "Gameplay launch is coming next.", FontWeight::Regular,
                static_cast<float>(11.0 * scale), ui::Theme::mutedBright,
                left, buttonY + 78.0 * scale, error)) return false;
        buttonY += 126.0 * scale;
    } else if (menu.page() == MainMenuPage::FindMatch) {
        if (!label(text, "Searching for another hunter...", FontWeight::Medium,
                static_cast<float>(15.0 * scale), ui::Theme::text,
                left, buttonY, error) ||
            !label(text, "Waiting in matchmaking queue.", FontWeight::Regular,
                static_cast<float>(11.0 * scale), ui::Theme::mutedBright,
                left, buttonY + 38.0 * scale, error)) return false;
        buttonY += 100.0 * scale;
    }

    const auto actions = menu.actions();
    const bool compact = menu.page() == MainMenuPage::Leaderboards;
    const double buttonWidth = compact ? 170.0 * scale : 430.0 * scale;
    const double buttonHeight = 48.0 * scale;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (actions[index] == MainMenuAction::EditProfile) continue;
        const PresentationRect bounds{
            menu.page() == MainMenuPage::Main
                ? centerX - buttonWidth * 0.5
                : left + (compact ? index * 184.0 * scale : 0.0),
            buttonY,
            buttonWidth, buttonHeight};
        const bool selected = index == menu.selectedIndex();
        pill(renderer, bounds,
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

    if (menu.page() == MainMenuPage::Main) {
        const PresentationRect profile{
            centerX - 215.0 * scale,
            shell.y + shell.height - 98.0 * scale,
            430.0 * scale,
            68.0 * scale,
        };
        const bool editSelected = menu.selectedAction() == MainMenuAction::EditProfile;
        roundedPanel(renderer, profile, 12.0 * scale, ui::Theme::surfaceRaised,
            editSelected ? ui::Theme::gold : ui::Theme::borderSoft);

        const SDL_FRect emblemBounds{
            static_cast<float>(profile.x + 13.0 * scale),
            static_cast<float>(profile.y + 11.0 * scale),
            static_cast<float>(46.0 * scale),
            static_cast<float>(46.0 * scale),
        };
        filledCircle(renderer, emblemBounds.x + emblemBounds.w * 0.5F,
            emblemBounds.y + emblemBounds.h * 0.5F, emblemBounds.w * 0.5F,
            ui::Theme::surfaceSoft);
        // Presentation-only fallback until authenticated accounts expose an
        // equipped cosmetic loadout.
        const SDL_FRect emblemArt{
            emblemBounds.x + 7.0F * static_cast<float>(scale),
            emblemBounds.y + 7.0F * static_cast<float>(scale),
            emblemBounds.w - 14.0F * static_cast<float>(scale),
            emblemBounds.h - 14.0F * static_cast<float>(scale),
        };
        if (!svgTextures.drawAspectFit(SvgAssetId::EmblemCircle, emblemArt,
                0.82F, ui::Theme::mutedBright, error)) return false;

        const std::string displayName = authenticatedProfile.has_value()
            ? authenticatedProfile->displayName : "PLAYER PROFILE";
        const std::string handle = authenticatedProfile.has_value()
            ? "@" + authenticatedProfile->handle.value : "@unavailable";
        const std::string trophies = trophyTotal.has_value()
            ? "TROPHIES  " + std::to_string(*trophyTotal)
            : "TROPHIES  --";
        if (!label(text, displayName, FontWeight::SemiBold,
                static_cast<float>(12.0 * scale), ui::Theme::text,
                profile.x + 75.0 * scale, profile.y + 12.0 * scale, error) ||
            !label(text, handle, FontWeight::Regular,
                static_cast<float>(9.0 * scale), ui::Theme::muted,
                profile.x + 75.0 * scale, profile.y + 38.0 * scale, error) ||
            !label(text, trophies, FontWeight::SemiBold,
                static_cast<float>(9.0 * scale), ui::Theme::gold,
                profile.x + 238.0 * scale, profile.y + 27.0 * scale, error))
            return false;

        const PresentationRect edit{
            profile.x + profile.width - 76.0 * scale,
            profile.y + 18.0 * scale,
            60.0 * scale,
            32.0 * scale,
        };
        pill(renderer, edit,
            editSelected ? ui::Theme::surfaceSoft : ui::Theme::surface,
            editSelected ? ui::Theme::gold : ui::Theme::border);
        if (!labelCentered(text, actionLabel(MainMenuAction::EditProfile),
                FontWeight::SemiBold, static_cast<float>(9.0 * scale),
                editSelected ? ui::Theme::gold : ui::Theme::text,
                edit.x + edit.width * 0.5, edit.y + 9.0 * scale, error))
            return false;
        geometry.buttons.push_back({MainMenuAction::EditProfile, edit});
    }
    error.clear();
    return true;
}

} // namespace basilisk::game
