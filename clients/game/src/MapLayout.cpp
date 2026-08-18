#include "MapLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <vector>

namespace basilisk::game {
namespace {

constexpr double kCaveSpacing = 4.0;
constexpr double kStubLength = 1.5;
// Reject locally crowded candidates without uniformly scaling the layout.
// Only colliding placements move farther along their deterministic exit ray.
constexpr double kMinimumCaveSeparation = 2.75;
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

struct LayoutEdge {
    CaveId first{};
    CaveId second{};
};

std::vector<LayoutEdge> layoutEdges(
    const PlayerMapView& map,
    const std::map<CaveId, LogicalPoint>& positions) {

    std::vector<LayoutEdge> edges;
    for (const DiscoveredCaveView& cave : map.caves) {
        if (!positions.contains(cave.cave)) continue;
        for (const TunnelView& exit : cave.exits) {
            if (!exit.destination.has_value() ||
                cave.cave >= *exit.destination ||
                !positions.contains(*exit.destination)) {
                continue;
            }
            edges.push_back(LayoutEdge{cave.cave, *exit.destination});
        }
    }
    std::sort(edges.begin(), edges.end(), [](const LayoutEdge& a, const LayoutEdge& b) {
        return a.first != b.first ? a.first < b.first : a.second < b.second;
    });
    edges.erase(
        std::unique(
            edges.begin(),
            edges.end(),
            [](const LayoutEdge& a, const LayoutEdge& b) {
                return a.first == b.first && a.second == b.second;
            }),
        edges.end());
    return edges;
}

LogicalBounds positionBounds(const std::map<CaveId, LogicalPoint>& positions) {
    LogicalBounds bounds;
    for (const auto& [cave, position] : positions) {
        (void)cave;
        include(bounds, position);
    }
    return bounds;
}

double pointSegmentDistance(
    LogicalPoint point,
    LogicalPoint start,
    LogicalPoint end) {

    const double lengthSquared = distanceSquared(start, end);
    if (lengthSquared <= 0.000001) {
        return std::sqrt(distanceSquared(point, start));
    }
    const double segmentX = end.x - start.x;
    const double segmentY = end.y - start.y;
    const double projection = std::clamp(
        ((point.x - start.x) * segmentX +
         (point.y - start.y) * segmentY) / lengthSquared,
        0.0,
        1.0);
    const LogicalPoint closest{
        start.x + segmentX * projection,
        start.y + segmentY * projection};
    return std::sqrt(distanceSquared(point, closest));
}

double orientation(LogicalPoint a, LogicalPoint b, LogicalPoint c) {
    return (b.x - a.x) * (c.y - a.y) -
        (b.y - a.y) * (c.x - a.x);
}

bool edgesCross(
    LogicalPoint firstStart,
    LogicalPoint firstEnd,
    LogicalPoint secondStart,
    LogicalPoint secondEnd) {

    constexpr double epsilon = 0.000001;
    const double a = orientation(firstStart, firstEnd, secondStart);
    const double b = orientation(firstStart, firstEnd, secondEnd);
    const double c = orientation(secondStart, secondEnd, firstStart);
    const double d = orientation(secondStart, secondEnd, firstEnd);
    return a * b < -epsilon && c * d < -epsilon;
}

double layoutScore(
    const std::map<CaveId, LogicalPoint>& positions,
    const std::vector<LayoutEdge>& edges,
    double targetAspect) {

    constexpr double desiredNodeSeparation = 4.9;
    constexpr double hardNodeSeparation = 4.15;
    constexpr double desiredNodeEdgeClearance = 2.25;
    constexpr double idealEdgeLength = 7.0;
    double score = 0.0;

    for (auto first = positions.begin(); first != positions.end(); ++first) {
        for (auto second = std::next(first); second != positions.end(); ++second) {
            const double distance = std::sqrt(
                distanceSquared(first->second, second->second));
            if (distance < hardNodeSeparation) {
                const double shortfall = hardNodeSeparation - distance;
                score += shortfall * shortfall * 12000.0;
            }
            if (distance < desiredNodeSeparation) {
                const double shortfall = desiredNodeSeparation - distance;
                score += shortfall * shortfall * 18.0;
            }
        }
    }

    for (const LayoutEdge& edge : edges) {
        const LogicalPoint start = positions.at(edge.first);
        const LogicalPoint end = positions.at(edge.second);
        const double length = std::sqrt(distanceSquared(start, end));
        const double stretch = length - idealEdgeLength;
        score += stretch * stretch * 0.35;
        if (length > 14.0) {
            const double excess = length - 14.0;
            score += excess * excess * 6.0;
        }
        if (length > 25.0) {
            const double excess = length - 25.0;
            score += excess * excess * 1500.0;
        }

        for (const auto& [cave, position] : positions) {
            if (cave == edge.first || cave == edge.second) continue;
            const double clearance = pointSegmentDistance(position, start, end);
            if (clearance < desiredNodeEdgeClearance) {
                const double shortfall = desiredNodeEdgeClearance - clearance;
                score += shortfall * shortfall * 3000.0;
            }
        }
    }

    for (std::size_t first = 0; first < edges.size(); ++first) {
        for (std::size_t second = first + 1; second < edges.size(); ++second) {
            const LayoutEdge& a = edges[first];
            const LayoutEdge& b = edges[second];
            if (a.first == b.first || a.first == b.second ||
                a.second == b.first || a.second == b.second) {
                continue;
            }
            if (edgesCross(
                    positions.at(a.first),
                    positions.at(a.second),
                    positions.at(b.first),
                    positions.at(b.second))) {
                score += 2000.0;
            }
        }
    }

    const LogicalBounds bounds = positionBounds(positions);
    const double width = bounds.maximumX - bounds.minimumX;
    const double height = bounds.maximumY - bounds.minimumY;
    if (width > 0.0 && height > 0.0 && targetAspect > 0.0) {
        const double aspectError = std::log((width / height) / targetAspect);
        score += aspectError * aspectError * 2500.0;
    }
    return score;
}

void expandHorizontally(
    std::map<CaveId, LogicalPoint>& positions,
    double targetAspect,
    double maximumWidth) {

    const LogicalBounds bounds = positionBounds(positions);
    const double width = bounds.maximumX - bounds.minimumX;
    const double height = bounds.maximumY - bounds.minimumY;
    if (width <= 0.0 || height <= 0.0 || targetAspect <= 0.0) return;
    const double desiredWidth = std::min(maximumWidth, targetAspect * height);
    if (width >= desiredWidth) return;

    const double scale = desiredWidth / width;
    const double centerX = (bounds.minimumX + bounds.maximumX) * 0.5;
    for (auto& [cave, position] : positions) {
        (void)cave;
        position.x = centerX + (position.x - centerX) * scale;
    }
}

} // namespace

void PlayerMapLayout::update(const PlayerMapView& map) {
    temporarilyRevealedCavePositions_.clear();
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

void PlayerMapLayout::finalizeFullLayout(const PlayerMapView& fullMap) {
    constexpr double targetAspect = 1.4;
    constexpr double maximumHorizontalScale = 3.0;
    finalizeFullLayout(fullMap, targetAspect, maximumHorizontalScale);
}

void PlayerMapLayout::finalizeFullLayout(
    const PlayerMapView& fullMap,
    double targetAspect,
    double maximumHorizontalScale) {

    if (fixedBounds_.has_value() || cavePositions_.size() < 2 ||
        targetAspect <= 0.0 ||
        maximumHorizontalScale <= 1.0) {
        return;
    }

    const LogicalBounds bounds = positionedBounds();
    const double width = bounds.maximumX - bounds.minimumX;
    const double height = bounds.maximumY - bounds.minimumY;
    if (width <= 0.0 || height <= 0.0) return;

    const double maximumWidth = width * maximumHorizontalScale;
    expandHorizontally(cavePositions_, targetAspect, maximumWidth);
    const std::vector<LayoutEdge> edges = layoutEdges(fullMap, cavePositions_);

    std::vector<CaveId> caveIds;
    caveIds.reserve(cavePositions_.size());
    for (const auto& [cave, position] : cavePositions_) {
        (void)position;
        caveIds.push_back(cave);
    }
    for (int pass = 0; pass < 12; ++pass) {
        double bestScore = layoutScore(cavePositions_, edges, targetAspect);
        std::optional<std::pair<CaveId, CaveId>> bestSwap;
        for (std::size_t first = 0; first < caveIds.size(); ++first) {
            for (std::size_t second = first + 1; second < caveIds.size(); ++second) {
                std::swap(
                    cavePositions_.at(caveIds[first]),
                    cavePositions_.at(caveIds[second]));
                const double candidateScore = layoutScore(
                    cavePositions_, edges, targetAspect);
                std::swap(
                    cavePositions_.at(caveIds[first]),
                    cavePositions_.at(caveIds[second]));
                if (candidateScore + 0.000001 < bestScore) {
                    bestScore = candidateScore;
                    bestSwap = std::pair{caveIds[first], caveIds[second]};
                }
            }
        }
        if (!bestSwap.has_value()) break;
        std::swap(
            cavePositions_.at(bestSwap->first),
            cavePositions_.at(bestSwap->second));
    }

    constexpr std::array<double, 5> steps{4.5, 3.0, 1.5, 0.75, 0.375};
    for (const double step : steps) {
        for (int pass = 0; pass < 4; ++pass) {
            bool moved = false;
            for (auto& [cave, position] : cavePositions_) {
                const LogicalPoint original = position;
                LogicalPoint best = original;
                double bestScore = layoutScore(
                    cavePositions_, edges, targetAspect);
                for (const LogicalPoint direction : kDirections) {
                    position = LogicalPoint{
                        original.x + direction.x * step,
                        original.y + direction.y * step};
                    const double candidateScore = layoutScore(
                        cavePositions_, edges, targetAspect);
                    if (candidateScore + 0.000001 < bestScore) {
                        bestScore = candidateScore;
                        best = position;
                    }
                }
                position = best;
                moved = moved || best != original;
            }
            if (!moved) break;
        }
    }
    expandHorizontally(cavePositions_, targetAspect, maximumWidth);
}

void PlayerMapLayout::updateFixed(const PlayerFixedMapGeometry& geometry) {
    cavePositions_ = geometry.discoveredCaves;
    fixedExitEndpoints_ = geometry.unknownExitEndpoints;
    temporarilyRevealedCavePositions_ = geometry.temporarilyRevealedCaves;
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

std::optional<LogicalPoint> PlayerMapLayout::temporarilyRevealedCavePosition(
    CaveId cave) const {

    const auto found = temporarilyRevealedCavePositions_.find(cave);
    if (found == temporarilyRevealedCavePositions_.end()) return std::nullopt;
    return found->second;
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
