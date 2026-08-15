#include "MapLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace basilisk::game {
namespace {

constexpr double kCaveSpacing = 4.0;
constexpr double kStubLength = 1.5;
constexpr double kMinimumCaveSeparation = 2.0;
constexpr double kDisconnectedSpacing = 6.0;

constexpr std::array<LogicalPoint, 16> kDirections{{
    {1.0, 0.0},
    {0.9238795325, 0.3826834324},
    {0.7071067812, 0.7071067812},
    {0.3826834324, 0.9238795325},
    {0.0, 1.0},
    {-0.3826834324, 0.9238795325},
    {-0.7071067812, 0.7071067812},
    {-0.9238795325, 0.3826834324},
    {-1.0, 0.0},
    {-0.9238795325, -0.3826834324},
    {-0.7071067812, -0.7071067812},
    {-0.3826834324, -0.9238795325},
    {0.0, -1.0},
    {0.3826834324, -0.9238795325},
    {0.7071067812, -0.7071067812},
    {0.9238795325, -0.3826834324},
}};

LogicalPoint deterministicDirection(CaveId source, TunnelId tunnel) {
    const std::uint64_t localIndex = tunnel == 0 ? 0 : tunnel - 1;
    const std::uint64_t slot =
        (static_cast<std::uint64_t>(source) * 7U + localIndex) % kDirections.size();
    return kDirections[static_cast<std::size_t>(slot)];
}

double distanceSquared(LogicalPoint a, LogicalPoint b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

void include(LogicalBounds& bounds, LogicalPoint point) {
    bounds.minimumX = std::min(bounds.minimumX, point.x);
    bounds.minimumY = std::min(bounds.minimumY, point.y);
    bounds.maximumX = std::max(bounds.maximumX, point.x);
    bounds.maximumY = std::max(bounds.maximumY, point.y);
    bounds.populated = true;
}

using VisibleCaves = std::map<CaveId, std::vector<TunnelView>>;

VisibleCaves normalize(const PlayerMapView& map) {
    VisibleCaves caves;
    for (const auto& cave : map.caves) {
        auto& exits = caves[cave.cave];
        exits.insert(exits.end(), cave.exits.begin(), cave.exits.end());
    }

    for (auto& [cave, exits] : caves) {
        (void)cave;
        std::sort(exits.begin(), exits.end(), [](const TunnelView& a, const TunnelView& b) {
            if (a.id != b.id) return a.id < b.id;
            return a.destination.value_or(std::numeric_limits<CaveId>::max()) <
                   b.destination.value_or(std::numeric_limits<CaveId>::max());
        });
    }
    return caves;
}

} // namespace

void PlayerMapLayout::update(const PlayerMapView& map) {
    if (fixedBounds_.has_value()) {
        cavePositions_.clear();
        exitDirections_.clear();
        fixedExitEndpoints_.clear();
        fixedBounds_.reset();
    }
    const VisibleCaves caves = normalize(map);
    if (caves.empty()) return;

    for (const auto& [source, exits] : caves) {
        for (const auto& exit : exits) {
            exitDirections_.try_emplace(
                MapExitKey{source, exit.id},
                deterministicDirection(source, exit.id));
        }
    }

    if (cavePositions_.empty()) {
        const CaveId root = caves.contains(map.currentCave)
            ? map.currentCave
            : caves.begin()->first;
        cavePositions_.emplace(root, LogicalPoint{});
    }

    while (true) {
        bool allPositioned = true;
        bool placedConnectedCave = false;

        for (const auto& [source, exits] : caves) {
            if (!cavePositions_.contains(source)) {
                allPositioned = false;
                continue;
            }

            for (const auto& exit : exits) {
                if (!exit.destination.has_value()) continue;
                const CaveId destination = *exit.destination;
                if (!caves.contains(destination) || cavePositions_.contains(destination)) continue;

                cavePositions_.emplace(
                    destination,
                    positionAlongExit(source, exit.id));
                placedConnectedCave = true;
            }
        }

        if (allPositioned) break;
        if (placedConnectedCave) continue;

        const auto unpositioned = std::find_if(
            caves.begin(), caves.end(),
            [&](const auto& entry) { return !cavePositions_.contains(entry.first); });
        if (unpositioned == caves.end()) break;
        cavePositions_.emplace(unpositioned->first, disconnectedPosition());
    }
}

void PlayerMapLayout::updateFixed(const PlayerFixedMapGeometry& geometry) {
    cavePositions_ = geometry.discoveredCaves;
    fixedExitEndpoints_ = geometry.unknownExitEndpoints;
    fixedBounds_ = geometry.fullBounds.populated
        ? std::optional<LogicalBounds>{geometry.fullBounds}
        : std::nullopt;
}

std::optional<LogicalPoint> PlayerMapLayout::cavePosition(CaveId cave) const {
    const auto found = cavePositions_.find(cave);
    if (found == cavePositions_.end()) return std::nullopt;
    return found->second;
}

std::optional<LogicalPoint> PlayerMapLayout::exitStubPosition(
    CaveId source,
    TunnelId tunnel) const {

    const auto cave = cavePositions_.find(source);
    const auto fixed = fixedExitEndpoints_.find(MapExitKey{source, tunnel});
    if (fixed != fixedExitEndpoints_.end()) return fixed->second;

    const auto direction = exitDirections_.find(MapExitKey{source, tunnel});
    if (cave == cavePositions_.end() || direction == exitDirections_.end()) {
        return std::nullopt;
    }

    return LogicalPoint{
        cave->second.x + direction->second.x * kStubLength,
        cave->second.y + direction->second.y * kStubLength};
}

std::optional<LogicalBounds> PlayerMapLayout::fixedBounds() const noexcept {
    return fixedBounds_;
}

LogicalBounds PlayerMapLayout::positionedBounds() const noexcept {
    LogicalBounds bounds;
    for (const auto& [cave, position] : cavePositions_) {
        (void)cave;
        include(bounds, position);
    }
    return bounds;
}

bool PlayerMapLayout::positionIsFree(LogicalPoint candidate) const {
    constexpr double minimumDistanceSquared =
        kMinimumCaveSeparation * kMinimumCaveSeparation;
    return std::all_of(
        cavePositions_.begin(), cavePositions_.end(),
        [&](const auto& entry) {
            return distanceSquared(candidate, entry.second) >= minimumDistanceSquared;
        });
}

LogicalPoint PlayerMapLayout::positionAlongExit(
    CaveId source,
    TunnelId tunnel) const {

    const LogicalPoint origin = cavePositions_.at(source);
    const LogicalPoint direction = exitDirections_.at(MapExitKey{source, tunnel});
    for (std::size_t step = 1;; ++step) {
        const double distance = kCaveSpacing * static_cast<double>(step);
        const LogicalPoint candidate{
            origin.x + direction.x * distance,
            origin.y + direction.y * distance};
        if (positionIsFree(candidate)) return candidate;
    }
}

LogicalPoint PlayerMapLayout::disconnectedPosition() const {
    for (int ring = 1;; ++ring) {
        for (int y = -ring; y <= ring; ++y) {
            for (int x = -ring; x <= ring; ++x) {
                if (std::max(std::abs(x), std::abs(y)) != ring) continue;
                const LogicalPoint candidate{
                    static_cast<double>(x) * kDisconnectedSpacing,
                    static_cast<double>(y) * kDisconnectedSpacing};
                if (positionIsFree(candidate)) return candidate;
            }
        }
    }
}

} // namespace basilisk::game
