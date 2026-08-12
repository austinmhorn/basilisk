#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "basilisk/Types.hpp"
#include "basilisk/world/Cave.hpp"

namespace basilisk {

class WorldGraph {
public:
    void addCave(CaveId id);
    void connect(CaveId a, CaveId b);

    [[nodiscard]] bool contains(CaveId id) const noexcept;
    [[nodiscard]] bool areConnected(CaveId a, CaveId b) const;
    [[nodiscard]] const Cave& cave(CaveId id) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<CaveId, Cave> caves_;
};

} // namespace basilisk
