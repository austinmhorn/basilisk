#include "ScreenShell.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <sstream>

#include "ActionPresentation.hpp"
#include "MapRenderer.hpp"
#include "SnapshotPresentation.hpp"
#include "UITheme.hpp"
#include "basilisk/client/Presentation.hpp"

#if defined(BASILISK_GAME_DEBUG)
#include "DebugMapRenderer.hpp"
#endif

namespace basilisk::game {
namespace {

constexpr float kReferenceWidth = 1440.0F;
constexpr float kReferenceHeight = 900.0F;

void setColor(SDL_Renderer* renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

SDL_FRect inset(SDL_FRect rectangle, float amount) {
    return SDL_FRect{
        rectangle.x + amount,
        rectangle.y + amount,
        std::max(0.0F, rectangle.w - amount * 2.0F),
        std::max(0.0F, rectangle.h - amount * 2.0F),
    };
}

void fillRoundedRect(
    SDL_Renderer* renderer, SDL_FRect rectangle, float radius, SDL_Color color) {

    if (rectangle.w <= 0.0F || rectangle.h <= 0.0F) return;
    radius = std::clamp(radius, 0.0F, std::min(rectangle.w, rectangle.h) * 0.5F);
    setColor(renderer, color);
    if (radius < 1.0F) {
        SDL_RenderFillRect(renderer, &rectangle);
        return;
    }

    const SDL_FRect vertical{
        rectangle.x,
        rectangle.y + radius,
        rectangle.w,
        std::max(0.0F, rectangle.h - radius * 2.0F),
    };
    const SDL_FRect horizontal{
        rectangle.x + radius,
        rectangle.y,
        std::max(0.0F, rectangle.w - radius * 2.0F),
        rectangle.h,
    };
    SDL_RenderFillRect(renderer, &vertical);
    SDL_RenderFillRect(renderer, &horizontal);

    const int rows = static_cast<int>(std::ceil(radius));
    for (int row = 0; row < rows; ++row) {
        const float distance = radius - static_cast<float>(row) - 0.5F;
        const float extent = std::sqrt(std::max(0.0F, radius * radius - distance * distance));
        SDL_RenderLine(
            renderer,
            rectangle.x + radius - extent,
            rectangle.y + static_cast<float>(row),
            rectangle.x + rectangle.w - radius + extent,
            rectangle.y + static_cast<float>(row));
        SDL_RenderLine(
            renderer,
            rectangle.x + radius - extent,
            rectangle.y + rectangle.h - static_cast<float>(row) - 1.0F,
            rectangle.x + rectangle.w - radius + extent,
            rectangle.y + rectangle.h - static_cast<float>(row) - 1.0F);
    }
}

void drawPanel(
    SDL_Renderer* renderer,
    SDL_FRect rectangle,
    float radius,
    SDL_Color fill,
    SDL_Color border,
    float scale) {

    fillRoundedRect(renderer, rectangle, radius, border);
    const float borderWidth = std::max(1.0F, scale);
    fillRoundedRect(
        renderer,
        inset(rectangle, borderWidth),
        std::max(0.0F, radius - borderWidth),
        fill);
}

struct DrawingContext {
    SDL_Renderer* renderer{};
    TextRenderer* text{};
    SvgTextureManager* svg{};
    float scale{1.0F};
    std::string* error{};

    bool label(
        std::string_view value,
        FontWeight weight,
        float size,
        SDL_Color color,
        float x,
        float y) const {

        return text->drawText(
            value,
            weight,
            size * scale,
            color,
            SDL_FPoint{x, y},
            *error);
    }

    bool centeredLabel(
        std::string_view value,
        FontWeight weight,
        float size,
        SDL_Color color,
        SDL_FRect bounds) const {

        const float pointSize = size * scale;
        const auto measured = text->measureText(value, weight, pointSize, *error);
        if (!measured.has_value()) return false;
        return text->drawText(
            value,
            weight,
            pointSize,
            color,
            SDL_FPoint{
                bounds.x + (bounds.w - static_cast<float>(measured->width)) * 0.5F,
                bounds.y + (bounds.h - static_cast<float>(measured->height)) * 0.5F},
            *error);
    }

    bool asset(
        SvgAssetId id,
        const SDL_FRect& destination,
        float opacity = 1.0F,
        SDL_Color tint = SDL_Color{255, 255, 255, SDL_ALPHA_OPAQUE}) const {
        std::string assetError;
        if (svg->draw(id, destination, opacity, tint, assetError)) return true;
        SDL_Log("SVG asset fallback: %s", assetError.c_str());
        return false;
    }

    bool aspectFitAsset(
        SvgAssetId id,
        const SDL_FRect& destination,
        float opacity,
        SDL_Color tint) const {
        std::string assetError;
        if (svg->drawAspectFit(id, destination, opacity, tint, assetError)) return true;
        SDL_Log("SVG aspect-fit fallback: %s", assetError.c_str());
        return false;
    }

    bool authoredAspectFitAsset(
        SvgAssetId id,
        const SDL_FRect& destination,
        float opacity = 1.0F) const {
        std::string assetError;
        if (svg->drawAuthoredAspectFit(id, destination, opacity, assetError))
            return true;
        SDL_Log("Authored SVG aspect-fit fallback: %s", assetError.c_str());
        return false;
    }
};

std::optional<std::vector<std::string>> wrapTextLines(
    const DrawingContext& context,
    std::string_view text,
    FontWeight weight,
    float size,
    float maxWidth) {

    std::vector<std::string> lines;
    std::istringstream stream(std::string{text});

    std::string word;
    std::string currentLine;

    const float pointSize = size * context.scale;

    while (stream >> word) {
        const std::string candidate =
            currentLine.empty() ? word : currentLine + " " + word;

        const auto measured =
            context.text->measureText(
                candidate,
                weight,
                pointSize,
                *context.error);

        if (!measured.has_value()) {
            return std::nullopt;
        }

        if (!currentLine.empty() &&
            static_cast<float>(measured->width) > maxWidth) {

            lines.push_back(currentLine);
            currentLine = word;
        } else {
            currentLine = candidate;
        }
    }

    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }

    if (lines.empty()) {
        lines.emplace_back();
    }

    return lines;
}

std::optional<float> roundReportHeight(
    const DrawingContext& context,
    const PlayerRoundSnapshot& snapshot,
    float panelWidth) {

    const float scale = context.scale;
    const std::vector<std::string> report = roundReportText(snapshot);

    const float rowWidth = panelWidth - 26.0F * scale;
    const float textWidth = rowWidth - 25.0F * scale;

    std::vector<std::size_t> wrappedLineCounts;
    wrappedLineCounts.reserve(report.size());

    for (const std::string& entry : report) {
        const auto lines = wrapTextLines(
            context,
            entry,
            FontWeight::Regular,
            ui::Typography::reportBody,
            textWidth);

        if (!lines.has_value()) {
            return std::nullopt;
        }

        wrappedLineCounts.push_back(lines->size());
    }

    return roundReportLayout(wrappedLineCounts, scale).panelHeight;
}

const PublicPlayerSlot* findSlot(
    const ClientSessionController& session, PlayerSlot slot) {
    const auto& players = session.matchMetadata().players;
    const auto found = std::find_if(
        players.begin(),
        players.end(),
        [slot](const PublicPlayerSlot& player) { return player.slot == slot; });
    return found == players.end() ? nullptr : &*found;
}

const client::PublicPlayerProfile* findProfile(
    const ClientSessionController& session, PlayerId player) {
    const auto& profiles = session.profiles();
    const auto found = std::find_if(
        profiles.begin(),
        profiles.end(),
        [player](const client::PublicPlayerProfile& profile) {
            return profile.player == player;
        });
    return found == profiles.end() ? nullptr : &*found;
}

bool drawBrand(const DrawingContext& context, float x, float y) {
    const float scale = context.scale;
    const SDL_FRect mark{x, y + 7.0F * scale, 28.0F * scale, 32.0F * scale};
    if (!context.asset(SvgAssetId::BasiliskLogo, mark)) {
        drawPanel(
            context.renderer,
            mark,
            7.0F * scale,
            SDL_Color{23, 27, 29, SDL_ALPHA_OPAQUE},
            SDL_Color{114, 97, 61, SDL_ALPHA_OPAQUE},
            scale);
        setColor(context.renderer, ui::Theme::gold);
        SDL_RenderLine(
            context.renderer,
            mark.x + 7.0F * scale,
            mark.y + 23.0F * scale,
            mark.x + 21.0F * scale,
            mark.y + 9.0F * scale);
        SDL_RenderLine(
            context.renderer,
            mark.x + 8.0F * scale,
            mark.y + 10.0F * scale,
            mark.x + 20.0F * scale,
            mark.y + 22.0F * scale);
    }

    return context.label(
               "BASILISK",
               FontWeight::Bold,
               ui::Typography::brandTitle,
               ui::Theme::text,
               x + 39.0F * scale,
               y + 8.0F * scale) &&
        context.label(
               "PLAYER FIELD VIEW",
               FontWeight::Medium,
               ui::Typography::brandSubtitle,
               ui::Theme::muted,
               x + 39.0F * scale,
               y + 29.0F * scale);
}

bool drawPlayerCard(
    const DrawingContext& context,
    SDL_FRect card,
    std::string_view designation,
    std::string_view username,
    const client::CallingCardId* callingCardId,
    const client::EmblemId* emblemId,
    bool local,
    bool firstPlayer) {

    const float scale = context.scale;
    const SDL_Color fallbackFill = firstPlayer
        ? SDL_Color{30, 24, 24, SDL_ALPHA_OPAQUE}
        : SDL_Color{21, 29, 33, SDL_ALPHA_OPAQUE};
    const SDL_Color border = firstPlayer
        ? SDL_Color{105, 66, 61, SDL_ALPHA_OPAQUE}
        : SDL_Color{62, 98, 113, SDL_ALPHA_OPAQUE};
    const SDL_Color accent = firstPlayer
        ? SDL_Color{181, 104, 93, SDL_ALPHA_OPAQUE}
        : SDL_Color{93, 145, 166, SDL_ALPHA_OPAQUE};
    const std::optional<SvgAssetId> mappedCard = callingCardId == nullptr
        ? std::nullopt : callingCardAsset(*callingCardId);
    if (!mappedCard.has_value() ||
        !context.authoredAspectFitAsset(*mappedCard, card)) {
        drawPanel(context.renderer, card, 8.0F * scale,
            fallbackFill, border, scale);
    }

    constexpr float emblemGap = 8.0F;
    const SDL_FRect emblem{
        card.x - card.h - emblemGap * scale,
        card.y,
        card.h,
        card.h,
    };
    drawPanel(context.renderer, emblem, 8.0F * scale,
        ui::Theme::surface, border, scale);
    const std::optional<SvgAssetId> mappedEmblem =
        emblemId == nullptr ? std::nullopt : emblemAsset(*emblemId);
    const SDL_FRect emblemArt = inset(emblem, 4.0F * scale);
    const bool emblemDrawn = mappedEmblem.has_value() &&
        context.authoredAspectFitAsset(*mappedEmblem, emblemArt);
    if (!emblemDrawn) {
        setColor(context.renderer, accent);
        const SDL_FRect fallback = inset(emblem, 13.0F * scale);
        SDL_RenderRect(context.renderer, &fallback);
    }

    const SDL_FRect nameplate{
        card.x + 5.0F * scale,
        card.y + 5.0F * scale,
        std::min(card.w - 10.0F * scale, 148.0F * scale),
        card.h - 10.0F * scale,
    };
    drawPanel(context.renderer, nameplate, 7.0F * scale,
        ui::Theme::surface, ui::Theme::borderSoft, scale);

    if (local) {
        setColor(context.renderer, accent);
        SDL_RenderLine(
            context.renderer,
            card.x + 10.0F * scale,
            card.y + card.h - 2.0F * scale,
            card.x + card.w - 10.0F * scale,
            card.y + card.h - 2.0F * scale);
    }

    if (!context.label(
            designation,
            FontWeight::Bold,
            ui::Typography::playerDesignation,
            ui::Theme::muted,
            nameplate.x + 8.0F * scale,
            nameplate.y + 3.0F * scale) ||
        !context.label(
            username,
            FontWeight::SemiBold,
            ui::Typography::playerName,
            ui::Theme::text,
            nameplate.x + 8.0F * scale,
            nameplate.y + 17.0F * scale)) {
        return false;
    }

    if (local) {
        const SDL_FRect localBadge{
            card.x + card.w - 38.0F * scale,
            card.y + 15.0F * scale,
            29.0F * scale,
            16.0F * scale,
        };
        drawPanel(
            context.renderer,
            localBadge,
            8.0F * scale,
            SDL_Color{20, 19, 19, SDL_ALPHA_OPAQUE},
            border,
            scale);
        return context.centeredLabel(
            "YOU", FontWeight::Bold, ui::Typography::localBadge, accent, localBadge);
    }
    return true;
}

bool drawHeaderHud(
    const DrawingContext& context,
    const PlayerRoundSnapshot& snapshot,
    const ClientSessionController& session,
    float headerHeight) {

    const float scale = context.scale;
    const float top = (headerHeight - 46.0F * scale) * 0.5F;
    if (!drawBrand(context, 26.0F * scale, top)) return false;

    const float matchX = 220.0F * scale;
    constexpr float playerCardHeight = 46.0F;
    constexpr float playerCardGap = 8.0F;
    constexpr float playerCardWidth =
        playerCardHeight * (400.0F / 75.0F);
    float hudX = matchX;
    const auto& matchPlayers = session.matchMetadata().players;
    if (matchPlayers.size() == 1) {
        const PublicPlayerSlot& onlySlot = matchPlayers.front();
        const client::PublicPlayerProfile* profile =
            findProfile(session, onlySlot.player);
        const SDL_FRect playerCard{
            matchX + (playerCardHeight + playerCardGap) * scale,
            top,
            playerCardWidth * scale,
            playerCardHeight * scale,
        };
        const std::string_view designation = onlySlot.slot == PlayerSlot::P1
            ? "P1 \xC2\xB7 SOLO"
            : "P2 \xC2\xB7 SOLO";
        if (!drawPlayerCard(
                context,
                playerCard,
                designation,
                profile == nullptr ? "Player" : profile->username,
                profile == nullptr ? nullptr : &profile->callingCardId,
                profile == nullptr ? nullptr : &profile->emblemId,
                onlySlot.player == session.viewContext().localPlayer,
                onlySlot.slot == PlayerSlot::P1)) {
            return false;
        }
        hudX = playerCard.x + playerCard.w + 36.0F * scale;
    } else {
        const PublicPlayerSlot* p1Slot = findSlot(session, PlayerSlot::P1);
        const PublicPlayerSlot* p2Slot = findSlot(session, PlayerSlot::P2);
        const client::PublicPlayerProfile* p1 =
            p1Slot == nullptr ? nullptr : findProfile(session, p1Slot->player);
        const client::PublicPlayerProfile* p2 =
            p2Slot == nullptr ? nullptr : findProfile(session, p2Slot->player);
        const SDL_FRect p1Card{
            matchX + (playerCardHeight + playerCardGap) * scale,
            top,
            playerCardWidth * scale,
            playerCardHeight * scale};
        const SDL_FRect versus{
            p1Card.x + p1Card.w + 8.0F * scale,
            top + 9.0F * scale,
            28.0F * scale,
            28.0F * scale,
        };
        const SDL_FRect p2Card{
            versus.x + versus.w +
                (8.0F + playerCardHeight + playerCardGap) * scale,
            top,
            playerCardWidth * scale,
            playerCardHeight * scale,
        };
        if (!drawPlayerCard(
                context,
                p1Card,
                "P1",
                p1 == nullptr ? "Player One" : p1->username,
                p1 == nullptr ? nullptr : &p1->callingCardId,
                p1 == nullptr ? nullptr : &p1->emblemId,
                p1Slot != nullptr &&
                    p1Slot->player == session.viewContext().localPlayer,
                true) ||
            !context.centeredLabel(
                "VS", FontWeight::Bold, ui::Typography::versus, ui::Theme::muted, versus) ||
            !drawPlayerCard(
                context,
                p2Card,
                "P2",
                p2 == nullptr ? "Player Two" : p2->username,
                p2 == nullptr ? nullptr : &p2->callingCardId,
                p2 == nullptr ? nullptr : &p2->emblemId,
                p2Slot != nullptr &&
                    p2Slot->player == session.viewContext().localPlayer,
                false)) {
            return false;
        }

        setColor(context.renderer, ui::Theme::border);
        SDL_RenderLine(
            context.renderer,
            versus.x,
            versus.y + versus.h * 0.5F,
            versus.x + 6.0F * scale,
            versus.y + versus.h * 0.5F);
        SDL_RenderLine(
            context.renderer,
            versus.x + versus.w - 6.0F * scale,
            versus.y + versus.h * 0.5F,
            versus.x + versus.w,
            versus.y + versus.h * 0.5F);
        hudX = p2Card.x + p2Card.w + 24.0F * scale;
    }

    const SDL_FRect roundBadge{hudX, top + 3.0F * scale, 54.0F * scale, 40.0F * scale};
    drawPanel(
        context.renderer,
        roundBadge,
        20.0F * scale,
        ui::Theme::surface,
        SDL_Color{52, 62, 71, SDL_ALPHA_OPAQUE},
        scale);
    if (!context.centeredLabel(
            "ROUND", FontWeight::Bold, ui::Typography::hudLabel, ui::Theme::muted, SDL_FRect{
                roundBadge.x,
                roundBadge.y + 2.0F * scale,
                roundBadge.w,
                14.0F * scale}) ||
        !context.centeredLabel(
            std::to_string(snapshot.round),
            FontWeight::Bold,
            ui::Typography::roundValue,
            ui::Theme::text,
            SDL_FRect{
                roundBadge.x,
                roundBadge.y + 15.0F * scale,
                roundBadge.w,
                21.0F * scale})) {
        return false;
    }

    hudX += 72.0F * scale;
    const float healthWidth = 110.0F * scale;
    if (!context.label(
            "HP", FontWeight::Bold, ui::Typography::hudLabel, ui::Theme::muted, hudX, top + 5.0F * scale) ||
        !context.label(
            std::to_string(snapshot.health) + "/" + std::to_string(snapshot.maxHealth),
            FontWeight::SemiBold,
            ui::Typography::hudValue,
            ui::Theme::text,
            hudX + 62.0F * scale,
            top + 4.0F * scale)) {
        return false;
    }
    const SDL_FRect healthTrack{
        hudX,
        top + 25.0F * scale,
        healthWidth,
        7.0F * scale,
    };
    drawPanel(
        context.renderer,
        healthTrack,
        4.0F * scale,
        SDL_Color{26, 17, 18, SDL_ALPHA_OPAQUE},
        SDL_Color{52, 38, 39, SDL_ALPHA_OPAQUE},
        scale);
    const float healthRatio = snapshot.maxHealth <= 0
        ? 0.0F
        : std::clamp(
              static_cast<float>(snapshot.health) /
                  static_cast<float>(snapshot.maxHealth),
              0.0F,
              1.0F);
    fillRoundedRect(
        context.renderer,
        SDL_FRect{
            healthTrack.x + scale,
            healthTrack.y + scale,
            std::max(0.0F, (healthTrack.w - 2.0F * scale) * healthRatio),
            std::max(0.0F, healthTrack.h - 2.0F * scale)},
        3.0F * scale,
        SDL_Color{196, 91, 79, SDL_ALPHA_OPAQUE});

    hudX += 132.0F * scale;
    if (!context.label(
            "ARROWS", FontWeight::Bold, ui::Typography::hudLabel, ui::Theme::muted, hudX, top + 3.0F * scale)) {
        return false;
    }
    for (int index = 0; index < std::max(0, snapshot.maxArrows); ++index) {
        const float slotX = hudX + static_cast<float>(index) * 13.0F * scale;
        const SDL_FRect arrow{
            slotX,
            top + 19.0F * scale,
            10.0F * scale,
            20.0F * scale,
        };
        const float opacity = index < snapshot.arrows ? 1.0F : 0.22F;
        if (!context.asset(SvgAssetId::Arrow, arrow, opacity, ui::Theme::mutedBright)) {
            setColor(context.renderer, ui::Theme::mutedBright);
            SDL_RenderLine(
                context.renderer,
                slotX + 5.0F * scale,
                top + 20.0F * scale,
                slotX + 5.0F * scale,
                top + 38.0F * scale);
            SDL_RenderLine(
                context.renderer,
                slotX + 2.0F * scale,
                top + 23.0F * scale,
                slotX + 5.0F * scale,
                top + 20.0F * scale);
            SDL_RenderLine(
                context.renderer,
                slotX + 8.0F * scale,
                top + 23.0F * scale,
                slotX + 5.0F * scale,
                top + 20.0F * scale);
        }
    }

    hudX += 78.0F * scale;
    if (!context.label(
            "PACK", FontWeight::Bold, ui::Typography::hudLabel, ui::Theme::muted, hudX, top + 3.0F * scale)) {
        return false;
    }
    const SDL_FRect packPill{
        hudX,
        top + 18.0F * scale,
        76.0F * scale,
        28.0F * scale,
    };
    drawPanel(
        context.renderer,
        packPill,
        14.0F * scale,
        SDL_Color{16, 21, 25, SDL_ALPHA_OPAQUE},
        SDL_Color{52, 62, 71, SDL_ALPHA_OPAQUE},
        scale);
    const int occupied = static_cast<int>(snapshot.inventory.items.size());
    for (std::size_t index = 0; index < snapshot.inventory.capacity; ++index) {
        const SDL_FRect slot{
            packPill.x + (8.0F + static_cast<float>(index) * 23.0F) * scale,
            packPill.y + 6.0F * scale,
            16.0F * scale,
            16.0F * scale,
        };
        if (index < static_cast<std::size_t>(occupied)) {
            drawPanel(
                context.renderer,
                slot,
                5.0F * scale,
                ui::Theme::surfaceSoft,
                SDL_Color{90, 102, 112, SDL_ALPHA_OPAQUE},
                scale);
            const SDL_FRect art = inset(slot, 2.0F * scale);
            if (!context.asset(
                    itemAsset(snapshot.inventory.items[index]),
                    art,
                    1.0F,
                    ui::Theme::mutedBright)) {
                setColor(context.renderer, ui::Theme::mutedBright);
                SDL_RenderRect(context.renderer, &art);
            }
        } else {
            fillRoundedRect(
                context.renderer,
                SDL_FRect{
                    slot.x + 6.0F * scale,
                    slot.y + 6.0F * scale,
                    4.0F * scale,
                    4.0F * scale},
                2.0F * scale,
                SDL_Color{65, 76, 85, SDL_ALPHA_OPAQUE});
        }
    }
    return true;
}

bool drawMapHeader(
    const DrawingContext& context,
    const PlayerRoundSnapshot& snapshot,
    std::size_t totalCaves,
    float x,
    float y) {

    return context.label(
               "CURRENT LOCATION",
               FontWeight::Bold,
               ui::Typography::mapEyebrow,
               ui::Theme::gold,
               x,
               y) &&
        context.label(
               "Cave " + std::to_string(snapshot.currentCave),
               FontWeight::SemiBold,
               ui::Typography::mapTitle,
               ui::Theme::text,
               x,
               y + 15.0F * context.scale) &&
        context.label(
               std::to_string(snapshot.map.caves.size()) + " discovered \xC2\xB7 " +
                   std::to_string(totalCaves) + " total",
               FontWeight::Regular,
               ui::Typography::mapContext,
               ui::Theme::muted,
               x,
               y + 49.0F * context.scale);
}

bool drawSectionHeader(
    const DrawingContext& context,
    std::string_view title,
    std::string_view count,
    SDL_FRect panel) {

    if (!context.label(
            title,
            FontWeight::Bold,
            ui::Typography::sectionHeading,
            ui::Theme::text,
            panel.x + 14.0F * context.scale,
            panel.y + 12.0F * context.scale)) {
        return false;
    }
    const SDL_FRect countBadge{
        panel.x + panel.w - 42.0F * context.scale,
        panel.y + 8.0F * context.scale,
        29.0F * context.scale,
        22.0F * context.scale,
    };
    drawPanel(
        context.renderer,
        countBadge,
        10.0F * context.scale,
        SDL_Color{17, 23, 28, SDL_ALPHA_OPAQUE},
        ui::Theme::border,
        context.scale);
    return context.centeredLabel(
        count, FontWeight::Bold, ui::Typography::sectionCount, ui::Theme::muted, countBadge);
}

bool drawObjectiveCard(
    const DrawingContext& context,
    SDL_FRect panel,
    bool primary,
    const SecondaryObjectivePresentation* secondary) {

    const float scale = context.scale;
    const SDL_Color accent = primary ? ui::Theme::red : ui::Theme::gold;
    const SDL_Color border = primary
        ? ui::Theme::redBorder
        : SDL_Color{80, 68, 39, SDL_ALPHA_OPAQUE};
    const SDL_Color fill = primary
        ? SDL_Color{25, 20, 21, SDL_ALPHA_OPAQUE}
        : SDL_Color{24, 23, 18, SDL_ALPHA_OPAQUE};
    drawPanel(context.renderer, panel, 10.0F * scale, fill, border, scale);

    if (!context.label(
            primary ? "PRIMARY OBJECTIVE" : "SECONDARY OBJECTIVE",
            FontWeight::Bold,
            ui::Typography::objectiveEyebrow,
            accent,
            panel.x + 14.0F * scale,
            panel.y + 13.0F * scale) ||
        !context.label(
            primary ? "SLAY THE BASILISK" : secondary->title,
            FontWeight::Bold,
            ui::Typography::objectiveTitle,
            ui::Theme::text,
            panel.x + 14.0F * scale,
            panel.y + 29.0F * scale)) {
        return false;
    }

    const SDL_FRect badge{
        panel.x + panel.w - 84.0F * scale,
        panel.y + 12.0F * scale,
        69.0F * scale,
        24.0F * scale,
    };
    drawPanel(context.renderer, badge, 11.0F * scale, fill, border, scale);
    if (!context.centeredLabel(
            primary ? "ACTIVE" : secondary->status,
            FontWeight::Bold,
            ui::Typography::objectiveStatus,
            accent,
            badge)) {
        return false;
    }

    const SDL_FRect emblem{
        panel.x + 14.0F * scale,
        panel.y + 63.0F * scale,
        36.0F * scale,
        40.0F * scale,
    };
    drawPanel(context.renderer, emblem, 8.0F * scale, fill, border, scale);
    const SDL_FRect symbol = inset(emblem, 4.0F * scale);
    const bool objectiveIconDrawn = primary
        ? context.asset(SvgAssetId::ObjectiveBasilisk, symbol, 1.0F, accent)
        : context.aspectFitAsset(SvgAssetId::HuntersSigil, symbol, 1.0F, accent);
    if (!objectiveIconDrawn) {
        setColor(context.renderer, accent);
        const SDL_FRect fallback = inset(emblem, 9.0F * scale);
        SDL_RenderRect(context.renderer, &fallback);
        SDL_RenderLine(
            context.renderer,
            fallback.x,
            fallback.y,
            fallback.x + fallback.w,
            fallback.y + fallback.h);
    }

    if (primary) {
        if (!context.label(
                "Find the Basilisk and end its reign.",
                FontWeight::Regular,
                ui::Typography::objectiveBody,
                ui::Theme::mutedBright,
                panel.x + 61.0F * scale,
                panel.y + 65.0F * scale)) {
            return false;
        }
    } else {
        for (std::size_t index = 0; index < secondary->bodyLines.size(); ++index) {
            if (!context.label(
                    secondary->bodyLines[index],
                    FontWeight::Regular,
                    ui::Typography::objectiveBody,
                    ui::Theme::mutedBright,
                    panel.x + 61.0F * scale,
                    panel.y + (65.0F + static_cast<float>(index) * 15.0F) * scale)) {
                return false;
            }
        }
    }
    if (!primary) {
        if (!secondary->detail.has_value()) return true;
        const SDL_FRect extractionPanel{
            panel.x + 61.0F * scale,
            panel.y + 97.0F * scale,
            panel.w - 76.0F * scale,
            28.0F * scale,
        };
        drawPanel(
            context.renderer,
            extractionPanel,
            4.0F * scale,
            SDL_Color{35, 31, 19, SDL_ALPHA_OPAQUE},
            SDL_Color{76, 61, 30, SDL_ALPHA_OPAQUE},
            scale);
        return context.label(
            *secondary->detail,
            FontWeight::Medium,
            ui::Typography::objectiveState,
            SDL_Color{230, 216, 180, SDL_ALPHA_OPAQUE},
            extractionPanel.x + 9.0F * scale,
            extractionPanel.y + 6.0F * scale);
    }
    return true;
}

bool drawRoundReport(
    const DrawingContext& context,
    const PlayerRoundSnapshot& snapshot,
    SDL_FRect panel) {

    const float scale = context.scale;

    drawPanel(
        context.renderer,
        panel,
        10.0F * scale,
        ui::Theme::surface,
        ui::Theme::borderSoft,
        scale);

    if (!drawSectionHeader(
            context,
            "ROUND REPORT",
            std::to_string(snapshot.observations.size()),
            panel)) {
        return false;
    }

    const std::vector<std::string> report = roundReportText(snapshot);

    float rowY = panel.y + 40.0F * scale;

    for (std::size_t index = 0; index < report.size(); ++index) {
        const float rowWidth = panel.w - 26.0F * scale;
        const float textWidth = rowWidth - 25.0F * scale;

        const auto lines = wrapTextLines(
            context,
            report[index],
            FontWeight::Regular,
            ui::Typography::reportBody,
            textWidth);

        if (!lines.has_value()) {
            return false;
        }

        const std::size_t lineCount = lines->size();
        const float rowHeight =
            roundReportLayout(std::span{&lineCount, 1}, scale).rowHeights.front();

        const SDL_FRect row{
            panel.x + 13.0F * scale,
            rowY,
            rowWidth,
            rowHeight,
        };

        fillRoundedRect(
            context.renderer,
            row,
            5.0F * scale,
            ui::Theme::surfaceRaised);

        fillRoundedRect(
            context.renderer,
            SDL_FRect{
                row.x + 7.0F * scale,
                row.y + 10.0F * scale,
                4.0F * scale,
                4.0F * scale},
            2.0F * scale,
            ui::Theme::gold);

        for (std::size_t line = 0; line < lines->size(); ++line) {
            if (!context.label(
                    (*lines)[line],
                    FontWeight::Regular,
                    ui::Typography::reportBody,
                    ui::Theme::mutedBright,
                    row.x + 17.0F * scale,
                    row.y +
                        (5.0F + static_cast<float>(line) * 14.0F) * scale)) {
                return false;
            }
        }

        rowY += rowHeight + 4.0F * scale;
    }

    return true;
}

bool drawInventory(
    const DrawingContext& context,
    const PlayerRoundSnapshot& snapshot,
    InventoryPanelGeometry& geometry,
    SDL_FRect panel) {

    const float scale = context.scale;
    geometry = {};
    geometry.panel = PresentationRect{panel.x, panel.y, panel.w, panel.h};
    drawPanel(
        context.renderer,
        panel,
        10.0F * scale,
        ui::Theme::surface,
        ui::Theme::borderSoft,
        scale);
    if (!drawSectionHeader(
            context,
            "INVENTORY",
            std::to_string(snapshot.inventory.items.size()) + "/" +
                std::to_string(snapshot.inventory.capacity),
            panel)) {
        return false;
    }

    const float gap = 7.0F * scale;
    const float cardWidth = (panel.w - 33.0F * scale) * 0.5F;
    for (std::size_t index = 0; index < snapshot.inventory.capacity; ++index) {
        const float column = static_cast<float>(index % 2);
        const float row = static_cast<float>(index / 2);
        const SDL_FRect card{
            panel.x + 13.0F * scale + column * (cardWidth + gap),
            panel.y + 40.0F * scale + row * 48.0F * scale,
            cardWidth,
            41.0F * scale,
        };
        const bool occupied = index < snapshot.inventory.items.size();
        if (occupied) {
            geometry.items.push_back(InventoryItemGeometry{
                snapshot.inventory.items[index],
                PresentationRect{card.x, card.y, card.w, card.h},
            });
        }
        drawPanel(
            context.renderer,
            card,
            6.0F * scale,
            occupied ? ui::Theme::surfaceRaised : ui::Theme::surface,
            occupied ? ui::Theme::border : ui::Theme::borderSoft,
            scale);
        const SDL_FRect icon{
            card.x + 6.0F * scale,
            card.y + 7.0F * scale,
            27.0F * scale,
            27.0F * scale,
        };
        drawPanel(
            context.renderer,
            icon,
            5.0F * scale,
            ui::Theme::surfaceSoft,
            occupied ? SDL_Color{72, 83, 93, SDL_ALPHA_OPAQUE} : ui::Theme::borderSoft,
            scale);
        if (occupied) {
            const SDL_FRect art = inset(icon, 2.0F * scale);
            if (!context.asset(
                    itemAsset(snapshot.inventory.items[index]),
                    art,
                    1.0F,
                    ui::Theme::mutedBright)) {
                setColor(context.renderer, ui::Theme::muted);
                SDL_RenderLine(
                    context.renderer,
                    art.x,
                    art.y + art.h,
                    art.x + art.w,
                    art.y);
            }
        }
        if (!context.label(
                occupied ? presentation::itemName(snapshot.inventory.items[index]) : "Empty",
                occupied ? FontWeight::Medium : FontWeight::Regular,
                ui::Typography::inventoryItem,
                occupied ? ui::Theme::text : ui::Theme::muted,
                card.x + 40.0F * scale,
                card.y + 13.0F * scale)) {
            return false;
        }
    }
    return true;
}

bool drawAvailableActions(
    const DrawingContext& context,
    const PlayerRoundSnapshot& snapshot,
    const ActionSelectionState& selection,
    std::optional<std::size_t> hoveredActionIndex,
    ActionPanelGeometry& geometry,
    const ClientSessionController& session,
    SDL_FRect panel) {

    const float scale = context.scale;
    geometry = {};
    geometry.panel = PresentationRect{panel.x, panel.y, panel.w, panel.h};
    drawPanel(
        context.renderer,
        panel,
        10.0F * scale,
        ui::Theme::surface,
        ui::Theme::borderSoft,
        scale);
    if (!drawSectionHeader(
            context,
            "AVAILABLE ACTIONS",
            std::to_string(snapshot.availableActions.size()),
            panel)) {
        return false;
    }

    const float rowStride = 37.0F * scale;
    const SDL_FRect lockButton{
        panel.x + 13.0F * scale,
        panel.y + panel.h - 39.0F * scale,
        panel.w - 26.0F * scale,
        28.0F * scale,
    };
    const float waitingSpace = selection.waitingForOtherHunter() ? 13.0F * scale : 0.0F;
    const SDL_FRect viewport{
        panel.x + 13.0F * scale,
        panel.y + 40.0F * scale,
        panel.w - 26.0F * scale,
        std::max(0.0F, lockButton.y - panel.y - 47.0F * scale - waitingSpace),
    };
    geometry.viewport = PresentationRect{
        viewport.x, viewport.y, viewport.w, viewport.h};
    geometry.lockButton = PresentationRect{
        lockButton.x, lockButton.y, lockButton.w, lockButton.h};
    geometry.visibleCapacity = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::floor(viewport.h / rowStride)));

