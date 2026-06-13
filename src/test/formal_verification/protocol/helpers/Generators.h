#pragma once

#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/protocol/helpers/Converters.h>

#include <xrpl/basics/Number.h>

#include <cstdint>
#include <random>

namespace xrpl::test::formal_verification {

struct Pair
{
    Number cppNum;
    LeanNumber leanNum;
};

inline Pair
makePair(bool negative, uint64_t mantissa, int exponent)
{
    return {
        Number{negative, mantissa, exponent, Number::Unchecked{}},
        LeanNumber{negative, mantissa, static_cast<uint64_t>(exponent)}};
}

inline Pair
randomPair(uint64_t mantMin, uint64_t mantMax, int expMin, int expMax)
{
    auto& rng = nextRng();
    std::uniform_int_distribution<uint64_t> mantDist(mantMin, mantMax);
    std::uniform_int_distribution<int> expDist(expMin, expMax);
    std::bernoulli_distribution signDist(0.5);
    return makePair(signDist(rng), mantDist(rng), expDist(rng));
}

// Normalized mantissa range, caller-chosen exponent range.
inline Pair
randomPair(int expMin, int expMax)
{
    return randomPair(Number::minMantissa(), Number::kMaxRep, expMin, expMax);
}

}  // namespace xrpl::test::formal_verification
