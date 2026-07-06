#pragma once

#include <cstdint>
#include <random>

namespace xrpl::test {

/** Return a pseudo-random engine seeded with the given value.

    Each test should call this with its own fixed seed so that results are
    fully reproducible when a test is retried in isolation, regardless of the
    order in which other tests ran.
*/
inline std::mt19937
rng(std::uint32_t seed)
{
    return std::mt19937{seed};
}

}  // namespace xrpl::test
