#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "basilisk/items/Item.hpp"

namespace basilisk {

struct Inventory {
    std::vector<ItemInstance> items;

    [[nodiscard]] bool full(std::size_t capacity) const noexcept {
        return items.size() >= capacity;
    }

    [[nodiscard]] bool add(ItemInstance item, std::size_t capacity) {
        if (full(capacity)) {
            return false;
        }

        items.push_back(item);
        return true;
    }

    [[nodiscard]] bool contains(ItemType type) const noexcept {
        return std::any_of(items.begin(), items.end(), [type](const ItemInstance& item) {
            return item.type == type;
        });
    }

    [[nodiscard]] bool removeOne(ItemType type) {
        const auto it = std::find_if(items.begin(), items.end(), [type](const ItemInstance& item) {
            return item.type == type;
        });

        if (it == items.end()) {
            return false;
        }

        items.erase(it);
        return true;
    }
};

} // namespace basilisk
