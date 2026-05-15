#include <xrpl/protocol/XRPAmount.h>

#include <xrpl/beast/utility/Zero.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace xrpl {

class XRPAmountTest : public ::testing::Test
{
public:
    static void
    testSigNum()
    {
        for (auto i : {-1, 0, 1})
        {
            XRPAmount const x(i);

            if (i < 0)
            {
                EXPECT_TRUE(x.signum() < 0);
            }
            else if (i > 0)
            {
                EXPECT_TRUE(x.signum() > 0);
            }
            else
            {
                EXPECT_EQ(x.signum(), 0);
            }
        }
    }

    static void
    testBeastZero()
    {
        using beast::kZERO;

        for (auto i : {-1, 0, 1})
        {
            XRPAmount const x(i);

            EXPECT_TRUE((i == 0) == (x == kZERO));
            EXPECT_TRUE((i != 0) == (x != kZERO));
            EXPECT_TRUE((i < 0) == (x < kZERO));
            EXPECT_TRUE((i > 0) == (x > kZERO));
            EXPECT_TRUE((i <= 0) == (x <= kZERO));
            EXPECT_TRUE((i >= 0) == (x >= kZERO));

            EXPECT_TRUE((0 == i) == (kZERO == x));
            EXPECT_TRUE((0 != i) == (kZERO != x));
            EXPECT_TRUE((0 < i) == (kZERO < x));
            EXPECT_TRUE((0 > i) == (kZERO > x));
            EXPECT_TRUE((0 <= i) == (kZERO <= x));
            EXPECT_TRUE((0 >= i) == (kZERO >= x));
        }
    }

    static void
    testComparisons()
    {
        for (auto i : {-1, 0, 1})
        {
            XRPAmount const x(i);

            for (auto j : {-1, 0, 1})
            {
                XRPAmount const y(j);

                EXPECT_EQ((i == j), (x == y));
                EXPECT_EQ((i != j), (x != y));
                EXPECT_EQ((i < j), (x < y));
                EXPECT_EQ((i > j), (x > y));
                EXPECT_EQ((i <= j), (x <= y));
                EXPECT_EQ((i >= j), (x >= y));
            }
        }
    }

    static void
    testAddSub()
    {
        for (auto i : {-1, 0, 1})
        {
            XRPAmount const x(i);

            for (auto j : {-1, 0, 1})
            {
                XRPAmount const y(j);

                EXPECT_EQ(XRPAmount(i + j), (x + y));
                EXPECT_EQ(XRPAmount(i - j), (x - y));

                EXPECT_EQ((x + y), (y + x));  // addition is commutative
            }
        }
    }

    static void
    testDecimal()
    {
        // Tautology
        EXPECT_EQ(kDROPS_PER_XRP.decimalXRP(), 1);

        XRPAmount test{1};
        EXPECT_EQ(test.decimalXRP(), 0.000001);

        test = -test;
        EXPECT_EQ(test.decimalXRP(), -0.000001);

        test = 100'000'000;
        EXPECT_EQ(test.decimalXRP(), 100);

        test = -test;
        EXPECT_EQ(test.decimalXRP(), -100);
    }

    static void
    testFunctions()
    {
        // Explicitly test every defined function for the XRPAmount class
        // since some of them are templated, but not used anywhere else.
        auto make = [&](auto x) -> XRPAmount { return XRPAmount{x}; };

        XRPAmount const defaulted{};
        (void)defaulted;
        XRPAmount test{0};
        EXPECT_EQ(test.drops(), 0);

        test = make(beast::kZERO);
        EXPECT_EQ(test.drops(), 0);

        test = beast::kZERO;
        EXPECT_EQ(test.drops(), 0);

        test = make(100);
        EXPECT_EQ(test.drops(), 100);

        test = make(100u);
        EXPECT_EQ(test.drops(), 100);

        XRPAmount const targetSame{200u};
        test = make(targetSame);
        EXPECT_EQ(test.drops(), 200);
        EXPECT_EQ(test, targetSame);
        EXPECT_TRUE(test < XRPAmount{1000});
        EXPECT_TRUE(test > XRPAmount{100});

        test = std::int64_t(200);
        EXPECT_EQ(test.drops(), 200);
        test = std::uint32_t(300);
        EXPECT_EQ(test.drops(), 300);

        test = targetSame;
        EXPECT_EQ(test.drops(), 200);
        auto testOther = test.dropsAs<std::uint32_t>();
        EXPECT_TRUE(testOther);
        EXPECT_EQ(*testOther, 200);  // NOLINT(bugprone-unchecked-optional-access)
        test = std::numeric_limits<std::uint64_t>::max();
        testOther = test.dropsAs<std::uint32_t>();
        EXPECT_TRUE(!testOther);
        test = -1;
        testOther = test.dropsAs<std::uint32_t>();
        EXPECT_TRUE(!testOther);

        test = targetSame * 2;
        EXPECT_EQ(test.drops(), 400);
        test = 3 * targetSame;
        EXPECT_EQ(test.drops(), 600);
        test = 20;
        EXPECT_EQ(test.drops(), 20);

        test += targetSame;
        EXPECT_EQ(test.drops(), 220);

        test -= targetSame;
        EXPECT_EQ(test.drops(), 20);

        test *= 5;
        EXPECT_EQ(test.drops(), 100);
        test = 50;
        EXPECT_EQ(test.drops(), 50);
        test -= 39;
        EXPECT_EQ(test.drops(), 11);

        // legal with signed
        test = -test;
        EXPECT_EQ(test.drops(), -11);
        EXPECT_EQ(test.signum(), -1);
        EXPECT_EQ(to_string(test), "-11");

        EXPECT_TRUE(test);
        test = 0;
        EXPECT_TRUE(!test);
        EXPECT_EQ(test.signum(), 0);
        test = targetSame;
        EXPECT_EQ(test.signum(), 1);
        EXPECT_EQ(to_string(test), "200");
    }

