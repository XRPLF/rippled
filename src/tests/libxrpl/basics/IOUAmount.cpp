#include <xrpl/protocol/IOUAmount.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace xrpl {

TEST(IOUAmountTest, zero)
{
    IOUAmount const z(0, 0);

    EXPECT_EQ(z.mantissa(), 0);
    EXPECT_EQ(z.exponent(), -100);
    EXPECT_FALSE(z);
    EXPECT_EQ(z.signum(), 0);
    EXPECT_EQ(z, beast::kZero);

    EXPECT_EQ((z + z), z);
    EXPECT_EQ((z - z), z);
    EXPECT_EQ(z, -z);

    IOUAmount const zz(beast::kZero);
    EXPECT_EQ(z, zz);

    // https://github.com/XRPLF/rippled/issues/5170
    IOUAmount const zzz{};
    EXPECT_EQ(zzz, beast::kZero);
    // EXPECT_EQ(zzz, zz);
}

TEST(IOUAmountTest, sig_num)
{
    IOUAmount const neg(-1, 0);
    EXPECT_LT(neg.signum(), 0);

    IOUAmount const zer(0, 0);
    EXPECT_EQ(zer.signum(), 0);

    IOUAmount const pos(1, 0);
    EXPECT_GT(pos.signum(), 0);
}

TEST(IOUAmountTest, beast_zero)
{
    using beast::kZero;

    {
        IOUAmount const z(kZero);
        EXPECT_TRUE(z == kZero);
        EXPECT_TRUE(z >= kZero);
        EXPECT_TRUE(z <= kZero);
        EXPECT_FALSE(z != kZero);
        EXPECT_FALSE(z > kZero);
        EXPECT_FALSE(z < kZero);
    }

    {
        IOUAmount const neg(-2, 0);
        EXPECT_TRUE(neg < kZero);
        EXPECT_TRUE(neg <= kZero);
        EXPECT_TRUE(neg != kZero);
        EXPECT_FALSE(neg == kZero);
    }

    {
        IOUAmount const pos(2, 0);
        EXPECT_TRUE(pos > kZero);
        EXPECT_TRUE(pos >= kZero);
        EXPECT_TRUE(pos != kZero);
        EXPECT_FALSE(pos == kZero);
    }
}

TEST(IOUAmountTest, comparisons)
{
    IOUAmount const n(-2, 0);
    IOUAmount const z(0, 0);
    IOUAmount const p(2, 0);
    // For code readability, we want to use general
    // EXPECT_TRUE instead of specific EXPECT_EQ etc.
    EXPECT_TRUE(z == z);
    EXPECT_TRUE(z >= z);
    EXPECT_TRUE(z <= z);
    EXPECT_TRUE(z == -z);
    // NOLINTBEGIN(misc-redundant-expression)
    EXPECT_FALSE(z > z);
    EXPECT_FALSE(z < z);
    EXPECT_FALSE(z != z);
    // NOLINTEND(misc-redundant-expression)
    EXPECT_FALSE(z != -z);

    EXPECT_TRUE(n < z);
    EXPECT_TRUE(n <= z);
    EXPECT_TRUE(n != z);
    EXPECT_FALSE(n > z);
    EXPECT_FALSE(n >= z);
    EXPECT_FALSE(n == z);

    EXPECT_TRUE(p > z);
    EXPECT_TRUE(p >= z);
    EXPECT_TRUE(p != z);
    EXPECT_FALSE(p < z);
    EXPECT_FALSE(p <= z);
    EXPECT_FALSE(p == z);

    EXPECT_TRUE(n < p);
    EXPECT_TRUE(n <= p);
    EXPECT_TRUE(n != p);
    EXPECT_FALSE(n > p);
    EXPECT_FALSE(n >= p);
    EXPECT_FALSE(n == p);

    EXPECT_TRUE(p > n);
    EXPECT_TRUE(p >= n);
    EXPECT_TRUE(p != n);
    EXPECT_FALSE(p < n);
    EXPECT_FALSE(p <= n);
    EXPECT_FALSE(p == n);

    EXPECT_TRUE(p > -p);
    EXPECT_TRUE(p >= -p);
    EXPECT_TRUE(p != -p);

    EXPECT_TRUE(n < -n);
    EXPECT_TRUE(n <= -n);
    EXPECT_TRUE(n != -n);
}

