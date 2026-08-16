#include "DebugMapRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace basilisk::game::debug {
namespace {

constexpr int kCircleSegments = 32;

SDL_FPoint project(LogicalPoint point, const MapTransform& transform) {
    const PresentationPoint projected = projectMapPoint(point, transform);
    return SDL_FPoint{
        static_cast<float>(projected.x),
        static_cast<float>(projected.y)};
}

void setColor(SDL_Renderer* renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void drawCircle(
    SDL_Renderer* renderer,
    SDL_FPoint center,
    float radius,
    SDL_Color color) {

    std::vector<SDL_FPoint> points;
    points.reserve(kCircleSegments + 1);
    for (int segment = 0; segment <= kCircleSegments; ++segment) {
        const double angle = 2.0 * std::numbers::pi *
            static_cast<double>(segment) / static_cast<double>(kCircleSegments);
        points.push_back(SDL_FPoint{
            center.x + radius * static_cast<float>(std::cos(angle)),
            center.y + radius * static_cast<float>(std::sin(angle))});
    }
    setColor(renderer, color);
    SDL_RenderLines(renderer, points.data(), static_cast<int>(points.size()));
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
        const float halfWidth = std::sqrt(
            std::max(0.0F, radius * radius - y * y));
        SDL_RenderLine(
            renderer,
            center.x - halfWidth,
            center.y + y,
            center.x + halfWidth,
            center.y + y);
    }
}

std::set<CaveId> discoveredCaves(const PlayerMapView& map) {
    std::set<CaveId> caves;
    for (const DiscoveredCaveView& cave : map.caves) caves.insert(cave.cave);
    return caves;
}

std::set<PhysicalTunnel> discoveredTunnels(const PlayerMapView& map) {
    const std::set<CaveId> caves = discoveredCaves(map);
    std::set<PhysicalTunnel> tunnels;
    for (const DiscoveredCaveView& cave : map.caves) {
        for (const TunnelView& exit : cave.exits) {
            if (!exit.destination.has_value() ||
                !caves.contains(*exit.destination)) {
                continue;
            }
            const auto [first, second] = std::minmax(cave.cave, *exit.destination);
            tunnels.insert(PhysicalTunnel{first, second});
        }
    }
    return tunnels;
}

void drawHiddenTunnel(
    SDL_Renderer* renderer,
    SDL_FPoint start,
    SDL_FPoint end,
    bool startDiscovered,
    bool endDiscovered,
    float scale) {

    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.01F) return;
    const float unitX = dx / length;
    const float unitY = dy / length;
    const float startInset = (startDiscovered ? 21.0F : 10.0F) * scale;
    const float endInset = (endDiscovered ? 21.0F : 10.0F) * scale;
    const SDL_FPoint clippedStart{
        start.x + unitX * std::min(startInset, length * 0.4F),
        start.y + unitY * std::min(startInset, length * 0.4F)};
    const SDL_FPoint clippedEnd{
        end.x - unitX * std::min(endInset, length * 0.4F),
        end.y - unitY * std::min(endInset, length * 0.4F)};

    setColor(renderer, SDL_Color{83, 96, 109, 150});
    constexpr float dash = 5.0F;
    constexpr float gap = 5.0F;
    const float clippedLength = std::sqrt(
        (clippedEnd.x - clippedStart.x) * (clippedEnd.x - clippedStart.x) +
        (clippedEnd.y - clippedStart.y) * (clippedEnd.y - clippedStart.y));
    if (clippedLength <= 0.01F) return;
    for (float offset = 0.0F; offset < clippedLength; offset += dash + gap) {
        const float finish = std::min(clippedLength, offset + dash);
        SDL_RenderLine(
            renderer,
            clippedStart.x + (clippedEnd.x - clippedStart.x) * offset / clippedLength,
            clippedStart.y + (clippedEnd.y - clippedStart.y) * offset / clippedLength,
            clippedStart.x + (clippedEnd.x - clippedStart.x) * finish / clippedLength,
            clippedStart.y + (clippedEnd.y - clippedStart.y) * finish / clippedLength);
    }
}