    const std::vector<PresentedAction> rows =
        presentAvailableActions(snapshot.availableActions);
    const std::size_t first = std::min(selection.scrollOffset(), rows.size());
    const std::size_t last = std::min(rows.size(), first + geometry.visibleCapacity);

    if (rows.size() > geometry.visibleCapacity) {
        const std::string range = std::to_string(first + 1) + "-" +
            std::to_string(last) + " OF " + std::to_string(rows.size());
        if (!context.label(
                range,
                FontWeight::Medium,
                ui::Typography::actionDetail,
                ui::Theme::muted,
                panel.x + panel.w - 122.0F * scale,
                panel.y + 14.0F * scale)) {
            return false;
        }
    }

    const SDL_Rect clip{
        static_cast<int>(viewport.x),
        static_cast<int>(viewport.y),
        std::max(0, static_cast<int>(viewport.w)),
        std::max(0, static_cast<int>(viewport.h)),
    };
    SDL_SetRenderClipRect(context.renderer, &clip);
    for (std::size_t index = first; index < last; ++index) {
        const PresentedAction& action = rows[index];
        const SDL_FRect row{
            viewport.x,
            viewport.y + static_cast<float>(index - first) * rowStride,
            viewport.w,
            33.0F * scale,
        };
        geometry.rows.push_back(ActionRowGeometry{
            index,
            PresentationRect{row.x, row.y, row.w, row.h},
        });
        const bool selected = selection.selectedIndex() == index;
        const bool hovered = hoveredActionIndex == index;
        const SDL_Color rowBorder = selected
            ? ui::Theme::blue
            : (!selection.locked() && hovered)
                ? ui::Theme::text
                : SDL_Color{37, 46, 54, SDL_ALPHA_OPAQUE};
        drawPanel(
            context.renderer,
            row,
            6.0F * scale,
            selected ? SDL_Color{19, 34, 47, SDL_ALPHA_OPAQUE} : ui::Theme::surfaceRaised,
            rowBorder,
            scale);
        const SDL_FRect key{
            row.x + 5.0F * scale,
            row.y + 5.0F * scale,
            23.0F * scale,
            23.0F * scale,
        };
        drawPanel(
            context.renderer,
            key,
            4.0F * scale,
            ui::Theme::sidebar,
            SDL_Color{59, 70, 80, SDL_ALPHA_OPAQUE},
            scale);
        const std::string keyText = index < 9 ? std::to_string(index + 1) : "-";
        if (!context.centeredLabel(
                keyText,
                FontWeight::Bold,
                ui::Typography::actionKey,
                selected ? ui::Theme::blue : ui::Theme::text,
                key) ||
            !context.label(
                action.title,
                FontWeight::SemiBold,
                ui::Typography::actionTitle,
                ui::Theme::text,
                row.x + 31.0F * scale,
                row.y + 3.0F * scale) ||
            !context.label(
                action.detail,
                FontWeight::Regular,
                ui::Typography::actionDetail,
                ui::Theme::muted,
                row.x + 31.0F * scale,
                row.y + 18.0F * scale)) {
            return false;
        }
    }
    SDL_SetRenderClipRect(context.renderer, nullptr);

