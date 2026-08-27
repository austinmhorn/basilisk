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
    if (bounds.width <= 0.0 || bounds.height <= 0.0) return;

    const double radius = bounds.height * 0.5;
    const double centerY = bounds.y + radius;
    const int firstRow = static_cast<int>(std::floor(bounds.y));
    const int lastRow = static_cast<int>(std::ceil(
        bounds.y + bounds.height));
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int row = firstRow; row < lastRow; ++row) {
        const double sampleY = static_cast<double>(row) + 0.5;
        if (sampleY < bounds.y || sampleY >= bounds.y + bounds.height) continue;
        const double vertical = std::abs(sampleY - centerY);
        const double horizontal = std::sqrt(std::max(
            0.0, radius * radius - vertical * vertical));
        const double inset = radius - horizontal;
        SDL_FRect span{
            static_cast<float>(bounds.x + inset),
            static_cast<float>(row),
            static_cast<float>(std::max(0.0, bounds.width - inset * 2.0)),
            1.0F,
        };
        SDL_RenderFillRect(renderer, &span);
    }
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

bool drawCallingCardArt(SvgTextureManager& svgTextures,
                        const client::CallingCardId& callingCard,
                        const PresentationRect& bounds, double scale,
                        std::string& error) {
    const auto asset = callingCardAsset(callingCard);
    if (!asset.has_value()) {
        error = "Selected calling card has no registered asset.";
        return false;
    }
    const SDL_FRect art{
        static_cast<float>(bounds.x),
        static_cast<float>(bounds.y),
        static_cast<float>(bounds.width),
        static_cast<float>(bounds.height)};
    return svgTextures.drawAuthoredAspectFit(*asset, art, 1.0F, error);
}

bool drawProfileEmblemSlot(
    SDL_Renderer* renderer, SvgTextureManager& svgTextures,
    const PresentationRect& card, const client::EmblemId& emblemId,
    double scale, std::string& error) {
    constexpr double slotSize = 75.0;
    constexpr double slotGap = 8.0;
    constexpr double emblemSize = 63.0;
    constexpr double emblemInset = (slotSize - emblemSize) * 0.5;
    const PresentationRect slot{
        card.x - (slotSize + slotGap) * scale, card.y,
        slotSize * scale, slotSize * scale};
    roundedPanel(renderer, slot, 8.0 * scale,
        ui::Theme::surfaceRaised, ui::Theme::borderSoft);
    const SDL_FRect emblem{
        static_cast<float>(slot.x + emblemInset * scale),
        static_cast<float>(slot.y + emblemInset * scale),
        static_cast<float>(emblemSize * scale),
        static_cast<float>(emblemSize * scale)};
    const auto asset = emblemAsset(emblemId);
    if (asset.has_value()) {
        return svgTextures.drawAuthoredAspectFit(*asset, emblem, 1.0F, error);
    }
    // Safe presentation fallback for an absent or unknown profile cosmetic.
    filledCircle(renderer, emblem.x + emblem.w * 0.5F,
        emblem.y + emblem.h * 0.5F, emblem.w * 0.28F, ui::Theme::mutedBright);
    return true;
}

bool drawCallingCardNameplate(
    SDL_Renderer* renderer, TextRenderer& text,
    const PresentationRect& card, double scale,
    const std::optional<PublicAccountProfile>& authenticatedProfile,
    std::optional<std::int64_t> trophyTotal, std::string& error) {
    const std::string name = authenticatedProfile.has_value()
        ? authenticatedProfile->username.value : "PLAYER PROFILE";
    const std::string trophies = trophyTotal.has_value()
        ? std::to_string(*trophyTotal) : "--";
    const float nameSizeValue = static_cast<float>(10.0 * scale);
    const float trophyLabelSize = static_cast<float>(6.5 * scale);
    const float trophyValueSize = static_cast<float>(11.0 * scale);
    const auto nameSize = text.measureText(
        name, FontWeight::SemiBold, nameSizeValue, error);
    const auto trophyLabel = text.measureText(
        "TROPHIES", FontWeight::SemiBold, trophyLabelSize, error);
    const auto trophyValue = text.measureText(
        trophies, FontWeight::Bold, trophyValueSize, error);
    if (!nameSize.has_value() ||
        !trophyLabel.has_value() || !trophyValue.has_value()) return false;

    const double identityWidth = nameSize->width;
    const double trophyWidth = std::max(trophyLabel->width, trophyValue->width);
    constexpr double artworkWidth = 400.0;
    constexpr double artworkHeight = 75.0;
    constexpr double nameplateHeight = 54.0;
    constexpr double uniformInset = (artworkHeight - nameplateHeight) * 0.5;
    const double nameplateX = card.x +
        (card.width - artworkWidth * scale) * 0.5 + uniformInset * scale;
    const double nameplateY = card.y + uniformInset * scale;
    const PresentationRect nameplate{
        nameplateX, nameplateY,
        12.0 * scale + identityWidth + 12.0 * scale + trophyWidth +
            22.0 * scale,
        (artworkHeight - uniformInset * 2.0) * scale};
    roundedPanel(renderer, nameplate, 8.0 * scale,
        ui::Theme::surface, ui::Theme::borderSoft);

    const double nameX = nameplate.x + 12.0 * scale;
    const double trophyX = nameX + identityWidth + 12.0 * scale;
    return label(text, name, FontWeight::SemiBold, nameSizeValue,
               ui::Theme::text, nameX, nameplate.y + 18.0 * scale, error) &&
        label(text, "TROPHIES", FontWeight::SemiBold, trophyLabelSize,
               ui::Theme::gold, trophyX, nameplate.y + 8.0 * scale, error) &&
        label(text, trophies, FontWeight::Bold, trophyValueSize,
               ui::Theme::gold, trophyX, nameplate.y + 26.0 * scale, error);
}