const char* behaviorName(BasiliskBehavior behavior) {
    switch (behavior) {
        case BasiliskBehavior::Normal: return "NORMAL";
        case BasiliskBehavior::Restless: return "RESTLESS";
        case BasiliskBehavior::Lurker: return "LURKER";
        case BasiliskBehavior::Skittish: return "SKITTISH";
        case BasiliskBehavior::Territorial: return "TERRITORIAL";
        case BasiliskBehavior::Enraged: return "ENRAGED";
    }
    return "UNKNOWN";
}

bool drawTruthMarker(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    const DebugMapTruth& mapTruth,
    const MapPresentationGeometry& geometry,
    CaveId cave,
    std::string_view label,
    SDL_Color color,
    float radius,
    std::string& error) {

    const auto position = mapTruth.cavePositions.find(cave);
    if (position == mapTruth.cavePositions.end()) return true;
    const float scale = static_cast<float>(geometry.transform.uiScale);
    const SDL_FPoint center = project(position->second, geometry.transform);
    fillCircle(renderer, center, radius * scale, SDL_Color{10, 13, 18, 220});
    drawCircle(renderer, center, radius * scale, color);
    const float pointSize = 8.0F * scale;
    const auto measured = textRenderer.measureText(
        label, FontWeight::Bold, pointSize, error);
    return measured.has_value() && textRenderer.drawText(
        label,
        FontWeight::Bold,
        pointSize,
        color,
        SDL_FPoint{
            center.x - static_cast<float>(measured->width) * 0.5F,
            center.y - static_cast<float>(measured->height) * 0.5F},
        error);
}

} // namespace

bool renderRevealedPhysicalMap(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    const DebugMapTruth& truth,
    const PlayerRoundSnapshot& snapshot,
    const MapPresentationGeometry& geometry,
    std::string& error) {

    error.clear();
    if (renderer == nullptr) {
        error = "Debug map reveal requires a renderer";
        return false;
    }

    const float scale = static_cast<float>(geometry.transform.uiScale);
    const std::set<CaveId> discovered = discoveredCaves(snapshot.map);
    const std::set<PhysicalTunnel> knownTunnels = discoveredTunnels(snapshot.map);

    for (const PhysicalTunnel& tunnel : truth.tunnels) {
        if (knownTunnels.contains(tunnel)) continue;
        const auto first = truth.cavePositions.find(tunnel.first);
        const auto second = truth.cavePositions.find(tunnel.second);
        if (first == truth.cavePositions.end() ||
            second == truth.cavePositions.end()) {
            continue;
        }
        drawHiddenTunnel(
            renderer,
            project(first->second, geometry.transform),
            project(second->second, geometry.transform),
            discovered.contains(tunnel.first),
            discovered.contains(tunnel.second),
            scale);
    }

    for (const auto& [cave, position] : truth.cavePositions) {
        if (discovered.contains(cave)) continue;
        const SDL_FPoint center = project(position, geometry.transform);
        fillCircle(
            renderer,
            center,
            10.0F * scale,
            SDL_Color{17, 23, 29, SDL_ALPHA_OPAQUE});
        drawCircle(
            renderer,
            center,
            10.0F * scale,
            SDL_Color{102, 116, 129, 190});
        const std::string label = std::to_string(cave);
        const float pointSize = 7.5F * scale;
        const auto measured = textRenderer.measureText(
            label, FontWeight::Medium, pointSize, error);
        if (!measured.has_value() || !textRenderer.drawText(
                label,
                FontWeight::Medium,
                pointSize,
                SDL_Color{132, 145, 157, 210},
                SDL_FPoint{
                    center.x - static_cast<float>(measured->width) * 0.5F,
                    center.y - static_cast<float>(measured->height) * 0.5F},
                error)) {
            return false;
        }
    }

    const SDL_FPoint indicator{
        static_cast<float>(geometry.transform.bounds.x + 12.0 * scale),
        static_cast<float>(geometry.transform.bounds.y + 10.0 * scale)};
    return textRenderer.drawText(
        "DEBUG  /  MAP REVEALED",
        FontWeight::SemiBold,
        10.0F * scale,
        SDL_Color{139, 171, 196, SDL_ALPHA_OPAQUE},
        indicator,
        error);
}

