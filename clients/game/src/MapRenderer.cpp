#include "MapRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "UITheme.hpp"

namespace basilisk::game {
namespace {

constexpr int kCircleSegments = 64;

SDL_FPoint toSdl(PresentationPoint point) {
    return SDL_FPoint{
        static_cast<float>(point.x),
        static_cast<float>(point.y)};
}

void setColor(SDL_Renderer* renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void drawThickLine(
    SDL_Renderer* renderer,
    SDL_FPoint start,
    SDL_FPoint end,
    float width,
    SDL_Color color) {

    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.01F) return;
    const float normalX = -dy / length;
    const float normalY = dx / length;
    const int strokes = std::max(1, static_cast<int>(std::ceil(width)));
    setColor(renderer, color);
    for (int stroke = 0; stroke < strokes; ++stroke) {
        const float offset = static_cast<float>(stroke) -
            static_cast<float>(strokes - 1) * 0.5F;
        SDL_RenderLine(
            renderer,
            start.x + normalX * offset,
            start.y + normalY * offset,
            end.x + normalX * offset,
            end.y + normalY * offset);
    }
}

void drawDashedLine(
    SDL_Renderer* renderer,
    SDL_FPoint start,
    SDL_FPoint end,
    float width,
    float dashLength,
    float gapLength,
    SDL_Color color) {

    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.01F) return;
    for (float offset = 0.0F; offset < length; offset += dashLength + gapLength) {
        const float finish = std::min(length, offset + dashLength);
        drawThickLine(
            renderer,
            {start.x + dx * offset / length, start.y + dy * offset / length},
            {start.x + dx * finish / length, start.y + dy * finish / length},
            width,
            color);
    }
}

std::vector<SDL_FPoint> circlePoints(SDL_FPoint center, float radius) {
    std::vector<SDL_FPoint> points;
    points.reserve(kCircleSegments + 1);
    for (int segment = 0; segment <= kCircleSegments; ++segment) {
        const double angle = 2.0 * std::numbers::pi *
            static_cast<double>(segment) / static_cast<double>(kCircleSegments);
        points.push_back(SDL_FPoint{
            center.x + radius * static_cast<float>(std::cos(angle)),
            center.y + radius * static_cast<float>(std::sin(angle))});
    }
    return points;
}

void fillCircle(
    SDL_Renderer* renderer,
    SDL_FPoint center,
    float radius,
    SDL_Color color) {

    setColor(renderer, color);
    const int rows = static_cast<int>(std::ceil(radius));
    for (int row = -rows; row <= rows; ++row) {
        const float y = static_cast<float>(row);
        const float halfWidth = std::sqrt(std::max(0.0F, radius * radius - y * y));
        SDL_RenderLine(
            renderer,
            center.x - halfWidth,
            center.y + y,
            center.x + halfWidth,
            center.y + y);
    }
}

void drawCircleOutline(
    SDL_Renderer* renderer,
    SDL_FPoint center,
    float radius,
    float width,
    SDL_Color color) {

    setColor(renderer, color);
    const int strokes = std::max(1, static_cast<int>(std::ceil(width)));
    for (int stroke = 0; stroke < strokes; ++stroke) {
        const float inset = static_cast<float>(stroke) -
            static_cast<float>(strokes - 1) * 0.5F;
        const auto points = circlePoints(center, std::max(0.5F, radius + inset));
        SDL_RenderLines(renderer, points.data(), static_cast<int>(points.size()));
    }
}

void drawDashedCircle(
    SDL_Renderer* renderer,
    SDL_FPoint center,
    float radius,
    float width,
    SDL_Color color) {

    constexpr int dashSegments = 4;
    setColor(renderer, color);
    for (int segment = 0; segment < kCircleSegments; ++segment) {
        if ((segment / dashSegments) % 2 != 0) continue;
        const double angleA = 2.0 * std::numbers::pi *
            static_cast<double>(segment) / static_cast<double>(kCircleSegments);
        const double angleB = 2.0 * std::numbers::pi *
            static_cast<double>(segment + 1) / static_cast<double>(kCircleSegments);
        drawThickLine(
            renderer,
            {center.x + radius * static_cast<float>(std::cos(angleA)),
             center.y + radius * static_cast<float>(std::sin(angleA))},
            {center.x + radius * static_cast<float>(std::cos(angleB)),
             center.y + radius * static_cast<float>(std::sin(angleB))},
            width,
            color);
    }
}

bool drawCenteredText(
    TextRenderer& textRenderer,
    std::string_view text,
    float pointSize,
    SDL_Color color,
    SDL_FPoint center,
    std::string& error) {

    const auto size = textRenderer.measureText(
        text, FontWeight::Bold, pointSize, error);
    if (!size.has_value()) return false;
    return textRenderer.drawText(
        text,
        FontWeight::Bold,
        pointSize,
        color,
        {center.x - static_cast<float>(size->width) * 0.5F,
         center.y - static_cast<float>(size->height) * 0.5F},
        error);
}

struct VisibleEdge {
    CaveId source{};
    CaveId destination{};
    bool pitWarning{false};
};

std::vector<VisibleEdge> visibleEdges(const PlayerMapView& map) {
    std::set<CaveId> discovered;
    for (const DiscoveredCaveView& cave : map.caves) discovered.insert(cave.cave);

    std::map<std::pair<CaveId, CaveId>, bool> normalized;
    for (const DiscoveredCaveView& cave : map.caves) {
        for (const TunnelView& exit : cave.exits) {
            if (!exit.destination.has_value() ||
                !discovered.contains(*exit.destination)) {
                continue;
            }
            const auto [low, high] = std::minmax(cave.cave, *exit.destination);
            normalized[{low, high}] = normalized[{low, high}] || exit.strongColdDraft;
        }
    }

    std::vector<VisibleEdge> result;
    result.reserve(normalized.size());
    for (const auto& [edge, warning] : normalized) {
        result.push_back({edge.first, edge.second, warning});
    }
    return result;
}

bool containsPit(const PlayerRoundSnapshot& snapshot, CaveId cave) {
    return std::find(
               snapshot.temporarilyRevealedPitCaves.begin(),
               snapshot.temporarilyRevealedPitCaves.end(),
               cave) != snapshot.temporarilyRevealedPitCaves.end();
}

} // namespace

