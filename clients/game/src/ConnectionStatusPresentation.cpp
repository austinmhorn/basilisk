#include "ConnectionStatusPresentation.hpp"

#include <algorithm>

#include "UITheme.hpp"

namespace basilisk::game {
namespace {

SDL_Color statusColor(NetworkConnectionState state) noexcept {
    switch (state) {
    case NetworkConnectionState::Connecting:
        return ui::Theme::gold;
    case NetworkConnectionState::Connected:
        return ui::Theme::blue;
    case NetworkConnectionState::Disconnected:
        return ui::Theme::mutedBright;
    case NetworkConnectionState::Error:
        return ui::Theme::red;
    }
    return ui::Theme::mutedBright;
}

} // namespace

std::string_view connectionStatusText(NetworkConnectionState state) noexcept {
    switch (state) {
    case NetworkConnectionState::Connecting:
        return "Connecting";
    case NetworkConnectionState::Connected:
        return "Connected";
    case NetworkConnectionState::Disconnected:
        return "Disconnected";
    case NetworkConnectionState::Error:
        return "Connection Error";
    }
    return "Connection Error";
}

bool renderConnectionStatus(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    NetworkConnectionState state,
    std::string_view detail,
    bool sessionReady,
    int outputWidth,
    int outputHeight,
    std::string& error) {

    error.clear();
    if (renderer == nullptr || outputWidth <= 0 || outputHeight <= 0) {
        error = "Connection status requires a valid renderer and output size";
        return false;
    }

    const float scale = std::max(
        0.75F,
        std::min(
            static_cast<float>(outputWidth) / 1440.0F,
            static_cast<float>(outputHeight) / 900.0F));
    const std::string_view label = connectionStatusText(state);
    const float pointSize = (sessionReady ? 9.0F : 15.0F) * scale;
    const auto measured = textRenderer.measureText(
        label, FontWeight::SemiBold, pointSize, error);
    if (!measured.has_value()) return false;

    const float horizontalPadding = (sessionReady ? 11.0F : 24.0F) * scale;
    const float verticalPadding = (sessionReady ? 6.0F : 16.0F) * scale;
    const float detailHeight = !sessionReady && !detail.empty() ? 24.0F * scale : 0.0F;
    const float panelWidth = sessionReady
        ? static_cast<float>(measured->width) + horizontalPadding * 2.0F
        : std::min(520.0F * scale, static_cast<float>(outputWidth) - 48.0F * scale);
    const float panelHeight = static_cast<float>(measured->height) +
        verticalPadding * 2.0F + detailHeight;
    const SDL_FRect panel{
        sessionReady
            ? static_cast<float>(outputWidth) - panelWidth - 16.0F * scale
            : (static_cast<float>(outputWidth) - panelWidth) * 0.5F,
        sessionReady
            ? static_cast<float>(outputHeight) - panelHeight - 16.0F * scale
            : (static_cast<float>(outputHeight) - panelHeight) * 0.5F,
        panelWidth,
        panelHeight,
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 15, 20, 25, 235);
    SDL_RenderFillRect(renderer, &panel);
    const SDL_Color accent = statusColor(state);
    SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, accent.a);
    SDL_RenderRect(renderer, &panel);

    if (!textRenderer.drawText(
            label,
            FontWeight::SemiBold,
            pointSize,
            accent,
            SDL_FPoint{
                sessionReady
                    ? panel.x + horizontalPadding
                    : panel.x + (panel.w - static_cast<float>(measured->width)) * 0.5F,
                panel.y + verticalPadding,
            },
            error)) {
        return false;
    }

    if (!sessionReady && !detail.empty()) {
        const float detailPointSize = 10.0F * scale;
        const auto detailSize = textRenderer.measureText(
            detail, FontWeight::Regular, detailPointSize, error);
        if (!detailSize.has_value()) return false;
        const float detailX = panel.x + std::max(
            12.0F * scale,
            (panel.w - static_cast<float>(detailSize->width)) * 0.5F);
        if (!textRenderer.drawText(
                detail,
                FontWeight::Regular,
                detailPointSize,
                ui::Theme::mutedBright,
                SDL_FPoint{
                    detailX,
                    panel.y + verticalPadding +
                        static_cast<float>(measured->height) + 6.0F * scale,
                },
                error)) {
            return false;
        }
    }

    return true;
}

} // namespace basilisk::game