bool renderGameplayTruth(
    SDL_Renderer* renderer,
    TextRenderer& textRenderer,
    const DebugMapTruth& mapTruth,
    const DebugGameplayTruth& gameplayTruth,
    const MapPresentationGeometry& geometry,
    std::string& error) {

    error.clear();
    if (renderer == nullptr) {
        error = "Debug gameplay truth requires a renderer";
        return false;
    }

    for (const CaveId cave : gameplayTruth.pitCaves) {
        if (!drawTruthMarker(
                renderer, textRenderer, mapTruth, geometry, cave, "P",
                SDL_Color{225, 78, 86, SDL_ALPHA_OPAQUE}, 12.0F, error)) {
            return false;
        }
    }
    for (const CaveId cave : gameplayTruth.jackalCaves) {
        if (!drawTruthMarker(
                renderer, textRenderer, mapTruth, geometry, cave, "J",
                SDL_Color{236, 151, 70, SDL_ALPHA_OPAQUE}, 11.0F, error)) {
            return false;
        }
    }
    if (gameplayTruth.basiliskLastCave.has_value() &&
        !drawTruthMarker(
            renderer,
            textRenderer,
            mapTruth,
            geometry,
            *gameplayTruth.basiliskLastCave,
            "L",
            SDL_Color{77, 190, 220, SDL_ALPHA_OPAQUE},
            13.0F,
            error)) {
        return false;
    }
    if (gameplayTruth.territorialSearchTarget.has_value() &&
        !drawTruthMarker(
            renderer,
            textRenderer,
            mapTruth,
            geometry,
            *gameplayTruth.territorialSearchTarget,
            "T",
            SDL_Color{83, 211, 145, SDL_ALPHA_OPAQUE},
            14.0F,
            error)) {
        return false;
    }
    if (!drawTruthMarker(
            renderer,
            textRenderer,
            mapTruth,
            geometry,
            gameplayTruth.basiliskCave,
            "B",
            SDL_Color{218, 93, 225, SDL_ALPHA_OPAQUE},
            15.0F,
            error)) {
        return false;
    }

    const auto basiliskPosition =
        mapTruth.cavePositions.find(gameplayTruth.basiliskCave);
    if (basiliskPosition == mapTruth.cavePositions.end()) return true;
    const float scale = static_cast<float>(geometry.transform.uiScale);
    const SDL_FPoint center = project(
        basiliskPosition->second, geometry.transform);
    std::string detail = std::string{"BASILISK  "} +
        behaviorName(gameplayTruth.basiliskBehavior) + "  E" +
        std::to_string(gameplayTruth.basiliskEncounterCount);
    return textRenderer.drawText(
        detail,
        FontWeight::SemiBold,
        8.0F * scale,
        SDL_Color{232, 151, 236, SDL_ALPHA_OPAQUE},
        SDL_FPoint{center.x + 19.0F * scale, center.y - 7.0F * scale},
        error);
}

bool renderDebugStatusLegend(
    TextRenderer& textRenderer,
    const MapPresentationGeometry& geometry,
    bool mapRevealed,
    bool truthRevealed,
    BasiliskBehavior behavior,
    std::string& error) {

    const float scale = static_cast<float>(geometry.transform.uiScale);
    const std::array<std::string, 3> lines{
        std::string{"F1  MAP    "} + (mapRevealed ? "ON" : "OFF"),
        std::string{"F2  TRUTH  "} + (truthRevealed ? "ON" : "OFF"),
        std::string{"F3  BEHAVIOR  "} + behaviorName(behavior)};
    float y = static_cast<float>(
        geometry.transform.bounds.y + 10.0 * scale);
    for (const std::string& line : lines) {
        const auto measured = textRenderer.measureText(
            line, FontWeight::SemiBold, 8.0F * scale, error);
        if (!measured.has_value()) return false;
        const float x = static_cast<float>(
            geometry.transform.bounds.x + geometry.transform.bounds.width -
            12.0 * scale - static_cast<double>(measured->width));
        if (!textRenderer.drawText(
                line,
                FontWeight::SemiBold,
                8.0F * scale,
                SDL_Color{139, 171, 196, SDL_ALPHA_OPAQUE},
                SDL_FPoint{x, y},
                error)) {
            return false;
        }
        y += 12.0F * scale;
    }
    return true;
}

} // namespace basilisk::game::debug
