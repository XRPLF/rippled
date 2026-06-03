#include <xrpl/basics/Number.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <utility>

using namespace xrpl;

namespace {

// The IOUAmount mantissa range: [10^15, 10^16 - 1]. Kept here as signed
// constants so the default template parameter T resolves to std::int64_t,
// matching IOUAmount's own use of Number::normalizeToRange.
constexpr std::int64_t kMin = 1'000'000'000'000'000;
constexpr std::int64_t kMax = (kMin * 10) - 1;

// The two-pass path that the static primitive replaces: build a Number (one
// normalize pass to the default range) and then re-normalize to the narrow IOU
// range via the const member overload (a second pass).
std::pair<std::int64_t, int>
twoPass(std::int64_t mantissa, int exponent)
{
    Number const v{mantissa, exponent};
    return v.normalizeToRange<kMin, kMax>();
}

// The single-pass static primitive under test.
std::pair<std::int64_t, int>
onePass(std::int64_t mantissa, int exponent)
{
    return Number::normalizeToRange<kMin, kMax>(mantissa, exponent);
}

}  // namespace

// The static primitive must produce bit-identical (mantissa, exponent) to the
// old two-pass path across a broad sweep of inputs: values needing scale-up,
// scale-down, rounding cusps, negatives, and exponent extremes.
TEST(Number, normalizeToRangeEquivalence)
{
    // A spread of mantissa magnitudes: tiny (heavy scale-up), mid, at the IOU
    // floor/ceiling, beyond it (scale-down), and int64 extremes.
    std::int64_t const mantissas[] = {
        1,
        2,
        7,
        9,
        99,
        100,
        12345,
        999'999'999'999'999,
        kMin,
        kMin + 1,
        kMax,
        kMax + 1,
        1'234'567'890'123'456,
        12'345'678'901'234'567,
        std::numeric_limits<std::int64_t>::max(),
        std::numeric_limits<std::int64_t>::max() - 1,
    };

    for (std::int64_t const absM : mantissas)
    {
        for (std::int64_t const m : {absM, -absM})
        {
            for (int const e : {-90, -32, -1, 0, 1, 5, 32, 70})
            {
                auto const expected = twoPass(m, e);
                auto const actual = onePass(m, e);
                EXPECT_EQ(actual.first, expected.first)
                    << "mantissa mismatch for m=" << m << " e=" << e;
                EXPECT_EQ(actual.second, expected.second)
                    << "exponent mismatch for m=" << m << " e=" << e;
            }
        }
    }

    // int64::min cannot be negated naively; externalToInternal handles it. Make
    // sure the static path agrees with the two-pass path on it too.
    {
        std::int64_t const m = std::numeric_limits<std::int64_t>::min();
        auto const expected = twoPass(m, 0);
        auto const actual = onePass(m, 0);
        EXPECT_EQ(actual.first, expected.first);
        EXPECT_EQ(actual.second, expected.second);
    }
}

// Exact, hand-computed results (state + cause), not just "equals the old path".
TEST(Number, normalizeToRangeExactValues)
{
    // A single digit scales up by 15 powers of ten to reach the floor 10^15,
    // with the exponent dropping by the same 15.
    {
        auto const [m, e] = onePass(1, 0);
        EXPECT_EQ(m, kMin);  // 1'000'000'000'000'000
        EXPECT_EQ(e, -15);
    }
    // Already exactly at the floor: unchanged.
    {
        auto const [m, e] = onePass(kMin, 4);
        EXPECT_EQ(m, kMin);
        EXPECT_EQ(e, 4);
    }
    // Already exactly at the ceiling: unchanged.
    {
        auto const [m, e] = onePass(kMax, -7);
        EXPECT_EQ(m, kMax);  // 9'999'999'999'999'999
        EXPECT_EQ(e, -7);
    }
    // One past the ceiling scales down by one power of ten; the dropped ones
    // digit (0) truncates cleanly and the exponent rises by one.
    {
        auto const [m, e] = onePass(kMax + 1, 0);  // 10'000'000'000'000'000
        EXPECT_EQ(m, kMin);                        // 1'000'000'000'000'000
        EXPECT_EQ(e, 1);
    }
    // Negative values keep their sign through normalization.
    {
        auto const [m, e] = onePass(-5, 0);
        EXPECT_EQ(m, -5 * kMin);  // -5'000'000'000'000'000
        EXPECT_EQ(e, -15);
    }
    // Zero mantissa: the workhorse leaves it as zero (callers special-case it).
    {
        auto const [m, e] = onePass(0, 0);
        EXPECT_EQ(m, 0);
    }
}

// Equivalence must hold under every rounding mode, not just the default
// ToNearest. This is the subtlest risk: the single-pass impl hardcodes
// CuspRoundingFix::Disabled, whereas the old two-pass path ran an intermediate
// normalize to the wider range first. Sweep all four modes, including inputs
// that round at a tie (a trailing digit of exactly 5 when scaling down).
TEST(Number, normalizeToRangeAllRoundingModes)
{
    // Inputs chosen so scale-down drops a non-zero (and tie) trailing digit.
    std::int64_t const mantissas[] = {
        15,
        25,
        12'345'678'901'234'565,  // 17 digits, trailing 5 -> tie on the drop
        99'999'999'999'999'995,
        kMax + 5,
        std::numeric_limits<std::int64_t>::max(),
    };

    for (auto mode :
         {Number::RoundingMode::ToNearest,
          Number::RoundingMode::TowardsZero,
          Number::RoundingMode::Downward,
          Number::RoundingMode::Upward})
    {
        for (std::int64_t const absM : mantissas)
        {
            for (std::int64_t const m : {absM, -absM})
            {
                for (int const e : {-20, 0, 13})
                {
                    NumberRoundModeGuard const g(mode);
                    auto const expected = twoPass(m, e);
                    auto const actual = onePass(m, e);
                    EXPECT_EQ(actual.first, expected.first)
                        << "mantissa mismatch: mode=" << static_cast<int>(mode) << " m=" << m
                        << " e=" << e;
                    EXPECT_EQ(actual.second, expected.second)
                        << "exponent mismatch: mode=" << static_cast<int>(mode) << " m=" << m
                        << " e=" << e;
                }
            }
        }
    }
}

// The refactored const member overload must forward to the static primitive
// and yield identical results for the same Number.
TEST(Number, normalizeToRangeMemberStaticConsistency)
{
    std::int64_t const mantissas[] = {3, 42, kMin, kMin + 7, kMax, kMax + 1, 1'234'567'890'123'456};

    for (std::int64_t const absM : mantissas)
    {
        for (std::int64_t const m : {absM, -absM})
        {
            for (int const e : {-50, -3, 0, 11, 60})
            {
                Number const v{m, e};
                auto const viaMember = v.normalizeToRange<kMin, kMax>();
                // Feed the static the raw inputs that built the Number.
                auto const viaStatic = Number::normalizeToRange<kMin, kMax>(m, e);
                EXPECT_EQ(viaMember.first, viaStatic.first) << "m=" << m << " e=" << e;
                EXPECT_EQ(viaMember.second, viaStatic.second) << "m=" << m << " e=" << e;
            }
        }
    }
}
