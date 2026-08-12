#include "basilisk/Random.hpp"

#include <stdexcept>

namespace basilisk {

RandomGenerator::RandomGenerator(std::uint64_t seed)
    : engine_(seed) {}

int RandomGenerator::range(int minInclusive, int maxInclusive) {
    if (minInclusive > maxInclusive) {
        throw std::invalid_argument("Random range minimum cannot exceed maximum.");
    }

    std::uniform_int_distribution<int> distribution(minInclusive, maxInclusive);
    return distribution(engine_);
}

bool RandomGenerator::chance(std::uint32_t numerator, std::uint32_t denominator) {
    if (denominator == 0 || numerator > denominator) {
        throw std::invalid_argument("Invalid probability fraction.");
    }

    if (numerator == 0) {
        return false;
    }

    if (numerator == denominator) {
        return true;
    }

    std::uniform_int_distribution<std::uint32_t> distribution(1, denominator);
    return distribution(engine_) <= numerator;
}

} // namespace basilisk