bool renderPlayerKnownMap(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    const PlayerRoundSnapshot& snapshot,
    const PlayerMapLayout& layout,
    const MapPresentationGeometry& geometry,
    const MapPresentationState& presentation,
    std::string& error) {

    error.clear();
    if (renderer == nullptr || geometry.transform.bounds.width <= 0.0 ||
        geometry.transform.bounds.height <= 0.0 ||
        geometry.transform.pixelsPerLogicalUnit <= 0.0) {
        return true;
    }

    const float uiScale = static_cast<float>(geometry.transform.uiScale);
    const auto projectedCave = [&](CaveId cave) -> std::optional<SDL_FPoint> {
        const auto position = layout.cavePosition(cave);
        if (!position.has_value()) return std::nullopt;
        return toSdl(projectMapPoint(*position, geometry.transform));
    };

    const RouteEdgeSet highlightedRouteEdges = routeEdges(
        selectedRoute(presentation, snapshot.map));
    for (const VisibleEdge& edge : visibleEdges(snapshot.map)) {
        const auto source = projectedCave(edge.source);
        const auto destination = projectedCave(edge.destination);
        if (!source.has_value() || !destination.has_value()) continue;
        const bool planned = containsRouteEdge(
            highlightedRouteEdges, edge.source, edge.destination);
        const bool hovered = presentation.hoveredCave.has_value() &&
            ((edge.source == snapshot.map.currentCave &&
              edge.destination == *presentation.hoveredCave) ||
             (edge.destination == snapshot.map.currentCave &&
              edge.source == *presentation.hoveredCave));
        if (planned) {
            drawThickLine(
                renderer, *source, *destination, 10.0F * uiScale,
                SDL_Color{4, 7, 10, SDL_ALPHA_OPAQUE});
            drawThickLine(
                renderer, *source, *destination, 5.0F * uiScale,
                ui::Theme::blue);
        } else if (hovered) {
            drawThickLine(
                renderer, *source, *destination, 9.0F * uiScale,
                SDL_Color{5, 7, 9, SDL_ALPHA_OPAQUE});
            drawThickLine(
                renderer, *source, *destination, 4.0F * uiScale,
                ui::Theme::text);
        } else {
            drawThickLine(
                renderer, *source, *destination, 8.0F * uiScale,
                SDL_Color{5, 7, 9, SDL_ALPHA_OPAQUE});
            drawThickLine(
                renderer, *source, *destination,
                (edge.pitWarning ? 3.0F : 2.2F) * uiScale,
                edge.pitWarning ? ui::Theme::gold :
                    SDL_Color{101, 113, 125, SDL_ALPHA_OPAQUE});
        }
    }

    for (const DiscoveredCaveView& cave : snapshot.map.caves) {
        const auto source = projectedCave(cave.cave);
        if (!source.has_value()) continue;
        for (const TunnelView& exit : cave.exits) {
            if (exit.destination.has_value()) continue;
            const auto stubPosition = layout.exitStubPosition(cave.cave, exit.id);
            if (!stubPosition.has_value()) continue;
            const SDL_FPoint stub = toSdl(projectMapPoint(*stubPosition, geometry.transform));
            const UnknownExitKey key{cave.cave, exit.id};
            const bool hovered = presentation.hoveredUnknownExit == key;
            const SDL_Color stroke = exit.strongColdDraft
                ? ui::Theme::gold
                : (hovered ? ui::Theme::text :
                    SDL_Color{123, 135, 146, SDL_ALPHA_OPAQUE});
            drawDashedLine(
                renderer, *source, stub, 7.0F * uiScale,
                8.0F * uiScale, 6.0F * uiScale,
                SDL_Color{5, 7, 9, SDL_ALPHA_OPAQUE});
            drawDashedLine(
                renderer, *source, stub,
                (hovered ? 4.0F : (exit.strongColdDraft ? 3.0F : 2.0F)) * uiScale,
                8.0F * uiScale, 6.0F * uiScale, stroke);
            fillCircle(
                renderer, stub, 13.0F * uiScale,
                SDL_Color{14, 18, 22, SDL_ALPHA_OPAQUE});
            drawDashedCircle(
                renderer, stub, (hovered ? 16.0F : 14.0F) * uiScale,
                (hovered ? 3.0F : 2.0F) * uiScale, stroke);
            if (!drawCenteredText(
                    textRenderer, "?", 13.0F * uiScale,
                    ui::Theme::text, stub, error)) {
                return false;
            }
        }
    }

    if (presentation.routeDestination.has_value()) {
        if (const auto destination = projectedCave(*presentation.routeDestination)) {
            drawDashedCircle(
                renderer, *destination, 25.0F * uiScale,
                2.5F * uiScale, ui::Theme::blue);
        }
    }

    std::set<CaveId> rendered;
    for (const DiscoveredCaveView& cave : snapshot.map.caves) {
        if (!rendered.insert(cave.cave).second) continue;
        const auto center = projectedCave(cave.cave);
        if (!center.has_value()) continue;
        const bool current = cave.cave == snapshot.currentCave;
        const bool pit = containsPit(snapshot, cave.cave);
        const bool hovered = presentation.hoveredCave == cave.cave;
        const float radius = (current ? 18.0F : 16.0F) * uiScale;
        fillCircle(
            renderer,
            *center,
            radius,
            current ? SDL_Color{169, 119, 30, SDL_ALPHA_OPAQUE} :
                SDL_Color{31, 39, 46, SDL_ALPHA_OPAQUE});
        const SDL_Color outline = current
            ? SDL_Color{255, 224, 154, SDL_ALPHA_OPAQUE}
            : (pit
                ? (hovered ? SDL_Color{239, 132, 120, SDL_ALPHA_OPAQUE} :
                    SDL_Color{201, 91, 79, SDL_ALPHA_OPAQUE})
                : (hovered ? ui::Theme::text :
                    SDL_Color{135, 147, 158, SDL_ALPHA_OPAQUE}));
        drawCircleOutline(
            renderer,
            *center,
            radius + (current ? 3.0F : 0.0F) * uiScale,
            (hovered ? 4.0F : (current ? 3.0F : 2.0F)) * uiScale,
            outline);
        if (!drawCenteredText(
                textRenderer,
                std::to_string(cave.cave),
                13.0F * uiScale,
                ui::Theme::text,
                *center,
                error)) {
            return false;
        }
    }

    for (const auto& [pit, position] : geometry.temporaryPitPositions) {
        const SDL_FPoint center = toSdl(projectMapPoint(position, geometry.transform));
        fillCircle(
            renderer, center, 16.0F * uiScale,
            SDL_Color{31, 28, 29, SDL_ALPHA_OPAQUE});
        drawCircleOutline(
            renderer, center, 17.0F * uiScale, 3.0F * uiScale,
            SDL_Color{201, 91, 79, SDL_ALPHA_OPAQUE});
        if (!drawCenteredText(
                textRenderer,
                std::to_string(pit),
                13.0F * uiScale,
                ui::Theme::text,
                center,
                error)) {
            return false;
        }
    }
    return true;
}

} // namespace basilisk::game