TEST(IOUAmountTest, to_string)
{
    auto test = [](IOUAmount const& n, std::string const& expected) {
        auto const result = to_string(n);
        std::stringstream ss;
        ss << "to_string(" << result << "). Expected: " << expected;
        EXPECT_EQ(result, expected) << ss.str();
    };

    for (auto const mantissaSize : MantissaRange::getAllScales())
    {
        NumberMantissaScaleGuard const mg(mantissaSize);

        test(IOUAmount(-2, 0), "-2");
        test(IOUAmount(0, 0), "0");
        test(IOUAmount(2, 0), "2");
        test(IOUAmount(25, -3), "0.025");
        test(IOUAmount(-25, -3), "-0.025");
        test(IOUAmount(25, 1), "250");
        test(IOUAmount(-25, 1), "-250");
        test(IOUAmount(2, 20), "2e20");
        test(IOUAmount(-2, -20), "-2e-20");
    }
}

TEST(IOUAmountTest, mul_ratio)
{
    /* The range for the mantissa when normalized */
    constexpr std::int64_t kMinMantissa = 1000000000000000ull;
    constexpr std::int64_t kMaxMantissa = 9999999999999999ull;
    // log(2,maxMantissa) ~ 53.15
    /* The range for the exponent when normalized */
    constexpr int kMinExponent = -96;
    constexpr int kMaxExponent = 80;
    constexpr auto kMaxUInt = std::numeric_limits<std::uint32_t>::max();

    {
        // multiply by a number that would overflow the mantissa, then
        // divide by the same number, and check we didn't lose any value
        IOUAmount const bigMan(kMaxMantissa, 0);
        EXPECT_EQ(bigMan, mulRatio(bigMan, kMaxUInt, kMaxUInt, true));
        // rounding mode shouldn't matter as the result is exact
        EXPECT_EQ(bigMan, mulRatio(bigMan, kMaxUInt, kMaxUInt, false));
    }
    {
        // Similar test as above, but for negative values
        IOUAmount const bigMan(-kMaxMantissa, 0);
        EXPECT_EQ(bigMan, mulRatio(bigMan, kMaxUInt, kMaxUInt, true));
        // rounding mode shouldn't matter as the result is exact
        EXPECT_EQ(bigMan, mulRatio(bigMan, kMaxUInt, kMaxUInt, false));
    }

    {
        // small amounts
        IOUAmount const tiny(kMinMantissa, kMinExponent);
        // Round up should give the smallest allowable number
        EXPECT_EQ(tiny, mulRatio(tiny, 1, kMaxUInt, true));
        EXPECT_EQ(tiny, mulRatio(tiny, kMaxUInt - 1, kMaxUInt, true));
        // rounding down should be zero
        EXPECT_EQ(beast::kZero, mulRatio(tiny, 1, kMaxUInt, false));
        EXPECT_EQ(beast::kZero, mulRatio(tiny, kMaxUInt - 1, kMaxUInt, false));

        // tiny negative numbers
        IOUAmount const tinyNeg(-kMinMantissa, kMinExponent);
        // Round up should give zero
        EXPECT_EQ(beast::kZero, mulRatio(tinyNeg, 1, kMaxUInt, true));
        EXPECT_EQ(beast::kZero, mulRatio(tinyNeg, kMaxUInt - 1, kMaxUInt, true));
        // rounding down should be tiny
        EXPECT_EQ(tinyNeg, mulRatio(tinyNeg, 1, kMaxUInt, false));
        EXPECT_EQ(tinyNeg, mulRatio(tinyNeg, kMaxUInt - 1, kMaxUInt, false));
    }

    {  // rounding
        {
            IOUAmount const one(1, 0);
            auto const rup = mulRatio(one, kMaxUInt - 1, kMaxUInt, true);
            auto const rdown = mulRatio(one, kMaxUInt - 1, kMaxUInt, false);
            EXPECT_EQ(rup.mantissa() - rdown.mantissa(), 1);
        }
        {
            IOUAmount const big(kMaxMantissa, kMaxExponent);
            auto const rup = mulRatio(big, kMaxUInt - 1, kMaxUInt, true);
            auto const rdown = mulRatio(big, kMaxUInt - 1, kMaxUInt, false);
            EXPECT_EQ(rup.mantissa() - rdown.mantissa(), 1);
        }

        {
            IOUAmount const negOne(-1, 0);
            auto const rup = mulRatio(negOne, kMaxUInt - 1, kMaxUInt, true);
            auto const rdown = mulRatio(negOne, kMaxUInt - 1, kMaxUInt, false);
            EXPECT_EQ(rup.mantissa() - rdown.mantissa(), 1);
        }
    }

    {
        // division by zero
        IOUAmount const one(1, 0);
        EXPECT_ANY_THROW({ mulRatio(one, 1, 0, true); });
    }

    {
        // overflow
        IOUAmount const big(kMaxMantissa, kMaxExponent);
        EXPECT_ANY_THROW({ mulRatio(big, 2, 0, true); });
    }
}

}  // namespace xrpl