    static void
    testMulRatio()
    {
        constexpr auto kMAX_UINT32 = std::numeric_limits<std::uint32_t>::max();
        constexpr auto kMAX_XRP = std::numeric_limits<XRPAmount::value_type>::max();
        constexpr auto kMIN_XRP = std::numeric_limits<XRPAmount::value_type>::min();

        {
            // multiply by a number that would overflow then divide by the same
            // number, and check we didn't lose any value
            XRPAmount big(kMAX_XRP);
            EXPECT_EQ(big, mulRatio(big, kMAX_UINT32, kMAX_UINT32, true));
            // rounding mode shouldn't matter as the result is exact
            EXPECT_EQ(big, mulRatio(big, kMAX_UINT32, kMAX_UINT32, false));

            // multiply and divide by values that would overflow if done
            // naively, and check that it gives the correct answer
            big -= 0xf;  // Subtract a little so it's divisible by 4
            EXPECT_EQ(mulRatio(big, 3, 4, false).value(), (big.value() / 4) * 3);
            EXPECT_EQ(mulRatio(big, 3, 4, true).value(), (big.value() / 4) * 3);
            EXPECT_NE((big.value() * 3) / 4, (big.value() / 4) * 3);
        }

        {
            // Similar test as above, but for negative values
            XRPAmount big(kMIN_XRP);  // NOLINT TODO
            EXPECT_EQ(big, mulRatio(big, kMAX_UINT32, kMAX_UINT32, true));
            // rounding mode shouldn't matter as the result is exact
            EXPECT_EQ(big, mulRatio(big, kMAX_UINT32, kMAX_UINT32, false));

            // multiply and divide by values that would overflow if done
            // naively, and check that it gives the correct answer
            EXPECT_EQ(mulRatio(big, 3, 4, false).value(), (big.value() / 4) * 3);
            EXPECT_EQ(mulRatio(big, 3, 4, true).value(), (big.value() / 4) * 3);
            EXPECT_NE((big.value() * 3) / 4, (big.value() / 4) * 3);
        }

        {
            // small amounts
            XRPAmount const tiny(1);
            // Round up should give the smallest allowable number
            EXPECT_EQ(tiny, mulRatio(tiny, 1, kMAX_UINT32, true));
            // rounding down should be zero
            EXPECT_EQ(beast::kZERO, mulRatio(tiny, 1, kMAX_UINT32, false));
            EXPECT_EQ(beast::kZERO, mulRatio(tiny, kMAX_UINT32 - 1, kMAX_UINT32, false));

            // tiny negative numbers
            XRPAmount const tinyNeg(-1);
            // Round up should give zero
            EXPECT_EQ(beast::kZERO, mulRatio(tinyNeg, 1, kMAX_UINT32, true));
            EXPECT_EQ(beast::kZERO, mulRatio(tinyNeg, kMAX_UINT32 - 1, kMAX_UINT32, true));
            // rounding down should be tiny
            EXPECT_EQ(tinyNeg, mulRatio(tinyNeg, kMAX_UINT32 - 1, kMAX_UINT32, false));
        }

        {  // rounding
            {
                XRPAmount const one(1);
                auto const rup = mulRatio(one, kMAX_UINT32 - 1, kMAX_UINT32, true);
                auto const rdown = mulRatio(one, kMAX_UINT32 - 1, kMAX_UINT32, false);
                EXPECT_EQ(rup.drops() - rdown.drops(), 1);
            }

            {
                XRPAmount const big(kMAX_XRP);
                auto const rup = mulRatio(big, kMAX_UINT32 - 1, kMAX_UINT32, true);
                auto const rdown = mulRatio(big, kMAX_UINT32 - 1, kMAX_UINT32, false);
                EXPECT_EQ(rup.drops() - rdown.drops(), 1);
            }

            {
                XRPAmount const negOne(-1);
                auto const rup = mulRatio(negOne, kMAX_UINT32 - 1, kMAX_UINT32, true);
                auto const rdown = mulRatio(negOne, kMAX_UINT32 - 1, kMAX_UINT32, false);
                EXPECT_EQ(rup.drops() - rdown.drops(), 1);
            }
        }

        {
            // division by zero
            XRPAmount const one(1);
            EXPECT_ANY_THROW({ mulRatio(one, 1, 0, true); });
        }

        {
            // overflow
            XRPAmount const big(kMAX_XRP);
            EXPECT_ANY_THROW({ mulRatio(big, 2, 1, true); });
        }

        {
            // underflow
            XRPAmount const bigNegative(kMIN_XRP + 10);
            EXPECT_EQ(mulRatio(bigNegative, 2, 1, true), kMIN_XRP);
        }
    }  // namespace xrpl

    //--------------------------------------------------------------------------
};

TEST_F(XRPAmountTest, sig_num)
{
    testSigNum();
}

TEST_F(XRPAmountTest, beast_zero)
{
    testBeastZero();
}

TEST_F(XRPAmountTest, comparisons)
{
    testComparisons();
}

TEST_F(XRPAmountTest, add_sub)
{
    testAddSub();
}

TEST_F(XRPAmountTest, decimal)
{
    testDecimal();
}

TEST_F(XRPAmountTest, functions)
{
    testFunctions();
}

TEST_F(XRPAmountTest, mul_ratio)
{
    testMulRatio();
}

}  // namespace xrpl
