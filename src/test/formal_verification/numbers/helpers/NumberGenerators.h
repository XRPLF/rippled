#pragma once

#include <test/formal_verification/common/LeanSuite.h>
#include <test/formal_verification/numbers/helpers/NumberTypes.h>

#include <xrpl/basics/Number.h>

#include <cstdint>
#include <random>

namespace xrpl::test::formal_verification {

// The real C++ Number and the Lean FFI fields wrapper.
struct NumberPair
{
    Number cppNum;
    LeanNumber leanNum;
};

// Construct NumberPair on both side using same args
inline NumberPair
makeNumberPair(bool negative, uint64_t mantissa, int exponent)
{
    return {
        Number{negative, mantissa, exponent, Number::Unchecked{}},
        LeanNumber{negative, mantissa, static_cast<uint64_t>(exponent)}};
}

// Construct random NumberPair (mantissa and exponent taken uniformly from the given bounds)
inline NumberPair
randomNumberPair(uint64_t mantMin, uint64_t mantMax, int expMin, int expMax)
{
    auto& rng = nextRng();
    std::uniform_int_distribution<uint64_t> mantDist(mantMin, mantMax);
    std::uniform_int_distribution<int> expDist(expMin, expMax);
    std::bernoulli_distribution signDist(0.5);
    return makeNumberPair(signDist(rng), mantDist(rng), expDist(rng));
}

// Construct random NumberPair where mantissa goes from minMantissa up to kMaxRep
inline NumberPair
randomNumberPair(int expMin, int expMax)
{
    return randomNumberPair(Number::minMantissa(), Number::kMaxRep, expMin, expMax);
}

}  // namespace xrpl::test::formal_verification
