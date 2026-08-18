#include "basilisk/Random.hpp"

#include <stdexcept>

namespace basilisk {

RandomGenerator::RandomGenerator(std::uint64_t seed)
    : engine_(seed) {}

std::uint64_t RandomGenerator::bounded(std::uint64_t upperExclusive) {
    // Unsigned wraparound makes this 2^64 modulo the bound. Rejecting the
    // shorter prefix leaves an exact multiple of upperExclusive outcomes.
    const std::uint64_t rejectionThreshold =
        (std::uint64_t{0} - upperExclusive) % upperExclusive;
    while (true) {
        const std::uint64_t value = engine_();
        if (value >= rejectionThreshold) return value % upperExclusive;
    }
}

int RandomGenerator::range(int minInclusive, int maxInclusive) {
    if (minInclusive > maxInclusive) {
        throw std::invalid_argument("Random range minimum cannot exceed maximum.");
    }

    const std::int64_t minimum = minInclusive;
    const std::uint64_t width = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(maxInclusive) - minimum) + 1U;
    return static_cast<int>(
        minimum + static_cast<std::int64_t>(bounded(width)));
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

    return bounded(denominator) < numerator;
}

} // namespace basilisk
