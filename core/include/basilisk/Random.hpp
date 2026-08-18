#pragma once

#include <cstdint>
#include <random>

namespace basilisk {

class RandomGenerator {
public:
    explicit RandomGenerator(std::uint64_t seed);

    [[nodiscard]] int range(int minInclusive, int maxInclusive);
    [[nodiscard]] bool chance(std::uint32_t numerator, std::uint32_t denominator);

private:
    [[nodiscard]] std::uint64_t bounded(std::uint64_t upperExclusive);

    std::mt19937_64 engine_;
};

} // namespace basilisk
