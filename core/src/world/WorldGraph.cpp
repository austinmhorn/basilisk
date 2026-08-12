#include "basilisk/world/WorldGraph.hpp"

#include <algorithm>
#include <stdexcept>

namespace basilisk {

void WorldGraph::addCave(CaveId id) {
    caves_.try_emplace(id, Cave{id, {}});
}

void WorldGraph::connect(CaveId a, CaveId b) {
    if (a == b) {
        throw std::invalid_argument("A cave cannot connect to itself.");
    }

    addCave(a);
    addCave(b);

    auto addUnique = [](std::vector<CaveId>& connections, CaveId id) {
        if (std::find(connections.begin(), connections.end(), id) == connections.end()) {
            connections.push_back(id);
        }
    };

    addUnique(caves_.at(a).connections, b);
    addUnique(caves_.at(b).connections, a);
}

bool WorldGraph::contains(CaveId id) const noexcept {
    return caves_.contains(id);
}

bool WorldGraph::areConnected(CaveId a, CaveId b) const {
    const auto it = caves_.find(a);
    if (it == caves_.end()) {
        return false;
    }

    const auto& connections = it->second.connections;
    return std::find(connections.begin(), connections.end(), b) != connections.end();
}

const Cave& WorldGraph::cave(CaveId id) const {
    return caves_.at(id);
}

std::size_t WorldGraph::size() const noexcept {
    return caves_.size();
}

} // namespace basilisk