    const bool canLock = selection.canLock(session.viewContext());
    const bool locked = selection.locked();
    const SDL_Color buttonFill = locked
        ? SDL_Color{35, 31, 19, SDL_ALPHA_OPAQUE}
        : canLock ? SDL_Color{20, 38, 52, SDL_ALPHA_OPAQUE}
                  : SDL_Color{18, 23, 28, SDL_ALPHA_OPAQUE};
    const SDL_Color buttonBorder = locked
        ? SDL_Color{76, 61, 30, SDL_ALPHA_OPAQUE}
        : canLock ? ui::Theme::blue : ui::Theme::borderSoft;
    const SDL_Color buttonText = locked
        ? ui::Theme::gold
        : canLock ? ui::Theme::text : ui::Theme::muted;
    drawPanel(
        context.renderer,
        lockButton,
        6.0F * scale,
        buttonFill,
        buttonBorder,
        scale);
    const std::string_view buttonLabel = locked
        ? "LOCKED"
        : session.canSubmitActions() ? "LOCK ACTION" : "VIEW ONLY";
    if (selection.waitingForOtherHunter() && !context.centeredLabel(
            "WAITING FOR OTHER HUNTER",
            FontWeight::Medium,
            ui::Typography::actionDetail,
            ui::Theme::muted,
            SDL_FRect{
                lockButton.x,
                lockButton.y - 14.0F * scale,
                lockButton.w,
                11.0F * scale})) {
        return false;
    }
    return context.centeredLabel(
        buttonLabel,
        FontWeight::Bold,
        ui::Typography::actionTitle,
        buttonText,
        lockButton);
}

