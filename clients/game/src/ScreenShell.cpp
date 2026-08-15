#include "ScreenShell.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include "MapRenderer.hpp"
#include "UITheme.hpp"
#include "basilisk/client/Presentation.hpp"

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
};

const PublicPlayerSlot* findSlot(const ScreenShellData& data, PlayerSlot slot) {
    const auto found = std::find_if(
        data.matchMetadata.players.begin(),
        data.matchMetadata.players.end(),
        [slot](const PublicPlayerSlot& player) { return player.slot == slot; });
    return found == data.matchMetadata.players.end() ? nullptr : &*found;
}

const client::PublicPlayerProfile* findProfile(
    const ScreenShellData& data, PlayerId player) {
    const auto found = std::find_if(
        data.profiles.begin(),
        data.profiles.end(),
        [player](const client::PublicPlayerProfile& profile) {
            return profile.player == player;
        });
    return found == data.profiles.end() ? nullptr : &*found;
}

bool drawBrand(const DrawingContext& context, float x, float y) {
    const float scale = context.scale;
    const SDL_FRect mark{x, y + 7.0F * scale, 28.0F * scale, 32.0F * scale};
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
    std::string_view displayName,
    bool local,
    bool firstPlayer) {

    const float scale = context.scale;
    const SDL_Color fill = firstPlayer
        ? SDL_Color{30, 24, 24, SDL_ALPHA_OPAQUE}
        : SDL_Color{21, 29, 33, SDL_ALPHA_OPAQUE};
    const SDL_Color border = firstPlayer
        ? SDL_Color{105, 66, 61, SDL_ALPHA_OPAQUE}
        : SDL_Color{62, 98, 113, SDL_ALPHA_OPAQUE};
    const SDL_Color accent = firstPlayer
        ? SDL_Color{181, 104, 93, SDL_ALPHA_OPAQUE}
        : SDL_Color{93, 145, 166, SDL_ALPHA_OPAQUE};
    drawPanel(context.renderer, card, 10.0F * scale, fill, border, scale);

    if (local) {
        setColor(context.renderer, accent);
        SDL_RenderLine(
            context.renderer,
            card.x + 10.0F * scale,
            card.y + card.h - 2.0F * scale,
            card.x + card.w - 10.0F * scale,
            card.y + card.h - 2.0F * scale);
    }

    const SDL_FRect emblem{
        card.x + 7.0F * scale,
        card.y + 7.0F * scale,
        31.0F * scale,
        31.0F * scale,
    };
    drawPanel(
        context.renderer,
        emblem,
        7.0F * scale,
        ui::Theme::surface,
        accent,
        scale);
    setColor(context.renderer, accent);
    if (firstPlayer) {
        const SDL_FPoint diamond[] = {
            {emblem.x + emblem.w * 0.5F, emblem.y + 6.0F * scale},
            {emblem.x + emblem.w - 6.0F * scale, emblem.y + emblem.h * 0.5F},
            {emblem.x + emblem.w * 0.5F, emblem.y + emblem.h - 6.0F * scale},
            {emblem.x + 6.0F * scale, emblem.y + emblem.h * 0.5F},
            {emblem.x + emblem.w * 0.5F, emblem.y + 6.0F * scale},
        };
        SDL_RenderLines(context.renderer, diamond, 5);
    } else {
        const SDL_FRect ward = inset(emblem, 8.0F * scale);
        SDL_RenderRect(context.renderer, &ward);
        SDL_RenderLine(
            context.renderer,
            emblem.x + emblem.w * 0.5F,
            ward.y,
            emblem.x + emblem.w * 0.5F,
            ward.y + ward.h);
        SDL_RenderLine(
            context.renderer,
            ward.x,
            emblem.y + emblem.h * 0.5F,
            ward.x + ward.w,
            emblem.y + emblem.h * 0.5F);
    }

    if (!context.label(
            designation,
            FontWeight::Bold,
            ui::Typography::playerDesignation,
            ui::Theme::muted,
            card.x + 47.0F * scale,
            card.y + 7.0F * scale) ||
        !context.label(
            displayName,
            FontWeight::SemiBold,
            ui::Typography::playerName,
            ui::Theme::text,
            card.x + 47.0F * scale,
            card.y + 21.0F * scale)) {
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
    const ScreenShellData& data,
    float headerHeight) {

    const float scale = context.scale;
    const float top = (headerHeight - 46.0F * scale) * 0.5F;
    if (!drawBrand(context, 26.0F * scale, top)) return false;

    const PublicPlayerSlot* p1Slot = findSlot(data, PlayerSlot::P1);
    const PublicPlayerSlot* p2Slot = findSlot(data, PlayerSlot::P2);
    const client::PublicPlayerProfile* p1 =
        p1Slot == nullptr ? nullptr : findProfile(data, p1Slot->player);
    const client::PublicPlayerProfile* p2 =
        p2Slot == nullptr ? nullptr : findProfile(data, p2Slot->player);

    const float matchX = 220.0F * scale;
    const SDL_FRect p1Card{matchX, top, 203.0F * scale, 46.0F * scale};
    const SDL_FRect versus{
        p1Card.x + p1Card.w + 8.0F * scale,
        top + 9.0F * scale,
        28.0F * scale,
        28.0F * scale,
    };
    const SDL_FRect p2Card{
        versus.x + versus.w + 8.0F * scale,
        top,
        203.0F * scale,
        46.0F * scale,
    };
    if (!drawPlayerCard(
            context,
            p1Card,
            "P1",
            p1 == nullptr ? "Player One" : p1->displayName,
            p1Slot != nullptr && p1Slot->player == data.localPlayer,
            true) ||
        !context.centeredLabel(
            "VS", FontWeight::Bold, ui::Typography::versus, ui::Theme::muted, versus) ||
        !drawPlayerCard(
            context,
            p2Card,
            "P2",
            p2 == nullptr ? "Player Two" : p2->displayName,
            p2Slot != nullptr && p2Slot->player == data.localPlayer,
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

    float hudX = p2Card.x + p2Card.w + 24.0F * scale;
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
    for (int index = 0; index < 5; ++index) {
        const float slotX = hudX + static_cast<float>(index) * 13.0F * scale;
        if (index < snapshot.arrows) {
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
        } else {
            fillRoundedRect(
                context.renderer,
                SDL_FRect{
                    slotX + 3.0F * scale,
                    top + 28.0F * scale,
                    4.0F * scale,
                    4.0F * scale},
                2.0F * scale,
                SDL_Color{65, 76, 85, SDL_ALPHA_OPAQUE});
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
    for (int index = 0; index < 3; ++index) {
        const SDL_FRect slot{
            packPill.x + (8.0F + static_cast<float>(index) * 23.0F) * scale,
            packPill.y + 6.0F * scale,
            16.0F * scale,
            16.0F * scale,
        };
        if (index < occupied) {
            drawPanel(
                context.renderer,
                slot,
                5.0F * scale,
                ui::Theme::surfaceSoft,
                SDL_Color{90, 102, 112, SDL_ALPHA_OPAQUE},
                scale);
            setColor(context.renderer, ui::Theme::mutedBright);
            SDL_RenderLine(
                context.renderer,
                slot.x + 4.0F * scale,
                slot.y + 8.0F * scale,
                slot.x + 8.0F * scale,
                slot.y + 4.0F * scale);
            SDL_RenderLine(
                context.renderer,
                slot.x + 8.0F * scale,
                slot.y + 4.0F * scale,
                slot.x + 12.0F * scale,
                slot.y + 8.0F * scale);
            SDL_RenderLine(
                context.renderer,
                slot.x + 12.0F * scale,
                slot.y + 8.0F * scale,
                slot.x + 8.0F * scale,
                slot.y + 12.0F * scale);
            SDL_RenderLine(
                context.renderer,
                slot.x + 8.0F * scale,
                slot.y + 12.0F * scale,
                slot.x + 4.0F * scale,
                slot.y + 8.0F * scale);
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
    std::optional<CaveId> extractionCave) {

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
            primary ? "SLAY THE BASILISK" : "HUNTER'S SIGIL",
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
            primary ? "ACTIVE" : "SECURED",
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
    setColor(context.renderer, accent);
    const SDL_FRect symbol = inset(emblem, 9.0F * scale);
    SDL_RenderRect(context.renderer, &symbol);
    SDL_RenderLine(
        context.renderer,
        symbol.x,
        symbol.y,
        symbol.x + symbol.w,
        symbol.y + symbol.h);

    if (!context.label(
            primary ? "Find the Basilisk and end its reign." :
                      "Carry the fallen hunter's Sigil to safety.",
            FontWeight::Regular,
            ui::Typography::objectiveBody,
            ui::Theme::mutedBright,
            panel.x + 61.0F * scale,
            panel.y + 65.0F * scale)) {
        return false;
    }
    if (!primary) {
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
        const std::string extractionText = extractionCave.has_value()
            ? "Extraction at Cave " + std::to_string(*extractionCave)
            : "Extraction location unavailable";
        return context.label(
            extractionText,
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

    const std::size_t visible = std::min<std::size_t>(3, snapshot.observations.size());
    for (std::size_t index = 0; index < visible; ++index) {
        const SDL_FRect row{
            panel.x + 13.0F * scale,
            panel.y + (40.0F + static_cast<float>(index) * 29.0F) * scale,
            panel.w - 26.0F * scale,
            25.0F * scale,
        };
        fillRoundedRect(
            context.renderer, row, 5.0F * scale, ui::Theme::surfaceRaised);
        fillRoundedRect(
            context.renderer,
            SDL_FRect{
                row.x + 7.0F * scale,
                row.y + 10.0F * scale,
                4.0F * scale,
                4.0F * scale},
            2.0F * scale,
            ui::Theme::gold);
        if (!context.label(
                presentation::observationText(snapshot.observations[index]),
                FontWeight::Regular,
                ui::Typography::reportBody,
                ui::Theme::mutedBright,
                row.x + 17.0F * scale,
                row.y + 5.0F * scale)) {
            return false;
        }
    }
    return true;
}

bool drawInventory(
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
            "INVENTORY",
            std::to_string(snapshot.inventory.items.size()) + "/" +
                std::to_string(snapshot.inventory.capacity),
            panel)) {
        return false;
    }

    const float gap = 7.0F * scale;
    const float cardWidth = (panel.w - 33.0F * scale) * 0.5F;
    for (std::size_t index = 0; index < 3; ++index) {
        const float column = static_cast<float>(index % 2);
        const float row = static_cast<float>(index / 2);
        const SDL_FRect card{
            panel.x + 13.0F * scale + column * (cardWidth + gap),
            panel.y + 40.0F * scale + row * 48.0F * scale,
            cardWidth,
            41.0F * scale,
        };
        const bool occupied = index < snapshot.inventory.items.size();
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
            setColor(context.renderer, ui::Theme::muted);
            SDL_RenderLine(
                context.renderer,
                icon.x + 6.0F * scale,
                icon.y + 12.0F * scale,
                icon.x + 12.0F * scale,
                icon.y + 6.0F * scale);
            SDL_RenderLine(
                context.renderer,
                icon.x + 12.0F * scale,
                icon.y + 6.0F * scale,
                icon.x + 18.0F * scale,
                icon.y + 12.0F * scale);
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
    const ScreenShellData& data,
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
            "AVAILABLE ACTIONS",
            std::to_string(data.actionRows.size()),
            panel)) {
        return false;
    }

    const std::size_t visible = std::min<std::size_t>(5, data.actionRows.size());
    for (std::size_t index = 0; index < visible; ++index) {
        const ActionRowView& action = data.actionRows[index];
        const SDL_FRect row{
            panel.x + 13.0F * scale,
            panel.y + (40.0F + static_cast<float>(index) * 37.0F) * scale,
            panel.w - 26.0F * scale,
            33.0F * scale,
        };
        drawPanel(
            context.renderer,
            row,
            6.0F * scale,
            ui::Theme::surfaceRaised,
            SDL_Color{37, 46, 54, SDL_ALPHA_OPAQUE},
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
        if (!context.centeredLabel(
                action.key, FontWeight::Bold, ui::Typography::actionKey, ui::Theme::text, key) ||
            !context.label(
                action.label,
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
    return true;
}

bool drawSidebar(
    const DrawingContext& context,
    const PlayerRoundSnapshot& snapshot,
    const ScreenShellData& data,
    SDL_FRect sidebar) {

    const float scale = context.scale;
    const float x = sidebar.x + 18.0F * scale;
    const float width = sidebar.w - 36.0F * scale;
    float y = sidebar.y + 18.0F * scale;

    const SDL_FRect primary{x, y, width, 126.0F * scale};
    if (!drawObjectiveCard(context, primary, true, std::nullopt)) return false;
    y += 136.0F * scale;

    const SDL_FRect secondary{x, y, width, 134.0F * scale};
    if (!drawObjectiveCard(context, secondary, false, snapshot.extractionCave)) return false;
    y += 146.0F * scale;

    const SDL_FRect report{x, y, width, 130.0F * scale};
    if (!drawRoundReport(context, snapshot, report)) return false;
    y += 142.0F * scale;

    const SDL_FRect inventory{x, y, width, 134.0F * scale};
    if (!drawInventory(context, snapshot, inventory)) return false;
    y += 146.0F * scale;

    const SDL_FRect actions{x, y, width, 228.0F * scale};
    return drawAvailableActions(context, data, actions);
}

} // namespace

bool renderScreenShell(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    const PlayerRoundSnapshot& snapshot,
    PlayerMapLayout& mapLayout,
    const ScreenShellData& data,
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
    const DrawingContext context{renderer, &textRenderer, scale, &error};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    setColor(renderer, ui::Theme::background);
    const SDL_FRect output{
        0.0F,
        0.0F,
        static_cast<float>(outputWidth),
        static_cast<float>(outputHeight),
    };
    SDL_RenderFillRect(renderer, &output);

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
            data.matchMetadata.totalCaves,
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

    mapLayout.update(snapshot.map);
    const SDL_Rect clip{
        static_cast<int>(mapContent.x),
        static_cast<int>(mapContent.y),
        std::max(0, static_cast<int>(mapContent.w)),
        std::max(0, static_cast<int>(mapContent.h)),
    };
    SDL_SetRenderClipRect(renderer, &clip);
    const MapViewport viewport{
        inset(mapContent, 18.0F * scale),
        LogicalPoint{},
        42.0F * scale,
    };
    renderPlayerKnownMap(
        renderer, snapshot.map, mapLayout, snapshot.currentCave, viewport);
    SDL_SetRenderClipRect(renderer, nullptr);

    if (!drawHeaderHud(context, snapshot, data, headerHeight)) return false;
    return drawSidebar(context, snapshot, data, sidebar);
}

} // namespace basilisk::game
