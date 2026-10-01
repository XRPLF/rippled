#pragma once

// Slot for a 128-bit Number prototype.
//
// When <xrpl/basics/Number128.h> exists, CMake defines XRPL_BENCH_NUMBER128
// (see src/benchmarks/libxrpl/CMakeLists.txt) and the type is benchmarked and
// checked by every tool here with no further changes: xrpl.bench.number (single
// operations and ledger formulas) and xrpl.bench.number_diff (rounding
// correctness in all four modes). The header must provide `xrpl::Number128`
// satisfying DecomposableNumber (see NumberLike.h), plus:
//
//   static constexpr unsigned kDigits;  // mantissa precision, e.g. 34 or 38
//
// number_diff rounds exact results onto a plain kDigits-digit grid. Up to 38
// digits that is exact: 10^38 - 1 is below the signed 128-bit maximum, so
// there is no capped region like today's Number has above 2^63 - 1. If the
// prototype's grid differs, extend `Grid` in number_diff/main.cpp.

#ifdef XRPL_BENCH_NUMBER128

#include <xrpl/basics/Number128.h>

#include <benchmarks/libxrpl/number/NumberLike.h>

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define XRPL_BENCH_HAS_NUMBER128 1

namespace xrpl::number_bench {

static_assert(
    DecomposableNumber<Number128>,
    "Number128 must provide Number's interface; see NumberLike.h");
static_assert(Number128::kDigits <= 38, "Decomposed holds at most 38 digits");

}  // namespace xrpl::number_bench

#endif
