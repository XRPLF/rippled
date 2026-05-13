#include <xrpl/protocol/IOUAmount.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace xrpl {

class IOUAmountTest : public ::testing::Test
{
public:
    static void
    testZero()
    {
        IOUAmount const z(0, 0);

        EXPECT_TRUE(z.mantissa() == 0);
        EXPECT_TRUE(z.exponent() == -100);
        EXPECT_TRUE(!z);
        EXPECT_TRUE(z.signum() == 0);
        EXPECT_TRUE(z == beast::kZERO);

        EXPECT_TRUE((z + z) == z);
        EXPECT_TRUE((z - z) == z);
        EXPECT_TRUE(z == -z);

        IOUAmount const zz(beast::kZERO);
        EXPECT_TRUE(z == zz);

        // https://github.com/XRPLF/rippled/issues/5170
        IOUAmount const zzz{};
        EXPECT_TRUE(zzz == beast::kZERO);
        // EXPECT_TRUE(zzz == zz);
    }

    static void
    testSigNum()
    {
        IOUAmount const neg(-1, 0);
        EXPECT_TRUE(neg.signum() < 0);

        IOUAmount const zer(0, 0);
        EXPECT_TRUE(zer.signum() == 0);

        IOUAmount const pos(1, 0);
        EXPECT_TRUE(pos.signum() > 0);
    }

    static void
    testBeastZero()
    {
        using beast::kZERO;

        {
            IOUAmount const z(kZERO);
            EXPECT_TRUE(z == kZERO);
            EXPECT_TRUE(z >= kZERO);
            EXPECT_TRUE(z <= kZERO);
            EXPECT_FALSE(z != kZERO);
            EXPECT_FALSE(z > kZERO);
            EXPECT_FALSE(z < kZERO);
        }

        {
            IOUAmount const neg(-2, 0);
            EXPECT_TRUE(neg < kZERO);
            EXPECT_TRUE(neg <= kZERO);
            EXPECT_TRUE(neg != kZERO);
            EXPECT_FALSE(neg == kZERO);
        }

        {
            IOUAmount const pos(2, 0);
            EXPECT_TRUE(pos > kZERO);
            EXPECT_TRUE(pos >= kZERO);
            EXPECT_TRUE(pos != kZERO);
            EXPECT_FALSE(pos == kZERO);
        }
    }

    static void
    testComparisons()
    {
        IOUAmount const n(-2, 0);
        IOUAmount const z(0, 0);
        IOUAmount const p(2, 0);

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

    static void
    testToString()
    {
        auto test = [](IOUAmount const& n, std::string const& expected) {
            auto const result = to_string(n);
            std::stringstream ss;
            ss << "to_string(" << result << "). Expected: " << expected;
            EXPECT_TRUE(result == expected) << ss.str();
        };

        for (auto const mantissaSize :
             {MantissaRange::MantissaScale::Small, MantissaRange::MantissaScale::Large})
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

    static void
    testMulRatio()
    {
        /* The range for the mantissa when normalized */
        constexpr std::int64_t kMIN_MANTISSA = 1000000000000000ull;
        constexpr std::int64_t kMAX_MANTISSA = 9999999999999999ull;
        // log(2,maxMantissa) ~ 53.15
        /* The range for the exponent when normalized */
        constexpr int kMIN_EXPONENT = -96;
        constexpr int kMAX_EXPONENT = 80;
        constexpr auto kMAX_UINT = std::numeric_limits<std::uint32_t>::max();

        {
            // multiply by a number that would overflow the mantissa, then
            // divide by the same number, and check we didn't lose any value
            IOUAmount const bigMan(kMAX_MANTISSA, 0);
            EXPECT_TRUE(bigMan == mulRatio(bigMan, kMAX_UINT, kMAX_UINT, true));
            // rounding mode shouldn't matter as the result is exact
            EXPECT_TRUE(bigMan == mulRatio(bigMan, kMAX_UINT, kMAX_UINT, false));
        }
        {
            // Similar test as above, but for negative values
            IOUAmount const bigMan(-kMAX_MANTISSA, 0);
            EXPECT_TRUE(bigMan == mulRatio(bigMan, kMAX_UINT, kMAX_UINT, true));
            // rounding mode shouldn't matter as the result is exact
            EXPECT_TRUE(bigMan == mulRatio(bigMan, kMAX_UINT, kMAX_UINT, false));
        }

        {
            // small amounts
            IOUAmount const tiny(kMIN_MANTISSA, kMIN_EXPONENT);
            // Round up should give the smallest allowable number
            EXPECT_TRUE(tiny == mulRatio(tiny, 1, kMAX_UINT, true));
            EXPECT_TRUE(tiny == mulRatio(tiny, kMAX_UINT - 1, kMAX_UINT, true));
            // rounding down should be zero
            EXPECT_TRUE(beast::kZERO == mulRatio(tiny, 1, kMAX_UINT, false));
            EXPECT_TRUE(beast::kZERO == mulRatio(tiny, kMAX_UINT - 1, kMAX_UINT, false));

            // tiny negative numbers
            IOUAmount const tinyNeg(-kMIN_MANTISSA, kMIN_EXPONENT);
            // Round up should give zero
            EXPECT_TRUE(beast::kZERO == mulRatio(tinyNeg, 1, kMAX_UINT, true));
            EXPECT_TRUE(beast::kZERO == mulRatio(tinyNeg, kMAX_UINT - 1, kMAX_UINT, true));
            // rounding down should be tiny
            EXPECT_TRUE(tinyNeg == mulRatio(tinyNeg, 1, kMAX_UINT, false));
            EXPECT_TRUE(tinyNeg == mulRatio(tinyNeg, kMAX_UINT - 1, kMAX_UINT, false));
        }

        {  // rounding
            {
                IOUAmount const one(1, 0);
                auto const rup = mulRatio(one, kMAX_UINT - 1, kMAX_UINT, true);
                auto const rdown = mulRatio(one, kMAX_UINT - 1, kMAX_UINT, false);
                EXPECT_TRUE(rup.mantissa() - rdown.mantissa() == 1);
            }
            {
                IOUAmount const big(kMAX_MANTISSA, kMAX_EXPONENT);
                auto const rup = mulRatio(big, kMAX_UINT - 1, kMAX_UINT, true);
                auto const rdown = mulRatio(big, kMAX_UINT - 1, kMAX_UINT, false);
                EXPECT_TRUE(rup.mantissa() - rdown.mantissa() == 1);
            }

            {
                IOUAmount const negOne(-1, 0);
                auto const rup = mulRatio(negOne, kMAX_UINT - 1, kMAX_UINT, true);
                auto const rdown = mulRatio(negOne, kMAX_UINT - 1, kMAX_UINT, false);
                EXPECT_TRUE(rup.mantissa() - rdown.mantissa() == 1);
            }
        }

        {
            // division by zero
            IOUAmount const one(1, 0);
            EXPECT_ANY_THROW({ mulRatio(one, 1, 0, true); });
        }

        {
            // overflow
            IOUAmount const big(kMAX_MANTISSA, kMAX_EXPONENT);
            EXPECT_ANY_THROW({ mulRatio(big, 2, 0, true); });
        }
    }  // namespace xrpl

    //--------------------------------------------------------------------------

    static void
    run()
    {
        testZero();
        testSigNum();
        testBeastZero();
        testComparisons();
        testToString();
        testMulRatio();
    }
};

TEST_F(IOUAmountTest, iou_amount)
{
    run();
}

}  // namespace xrpl
