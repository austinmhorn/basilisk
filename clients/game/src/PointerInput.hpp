#pragma once

#include "MapPresentation.hpp"

#include <utility>

namespace basilisk::game {

template <typename Converter>
[[nodiscard]] bool tryGameplayPointerCoordinates(
    float windowX,
    float windowY,
    PresentationPoint& point,
    Converter&& converter) {
    float renderX = 0.0F;
    float renderY = 0.0F;
    if (!std::forward<Converter>(converter)(
            windowX, windowY, renderX, renderY)) {
        return false;
    }
    point = {renderX, renderY};
    return true;
}

} // namespace basilisk::game