bool drawSidebar(
    const DrawingContext& context,
    const PlayerRoundSnapshot& snapshot,
    const ActionSelectionState& actionSelection,
    std::optional<std::size_t> hoveredActionIndex,
    ActionPanelGeometry& actionGeometry,
    InventoryPanelGeometry& inventoryGeometry,
    const ClientSessionController& session,
    SDL_FRect sidebar) {

    const float scale = context.scale;
    const float x = sidebar.x + 18.0F * scale;
    const float width = sidebar.w - 36.0F * scale;
    float y = sidebar.y + 18.0F * scale;

    const SDL_FRect primary{x, y, width, 126.0F * scale};
    if (!drawObjectiveCard(context, primary, true, nullptr)) return false;
    y += 136.0F * scale;

    const auto secondaryPresentation = secondaryObjectivePresentation(snapshot);
    if (secondaryPresentation.has_value()) {
        const SDL_FRect secondary{x, y, width, 134.0F * scale};
        if (!drawObjectiveCard(
                context, secondary, false, &*secondaryPresentation)) {
            return false;
        }
        y += 146.0F * scale;
    }

    const auto requiredReportHeight =
        roundReportHeight(context, snapshot, width);

    if (!requiredReportHeight.has_value()) {
        return false;
    }

    const SDL_FRect report{
        x,
        y,
        width,
        *requiredReportHeight
    };

    if (!drawRoundReport(context, snapshot, report)) {
        return false;
    }

    y += report.h + 12.0F * scale;

    const SDL_FRect inventory{x, y, width, 134.0F * scale};
    if (!drawInventory(context, snapshot, inventoryGeometry, inventory)) {
        return false;
    }
    y += 146.0F * scale;

    const float remainingHeight = std::max(
        0.0F,
        sidebar.y + sidebar.h - 18.0F * scale - y);
    const float waitingSpace =
        actionSelection.waitingForOtherHunter() ? 13.0F * scale : 0.0F;
    const float rowsHeight = 37.0F * scale *
        static_cast<float>(snapshot.availableActions.size());
    const float desiredHeight = std::max(
        228.0F * scale,
        86.0F * scale + waitingSpace + rowsHeight);
    const SDL_FRect actions{
        x,
        y,
        width,
        std::min(desiredHeight, remainingHeight),
    };
    return drawAvailableActions(
        context,
        snapshot,
        actionSelection,
        hoveredActionIndex,
        actionGeometry,
        session,
        actions);
}