std::string_view actionLabel(MainMenuAction action) {
    switch (action) {
        case MainMenuAction::StartGame: return "START GAME";
        case MainMenuAction::PlayOnline: return "PLAY ONLINE";
        case MainMenuAction::PlayAi: return "PLAY AI";
        case MainMenuAction::Sandbox: return "SANDBOX";
        case MainMenuAction::CycleAiDifficulty: return "DIFFICULTY";
        case MainMenuAction::CycleAiBehavior: return "BEHAVIOR";
        case MainMenuAction::StartAiGame: return "BEGIN HUNT";
        case MainMenuAction::CycleSandboxHunters: return "HUNTERS";
        case MainMenuAction::CycleSandboxDifficulty: return "DIFFICULTY";
        case MainMenuAction::CycleSandboxBehavior: return "BEHAVIOR";
        case MainMenuAction::StartSandbox: return "BEGIN SANDBOX";
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
        case MainMenuPage::PlayOnline: return "PLAY ONLINE";
        case MainMenuPage::PlayAi: return "PLAY AI";
        case MainMenuPage::Sandbox: return "SANDBOX";
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

struct CallingCardOption {
    std::string_view id;
    std::string_view name;
};

constexpr std::array callingCardOptions{
    CallingCardOption{"arrow-right-black", "Arrow Right · Black"},
    CallingCardOption{"arrow-right-white", "Arrow Right · White"},
    CallingCardOption{"diamonds-flag-black", "Diamonds Flag · Black"},
    CallingCardOption{"diamonds-flag-white", "Diamonds Flag · White"},
    CallingCardOption{"honeycomb-flag-black", "Honeycomb Flag · Black"},
    CallingCardOption{"honeycomb-flag-white", "Honeycomb Flag · White"},
    CallingCardOption{"slanted-rectangles-black", "Slanted Rectangles · Black"},
    CallingCardOption{"slanted-rectangles-white", "Slanted Rectangles · White"},
};

struct EmblemOption {
    std::string_view id;
    std::string_view name;
};

constexpr std::array emblemOptions{
    EmblemOption{"circle-black", "Circle · Black"},
    EmblemOption{"circle-green", "Circle · Green"},
    EmblemOption{"rounded-square-black", "Rounded Square · Black"},
    EmblemOption{"rounded-square-green", "Rounded Square · Green"},
};

} // namespace

std::optional<std::size_t> hitTestMainMenu(
    const MainMenuGeometry& geometry,
    PresentationPoint point) noexcept {
    for (std::size_t index = 0; index < geometry.buttons.size(); ++index) {
        if (contains(geometry.buttons[index].bounds, point)) return index;
    }
    return std::nullopt;
}

std::optional<client::CallingCardId> hitTestCallingCardGallery(
    const MainMenuGeometry& geometry,
    PresentationPoint point) {
    for (const MainMenuGeometry::CallingCardTile& tile : geometry.callingCards) {
        if (contains(tile.bounds, point)) return tile.callingCard;
    }
    return std::nullopt;
}

std::optional<client::EmblemId> hitTestEmblemGallery(
    const MainMenuGeometry& geometry,
    PresentationPoint point) {
    for (const MainMenuGeometry::EmblemTile& tile : geometry.emblems) {
        if (contains(tile.bounds, point)) return tile.emblem;
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
        const std::string identity = authenticatedProfile->username.value;
        if (!label(text, identity, FontWeight::Medium,
                static_cast<float>(10.0 * scale), ui::Theme::mutedBright,
                shell.x + shell.width - 300.0 * scale,
                shell.y + 50.0 * scale, error)) return false;
    }

    geometry.buttons.clear();
    geometry.callingCards.clear();
    geometry.emblems.clear();
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
        const std::array headings{"RANK", "USERNAME", "TROPHIES"};
        const std::array<double, 3> columns{0.0, 90.0, 550.0};
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
                    std::string{rank}, entry.username.value,
                    std::to_string(entry.trophyTotal)};
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
                "Choose how you enter the caverns.",
                FontWeight::Regular, static_cast<float>(11.0 * scale),
                ui::Theme::mutedBright, left, buttonY, error)) return false;
        buttonY += 44.0 * scale;
    } else if (menu.page() == MainMenuPage::PlayOnline) {
        if (!label(text, "Authenticated online hunt", FontWeight::Regular,
                static_cast<float>(11.0 * scale), ui::Theme::mutedBright,
                left, buttonY, error)) return false;
        buttonY += 44.0 * scale;
    } else if (menu.page() == MainMenuPage::PlayAi) {
        if (!label(text, "Local offline hunt against BASILISK AI",
                FontWeight::Regular, static_cast<float>(11.0 * scale),
                ui::Theme::mutedBright, left, buttonY, error)) return false;
        buttonY += 44.0 * scale;
    } else if (menu.page() == MainMenuPage::Sandbox) {
        if (!label(text, "Local offline hunt with 2-6 hunters",
                FontWeight::Regular, static_cast<float>(11.0 * scale),
                ui::Theme::mutedBright, left, buttonY, error)) return false;
        buttonY += 44.0 * scale;
    } else if (menu.page() == MainMenuPage::Settings) {
        if (!label(text, "Settings are coming soon.", FontWeight::Regular,
                static_cast<float>(12.0 * scale), ui::Theme::mutedBright,
                left, buttonY, error)) return false;
        buttonY += 54.0 * scale;
    } else if (menu.page() == MainMenuPage::Cosmetics) {
        if (!label(text, "YOUR CARD", FontWeight::SemiBold,
                static_cast<float>(10.0 * scale), ui::Theme::muted,
                left, buttonY - 10.0 * scale, error)) return false;
        const PresentationRect preview{
            centerX - 200.0 * scale, buttonY + 12.0 * scale,
            400.0 * scale, 75.0 * scale};
        if (!drawProfileEmblemSlot(renderer, svgTextures, preview,
                menu.selectedEmblem(), scale, error) ||
            !drawCallingCardArt(svgTextures, menu.selectedCallingCard(),
                preview, scale, error) ||
            !drawCallingCardNameplate(renderer, text, preview, scale,
                authenticatedProfile, trophyTotal, error))
            return false;

        const double galleryLabelY = buttonY + 108.0 * scale;
        if (!label(text, "CALLING CARDS", FontWeight::SemiBold,
                static_cast<float>(10.0 * scale), ui::Theme::muted,
                left, galleryLabelY, error)) return false;
        constexpr double tileWidth = 151.0;
        constexpr double tileHeight = 72.0;
        constexpr double columnGap = 10.0;
        constexpr double rowGap = 10.0;
        const double gridTop = galleryLabelY + 22.0 * scale;
        for (std::size_t index = 0; index < callingCardOptions.size(); ++index) {
            const std::size_t column = index % 4;
            const std::size_t row = index / 4;
            const PresentationRect tile{
                left + static_cast<double>(column) *
                    (tileWidth + columnGap) * scale,
                gridTop + static_cast<double>(row) *
                    (tileHeight + rowGap) * scale,
                tileWidth * scale, tileHeight * scale};
            const client::CallingCardId id{std::string{callingCardOptions[index].id}};
            const bool selected = id == menu.selectedCallingCard();
            roundedPanel(renderer, tile, 8.0 * scale, ui::Theme::surfaceRaised,
                selected ? ui::Theme::gold : ui::Theme::borderSoft);
            const auto asset = callingCardAsset(id);
            if (!asset.has_value()) {
                error = "Calling-card gallery contains an unregistered asset.";
                return false;
            }
            const SDL_FRect art{
                static_cast<float>(tile.x + 4.0 * scale),
                static_cast<float>(tile.y + 9.0 * scale),
                static_cast<float>(tile.width - 8.0 * scale),
                static_cast<float>((tileWidth - 8.0) * (75.0 / 400.0) * scale)};
            if (!svgTextures.drawAuthoredAspectFit(*asset, art, 1.0F, error) ||
                !labelCentered(text, callingCardOptions[index].name,
                    FontWeight::Medium, static_cast<float>(8.0 * scale),
                    selected ? ui::Theme::gold : ui::Theme::mutedBright,
                    tile.x + tile.width * 0.5,
                    tile.y + 52.0 * scale, error)) return false;
            geometry.callingCards.push_back({id, tile});
        }
        const double emblemsY = gridTop +
            (tileHeight * 2.0 + rowGap + 18.0) * scale;
        if (!label(text, "EMBLEMS", FontWeight::SemiBold,
                static_cast<float>(10.0 * scale), ui::Theme::muted,
                left, emblemsY, error)) return false;
        constexpr double emblemTileWidth = 151.0;
        constexpr double emblemTileHeight = 68.0;
        const double emblemGridTop = emblemsY + 18.0 * scale;
        for (std::size_t index = 0; index < emblemOptions.size(); ++index) {
            const PresentationRect tile{
                left + static_cast<double>(index) *
                    (emblemTileWidth + columnGap) * scale,
                emblemGridTop,
                emblemTileWidth * scale,
                emblemTileHeight * scale};
            const client::EmblemId id{std::string{emblemOptions[index].id}};
            const bool selected = id == menu.selectedEmblem();
            roundedPanel(renderer, tile, 8.0 * scale, ui::Theme::surfaceRaised,
                selected ? ui::Theme::gold : ui::Theme::borderSoft);
            const auto asset = emblemAsset(id);
            if (!asset.has_value()) {
                error = "Emblem gallery contains an unregistered asset.";
                return false;
            }
            const SDL_FRect art{
                static_cast<float>(tile.x + (tile.width - 42.0 * scale) * 0.5),
                static_cast<float>(tile.y + 4.0 * scale),
                static_cast<float>(42.0 * scale),
                static_cast<float>(42.0 * scale)};
            if (!svgTextures.drawAuthoredAspectFit(*asset, art, 1.0F, error) ||
                !labelCentered(text, emblemOptions[index].name,
                    FontWeight::Medium, static_cast<float>(7.0 * scale),
                    selected ? ui::Theme::gold : ui::Theme::mutedBright,
                    tile.x + tile.width * 0.5,
                    tile.y + 50.0 * scale, error)) return false;
            geometry.emblems.push_back({id, tile});
        }
        buttonY = shell.y + shell.height - 70.0 * scale;
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
        std::string dynamicLabel;
        if (actions[index] == MainMenuAction::CycleAiDifficulty) {
            dynamicLabel = "DIFFICULTY  ";
            dynamicLabel += client::ai::difficultyName(menu.aiDifficulty());
        } else if (actions[index] == MainMenuAction::CycleAiBehavior) {
            dynamicLabel = "BEHAVIOR  ";
            dynamicLabel += client::ai::behaviorName(menu.aiBehavior());
        } else if (actions[index] == MainMenuAction::CycleSandboxHunters) {
            dynamicLabel = "HUNTERS  " + std::to_string(menu.sandboxHunterCount());
        } else if (actions[index] == MainMenuAction::CycleSandboxDifficulty) {
            dynamicLabel = "DIFFICULTY  ";
            dynamicLabel += client::ai::difficultyName(menu.sandboxDifficulty());
        } else if (actions[index] == MainMenuAction::CycleSandboxBehavior) {
            dynamicLabel = "BEHAVIOR  ";
            dynamicLabel += client::ai::behaviorName(menu.sandboxBehavior());
        }
        const std::string_view displayLabel = dynamicLabel.empty()
            ? actionLabel(actions[index]) : std::string_view{dynamicLabel};
        if (!label(text, displayLabel, FontWeight::SemiBold,
                static_cast<float>(11.0 * scale),
                selected ? ui::Theme::gold : ui::Theme::text,
                bounds.x + 18.0 * scale, bounds.y + 15.0 * scale, error))
            return false;
        geometry.buttons.push_back({actions[index], bounds});
        if (!compact) buttonY += 62.0 * scale;
    }

    if (menu.page() == MainMenuPage::Main) {
        constexpr double profileWidth = 400.0;
        constexpr double editGap = 14.0;
        constexpr double editWidth = 60.0;
        const PresentationRect profile{
            centerX - profileWidth * 0.5 * scale,
            shell.y + shell.height - 105.0 * scale,
            profileWidth * scale,
            75.0 * scale,
        };
        const bool editSelected = menu.selectedAction() == MainMenuAction::EditProfile;
        if (!drawProfileEmblemSlot(renderer, svgTextures, profile,
                menu.selectedEmblem(), scale, error) ||
            !drawCallingCardArt(svgTextures, menu.selectedCallingCard(),
                profile, scale, error) ||
            !drawCallingCardNameplate(renderer, text, profile, scale,
                authenticatedProfile, trophyTotal, error))
            return false;

        const PresentationRect edit{
            profile.x + profile.width + editGap * scale,
            profile.y + 21.5 * scale,
            editWidth * scale,
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
