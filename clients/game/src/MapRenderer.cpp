#include "MapRenderer.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

namespace basilisk::game {
namespace {

constexpr float kNodeHalfSize = 9.0F;
constexpr float kCurrentHighlightHalfSize = 14.0F;

SDL_FPoint project(LogicalPoint point, const MapViewport& viewport) {
    return SDL_FPoint{
        viewport.bounds.x + viewport.bounds.w * 0.5F +
            static_cast<float>(point.x - viewport.logicalCenter.x) * viewport.scale,
        viewport.bounds.y + viewport.bounds.h * 0.5F +
            static_cast<float>(point.y - viewport.logicalCenter.y) * viewport.scale};
}

void setColor(SDL_Renderer* renderer, Uint8 red, Uint8 green, Uint8 blue) {
    SDL_SetRenderDrawColor(renderer, red, green, blue, SDL_ALPHA_OPAQUE);
}

void drawQuestionMark(SDL_Renderer* renderer, SDL_FPoint center) {
    constexpr float width = 4.0F;
    constexpr float height = 7.0F;
    const std::array<SDL_FPoint, 6> stroke{{
        {center.x - width, center.y - height},
        {center.x, center.y - height - 2.0F},
        {center.x + width, center.y - height},
        {center.x + width, center.y - 3.0F},
        {center.x, center.y},
        {center.x, center.y + 3.0F},
    }};
    SDL_RenderLines(renderer, stroke.data(), static_cast<int>(stroke.size()));

    const SDL_FRect dot{center.x - 1.0F, center.y + 6.0F, 2.0F, 2.0F};
    SDL_RenderFillRect(renderer, &dot);
}

void drawNode(SDL_Renderer* renderer, SDL_FPoint center, bool current) {
    if (current) {
        setColor(renderer, 241, 194, 92);
        const SDL_FRect highlight{
            center.x - kCurrentHighlightHalfSize,
            center.y - kCurrentHighlightHalfSize,
            kCurrentHighlightHalfSize * 2.0F,
            kCurrentHighlightHalfSize * 2.0F};
        SDL_RenderRect(renderer, &highlight);
    }

    setColor(renderer, current ? 184 : 52, current ? 132 : 61, current ? 34 : 72);
    const SDL_FRect node{
        center.x - kNodeHalfSize,
        center.y - kNodeHalfSize,
        kNodeHalfSize * 2.0F,
        kNodeHalfSize * 2.0F};
    SDL_RenderFillRect(renderer, &node);

    setColor(renderer, current ? 255 : 139, current ? 224 : 151, current ? 154 : 163);
    SDL_RenderRect(renderer, &node);
}

} // namespace

void renderPlayerKnownMap(
    SDL_Renderer* renderer,
    const PlayerMapView& map,
    const PlayerMapLayout& layout,
    CaveId currentCave,
    const MapViewport& viewport) {

    if (renderer == nullptr || viewport.bounds.w <= 0.0F ||
        viewport.bounds.h <= 0.0F || viewport.scale <= 0.0F) {
        return;
    }

    std::set<CaveId> discoveredCaves;
    for (const auto& cave : map.caves) discoveredCaves.insert(cave.cave);

    std::set<std::pair<CaveId, CaveId>> knownEdges;
    for (const auto& cave : map.caves) {
        for (const auto& exit : cave.exits) {
            if (!exit.destination.has_value() ||
                !discoveredCaves.contains(*exit.destination)) {
                continue;
            }
            const auto [low, high] = std::minmax(cave.cave, *exit.destination);
            knownEdges.emplace(low, high);
        }
    }

    setColor(renderer, 83, 96, 109);
    for (const auto& [source, destination] : knownEdges) {
        const auto sourcePosition = layout.cavePosition(source);
        const auto destinationPosition = layout.cavePosition(destination);
        if (!sourcePosition.has_value() || !destinationPosition.has_value()) continue;

        const SDL_FPoint a = project(*sourcePosition, viewport);
        const SDL_FPoint b = project(*destinationPosition, viewport);
        SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
    }

    setColor(renderer, 117, 131, 145);
    for (const auto& cave : map.caves) {
        const auto sourcePosition = layout.cavePosition(cave.cave);
        if (!sourcePosition.has_value()) continue;

        for (const auto& exit : cave.exits) {
            if (exit.destination.has_value()) continue;
            const auto stubPosition = layout.exitStubPosition(cave.cave, exit.id);
            if (!stubPosition.has_value()) continue;

            const SDL_FPoint source = project(*sourcePosition, viewport);
            const SDL_FPoint stub = project(*stubPosition, viewport);
            SDL_RenderLine(renderer, source.x, source.y, stub.x, stub.y);
            drawQuestionMark(renderer, stub);
        }
    }

    std::set<CaveId> renderedCaves;
    for (const auto& cave : map.caves) {
        if (!renderedCaves.insert(cave.cave).second) continue;
        const auto position = layout.cavePosition(cave.cave);
        if (!position.has_value()) continue;
        drawNode(renderer, project(*position, viewport), cave.cave == currentCave);
    }
}

} // namespace basilisk::game