bool drawMapActionMenu(
    const DrawingContext& context,
    const PlayerRoundSnapshot& snapshot,
    const MapActionMenuState& menu,
    MapActionMenuGeometry& geometry,
    PresentationRect mapBounds) {

    geometry = {};
    if (!menu.isOpen() || !menu.target().has_value()) return true;

    const float scale = context.scale;
    const float width = 238.0F * scale;
    const float headerHeight = 35.0F * scale;
    const float rowStride = 39.0F * scale;
    const float height = headerHeight +
        rowStride * static_cast<float>(menu.choices().size()) + 9.0F * scale;
    const float margin = 9.0F * scale;
    const float anchorGap = 22.0F * scale;

    float x = static_cast<float>(menu.anchorX()) + anchorGap;
    if (x + width > static_cast<float>(mapBounds.x + mapBounds.width) - margin) {
        x = static_cast<float>(menu.anchorX()) - width - anchorGap;
    }
    float y = static_cast<float>(menu.anchorY()) - height * 0.5F;
    x = std::clamp(
        x,
        static_cast<float>(mapBounds.x) + margin,
        std::max(
            static_cast<float>(mapBounds.x) + margin,
            static_cast<float>(mapBounds.x + mapBounds.width) - width - margin));
    y = std::clamp(
        y,
        static_cast<float>(mapBounds.y) + margin,
        std::max(
            static_cast<float>(mapBounds.y) + margin,
            static_cast<float>(mapBounds.y + mapBounds.height) - height - margin));

    const SDL_FRect panel{x, y, width, height};
    geometry.panel = PresentationRect{panel.x, panel.y, panel.w, panel.h};
    drawPanel(
        context.renderer,
        panel,
        9.0F * scale,
        SDL_Color{13, 18, 23, 246},
        SDL_Color{55, 69, 81, SDL_ALPHA_OPAQUE},
        scale);

    const SpatialActionTarget target = *menu.target();
    const std::string heading = target.kind == SpatialActionTargetKind::Cave
        ? "CAVE " + std::to_string(target.cave)
        : "UNKNOWN EXIT";
    if (!context.label(
            heading,
            FontWeight::Bold,
            ui::Typography::actionDetail,
            ui::Theme::mutedBright,
            panel.x + 11.0F * scale,
            panel.y + 10.0F * scale)) {
        return false;
    }

    for (std::size_t rowIndex = 0;
         rowIndex < menu.choices().size();
         ++rowIndex) {
        const MapActionMenuChoice choice = menu.choices()[rowIndex];
        if (choice.kind == MapActionMenuChoiceKind::GameplayAction &&
            choice.actionIndex >= snapshot.availableActions.size()) {
            continue;
        }
        const SDL_FRect row{
            panel.x + 8.0F * scale,
            panel.y + headerHeight + static_cast<float>(rowIndex) * rowStride,
            panel.w - 16.0F * scale,
            34.0F * scale,
        };
        geometry.rows.push_back(MapActionMenuGeometry::Row{
            choice,
            PresentationRect{row.x, row.y, row.w, row.h},
        });
        const bool hovered = menu.hoveredChoice() == choice;
        drawPanel(
            context.renderer,
            row,
            6.0F * scale,
            hovered ? SDL_Color{20, 36, 49, SDL_ALPHA_OPAQUE}
                    : ui::Theme::surfaceRaised,
            hovered ? ui::Theme::blue : ui::Theme::border,
            scale);
        const std::string label =
            mapActionMenuChoiceTitle(choice, snapshot.availableActions);
        if (!context.label(
                label,
                FontWeight::SemiBold,
                ui::Typography::actionTitle,
                hovered ? ui::Theme::blue : ui::Theme::text,
                row.x + 10.0F * scale,
                row.y + 10.0F * scale)) {
            return false;
        }
    }
    return true;
}

bool drawLifecycleModal(
    const DrawingContext& context,
    const PlayerRoundSnapshot& snapshot,
    const ClientSessionController& session,
    LifecycleModalGeometry& geometry,
    SDL_FRect output) {

    geometry = {};
    const auto presentation = lifecycleModalPresentation(
        snapshot, session.viewContext(), session.profiles());
    if (!presentation.has_value()) return true;

    geometry.blocking = true;
    setColor(context.renderer, SDL_Color{4, 6, 8, 176});
    SDL_RenderFillRect(context.renderer, &output);
    const float scale = context.scale;
    const float panelHeight = presentation->offersWatch ? 218.0F : 180.0F;
    const SDL_FRect panel{
        output.x + (output.w - 430.0F * scale) * 0.5F,
        output.y + (output.h - panelHeight * scale) * 0.5F,
        430.0F * scale,
        panelHeight * scale,
    };
    geometry.panel = PresentationRect{panel.x, panel.y, panel.w, panel.h};
    drawPanel(
        context.renderer,
        panel,
        12.0F * scale,
        ui::Theme::surface,
        presentation->kind == LifecycleModalKind::HuntEnded
            ? ui::Theme::gold
            : ui::Theme::red,
        scale);
    const SDL_Color titleColor = presentation->kind == LifecycleModalKind::HuntEnded
        ? ui::Theme::gold
        : ui::Theme::red;
    if (!context.centeredLabel(
            presentation->title,
            FontWeight::Bold,
            24.0F,
            titleColor,
            SDL_FRect{
                panel.x + 20.0F * scale,
                panel.y + 24.0F * scale,
                panel.w - 40.0F * scale,
                34.0F * scale}) ||
        !context.centeredLabel(
            presentation->detail,
            FontWeight::Regular,
            ui::Typography::objectiveBody,
            ui::Theme::mutedBright,
            SDL_FRect{
                panel.x + 20.0F * scale,
                panel.y + 68.0F * scale,
                panel.w - 40.0F * scale,
                34.0F * scale})) {
        return false;
    }

    float buttonY = panel.y + 116.0F * scale;
    if (presentation->offersWatch) {
        const SDL_FRect watch{
            panel.x + 42.0F * scale,
            buttonY,
            panel.w - 84.0F * scale,
            36.0F * scale,
        };
        geometry.watchButton = PresentationRect{watch.x, watch.y, watch.w, watch.h};
        drawPanel(
            context.renderer,
            watch,
            7.0F * scale,
            SDL_Color{20, 38, 52, SDL_ALPHA_OPAQUE},
            ui::Theme::blue,
            scale);
        if (!context.centeredLabel(
                "WATCH REMAINING HUNTER",
                FontWeight::Bold,
                ui::Typography::actionTitle,
                ui::Theme::text,
                watch)) {
            return false;
        }
        buttonY += 45.0F * scale;
    }

    const SDL_FRect quit{
        panel.x + 42.0F * scale,
        buttonY,
        panel.w - 84.0F * scale,
        36.0F * scale,
    };
    geometry.quitButton = PresentationRect{quit.x, quit.y, quit.w, quit.h};
    drawPanel(
        context.renderer,
        quit,
        7.0F * scale,
        ui::Theme::surfaceRaised,
        ui::Theme::border,
        scale);
    return context.centeredLabel(
        "QUIT GAME",
        FontWeight::Bold,
        ui::Typography::actionTitle,
        ui::Theme::mutedBright,
        quit);
}

} // namespace

namespace {

bool contains(PresentationRect rectangle, PresentationPoint point) noexcept {
    return point.x >= rectangle.x && point.x <= rectangle.x + rectangle.width &&
        point.y >= rectangle.y && point.y <= rectangle.y + rectangle.height;
}

} // namespace

std::optional<std::size_t> hitTestActionRow(
    const ActionPanelGeometry& geometry,
    PresentationPoint point) noexcept {

    for (const ActionRowGeometry& row : geometry.rows) {
        if (contains(row.bounds, point)) return row.actionIndex;
    }
    return std::nullopt;
}

bool hitTestActionLockButton(
    const ActionPanelGeometry& geometry,
    PresentationPoint point) noexcept {

    return contains(geometry.lockButton, point);
}

bool hitTestActionPanel(
    const ActionPanelGeometry& geometry,
    PresentationPoint point) noexcept {

    return contains(geometry.panel, point);
}

std::optional<ItemType> hitTestInventoryItem(
    const InventoryPanelGeometry& geometry,
    PresentationPoint point) noexcept {

    for (const InventoryItemGeometry& item : geometry.items) {
        if (contains(item.bounds, point)) return item.item;
    }
    return std::nullopt;
}

std::optional<MapActionMenuChoice> hitTestMapActionRow(
    const MapActionMenuGeometry& geometry,
    PresentationPoint point) noexcept {

    for (const MapActionMenuGeometry::Row& row : geometry.rows) {
        if (contains(row.bounds, point)) return row.choice;
    }
    return std::nullopt;
}

bool hitTestMapActionMenu(
    const MapActionMenuGeometry& geometry,
    PresentationPoint point) noexcept {

    return contains(geometry.panel, point);
}

bool hitTestLifecycleWatch(
    const LifecycleModalGeometry& geometry,
    PresentationPoint point) noexcept {

    return geometry.blocking && geometry.watchButton.has_value() &&
        contains(*geometry.watchButton, point);
}

bool hitTestLifecycleQuit(
    const LifecycleModalGeometry& geometry,
    PresentationPoint point) noexcept {

    return geometry.blocking && contains(geometry.quitButton, point);
}

bool renderScreenShell(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    SvgTextureManager& svgTextures,
    const ClientSessionController& session,
    PlayerMapLayout& mapLayout,
    MapPresentationState& mapPresentation,
    MapPresentationGeometry& mapGeometry,
    const ActionSelectionState& actionSelection,
    std::optional<std::size_t> hoveredActionIndex,
    ActionPanelGeometry& actionGeometry,
    InventoryPanelGeometry& inventoryGeometry,
    const MapActionMenuState& mapActionMenu,
    MapActionMenuGeometry& mapActionMenuGeometry,
    LifecycleModalGeometry& lifecycleModalGeometry,
#if defined(BASILISK_GAME_DEBUG)
    const debug::DebugMapTruth* debugMapTruth,
    const debug::DebugGameplayTruth* debugGameplayTruth,
    bool revealDebugMap,
    bool revealDebugGameplay,
    bool debugInventoryMenuOpen,
#endif
    int outputWidth,
    int outputHeight,
    std::string& error) {

    error.clear();
    if (renderer == nullptr || outputWidth <= 0 || outputHeight <= 0) {
        error = "Screen shell requires a valid renderer and output size";
        return false;
    }

    const float scale = std::min(
        static_cast<float>(outputWidth) / kReferenceWidth,
        static_cast<float>(outputHeight) / kReferenceHeight);
    if (scale <= 0.0F) {
        error = "Screen shell scale must be positive";
        return false;
    }
    const DrawingContext context{renderer, &textRenderer, &svgTextures, scale, &error};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    setColor(renderer, ui::Theme::background);
    const SDL_FRect output{
        0.0F,
        0.0F,
        static_cast<float>(outputWidth),
        static_cast<float>(outputHeight),
    };
    SDL_RenderFillRect(renderer, &output);

    const PlayerRoundSnapshot* displayedSnapshot = session.displayedSnapshot();
    if (displayedSnapshot == nullptr) {
        actionGeometry = {};
        mapActionMenuGeometry = {};
        lifecycleModalGeometry = {};
        return context.centeredLabel(
            "WAITING FOR PLAYER SNAPSHOT",
            FontWeight::SemiBold,
            ui::Typography::sectionHeading,
            ui::Theme::mutedBright,
            output);
    }
    const PlayerRoundSnapshot& snapshot = *displayedSnapshot;

    const float headerHeight = 68.0F * scale;
    const SDL_FRect header{0.0F, 0.0F, output.w, headerHeight};
    setColor(renderer, ui::Theme::header);
    SDL_RenderFillRect(renderer, &header);
    setColor(renderer, ui::Theme::border);
    SDL_RenderLine(renderer, 0.0F, headerHeight - 1.0F, output.w, headerHeight - 1.0F);

    const float sidebarWidth = std::min(390.0F * scale, output.w * 0.42F);
    const SDL_FRect sidebar{
        output.w - sidebarWidth,
        headerHeight,
        sidebarWidth,
        output.h - headerHeight,
    };
    setColor(renderer, ui::Theme::sidebar);
    SDL_RenderFillRect(renderer, &sidebar);
    setColor(renderer, ui::Theme::border);
    SDL_RenderLine(renderer, sidebar.x, sidebar.y, sidebar.x, output.h);

    const float mapPadding = 22.0F * scale;
    if (!drawMapHeader(
            context,
            snapshot,
            session.matchMetadata().totalCaves,
            mapPadding,
            headerHeight + 22.0F * scale)) {
        return false;
    }

    const SDL_FRect mapFrame{
        mapPadding,
        headerHeight + 94.0F * scale,
        std::max(0.0F, sidebar.x - mapPadding * 2.0F),
        std::max(0.0F, output.h - headerHeight - 114.0F * scale),
    };
    drawPanel(
        renderer,
        mapFrame,
        12.0F * scale,
        ui::Theme::mapSurface,
        ui::Theme::border,
        scale);

    const SDL_FRect mapContent = inset(mapFrame, 3.0F * scale);
    setColor(renderer, SDL_Color{25, 31, 37, SDL_ALPHA_OPAQUE});
    const float grid = 42.0F * scale;
    for (float x = mapContent.x + grid; x < mapContent.x + mapContent.w; x += grid) {
        SDL_RenderLine(renderer, x, mapContent.y, x, mapContent.y + mapContent.h);
    }
    for (float y = mapContent.y + grid; y < mapContent.y + mapContent.h; y += grid) {
        SDL_RenderLine(renderer, mapContent.x, y, mapContent.x + mapContent.w, y);
    }

    if (const PlayerFixedMapGeometry* fixed = session.displayedMapGeometry()) {
        mapLayout.updateFixed(*fixed);
    } else {
        mapLayout.update(snapshot.map);
    }
    const SDL_Rect clip{
        static_cast<int>(mapContent.x),
        static_cast<int>(mapContent.y),
        std::max(0, static_cast<int>(mapContent.w)),
        std::max(0, static_cast<int>(mapContent.h)),
    };
    SDL_SetRenderClipRect(renderer, &clip);
    const SDL_FRect mapBounds = inset(mapContent, 18.0F * scale);
    mapGeometry = buildMapPresentationGeometry(
        snapshot.map,
        mapLayout,
        snapshot.temporarilyRevealedPitCaves,
        PresentationRect{
            mapBounds.x,
            mapBounds.y,
            mapBounds.w,
            mapBounds.h},
        40.0F * scale,
        scale);
    refreshSelectedRoute(mapPresentation, snapshot.map);
    if (!renderPlayerKnownMap(
            renderer,
            textRenderer,
            snapshot,
            mapLayout,
            mapGeometry,
            mapPresentation,
            error)) {
        SDL_SetRenderClipRect(renderer, nullptr);
        return false;
    }
#if defined(BASILISK_GAME_DEBUG)
    if (revealDebugMap && debugMapTruth != nullptr &&
        !debug::renderRevealedPhysicalMap(
            renderer,
            textRenderer,
            *debugMapTruth,
            snapshot,
            mapGeometry,
            error)) {
        SDL_SetRenderClipRect(renderer, nullptr);
        return false;
    }
    if (revealDebugGameplay && debugMapTruth != nullptr &&
        debugGameplayTruth != nullptr &&
        !debug::renderGameplayTruth(
            renderer,
            textRenderer,
            *debugMapTruth,
            *debugGameplayTruth,
            mapGeometry,
            error)) {
        SDL_SetRenderClipRect(renderer, nullptr);
        return false;
    }
    if (debugMapTruth != nullptr && debugGameplayTruth != nullptr &&
        !debug::renderDebugStatusLegend(
            textRenderer,
            mapGeometry,
            revealDebugMap,
            revealDebugGameplay,
            debugInventoryMenuOpen,
            debugGameplayTruth->basiliskBehavior,
            error)) {
        SDL_SetRenderClipRect(renderer, nullptr);
        return false;
    }
#endif
    SDL_SetRenderClipRect(renderer, nullptr);

    if (!drawHeaderHud(context, snapshot, session, headerHeight)) return false;
    if (!drawSidebar(
        context,
        snapshot,
        actionSelection,
        hoveredActionIndex,
        actionGeometry,
        inventoryGeometry,
        session,
        sidebar)) {
        return false;
    }
    if (rivalReconnectWaiting(snapshot)) {
        const SDL_FRect status{
            mapFrame.x + (mapFrame.w - 330.0F * scale) * 0.5F,
            mapFrame.y + 12.0F * scale,
            330.0F * scale,
            28.0F * scale,
        };
        drawPanel(
            renderer,
            status,
            14.0F * scale,
            SDL_Color{28, 25, 18, 238},
            SDL_Color{105, 85, 42, SDL_ALPHA_OPAQUE},
            scale);
        if (!context.centeredLabel(
                "Opponent disconnected \xE2\x80\x94 waiting for reconnect...",
                FontWeight::Medium,
                ui::Typography::actionDetail,
                ui::Theme::gold,
                status)) {
            return false;
        }
    }
    if (!drawMapActionMenu(
            context,
            snapshot,
            mapActionMenu,
            mapActionMenuGeometry,
            mapGeometry.transform.bounds)) {
        return false;
    }
    return drawLifecycleModal(
        context,
        snapshot,
        session,
        lifecycleModalGeometry,
        output);
}

} // namespace basilisk::game
