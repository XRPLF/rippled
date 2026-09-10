#include <xrpl/protocol/XRPAmount.h>

#include <xrpl/beast/utility/Zero.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace xrpl {

TEST(XRPAmountTest, sig_num)
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

TEST(XRPAmountTest, beast_zero)
{
    using beast::kZero;

    for (auto i : {-1, 0, 1})
    {
        XRPAmount const x(i);

        EXPECT_TRUE((i == 0) == (x == kZero));
        EXPECT_TRUE((i != 0) == (x != kZero));
        EXPECT_TRUE((i < 0) == (x < kZero));
        EXPECT_TRUE((i > 0) == (x > kZero));
        EXPECT_TRUE((i <= 0) == (x <= kZero));
        EXPECT_TRUE((i >= 0) == (x >= kZero));

        EXPECT_TRUE((0 == i) == (kZero == x));
        EXPECT_TRUE((0 != i) == (kZero != x));
        EXPECT_TRUE((0 < i) == (kZero < x));
        EXPECT_TRUE((0 > i) == (kZero > x));
        EXPECT_TRUE((0 <= i) == (kZero <= x));
        EXPECT_TRUE((0 >= i) == (kZero >= x));
    }
}

TEST(XRPAmountTest, comparisons)
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

TEST(XRPAmountTest, add_sub)
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

TEST(XRPAmountTest, decimal)
{
    // Tautology
    EXPECT_EQ(kDropsPerXrp.decimalXRP(), 1);

    XRPAmount test{1};
    EXPECT_EQ(test.decimalXRP(), 0.000001);

    test = -test;
    EXPECT_EQ(test.decimalXRP(), -0.000001);

    test = 100'000'000;
    EXPECT_EQ(test.decimalXRP(), 100);

    test = -test;
    EXPECT_EQ(test.decimalXRP(), -100);
}

TEST(XRPAmountTest, functions)
{
    // Explicitly test every defined function for the XRPAmount class
    // since some of them are templated, but not used anywhere else.
    auto make = [&](auto x) -> XRPAmount { return XRPAmount{x}; };

    XRPAmount const defaulted{};
    (void)defaulted;
    XRPAmount test{0};
    EXPECT_EQ(test.drops(), 0);

    test = make(beast::kZero);
    EXPECT_EQ(test.drops(), 0);

    test = beast::kZero;
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
    EXPECT_FALSE(testOther);
    test = -1;
    testOther = test.dropsAs<std::uint32_t>();
    EXPECT_FALSE(testOther);

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
    EXPECT_FALSE(test);
    EXPECT_EQ(test.signum(), 0);
    test = targetSame;
    EXPECT_EQ(test.signum(), 1);
    EXPECT_EQ(to_string(test), "200");
}

class XRPAmountMulRatioTest : public ::testing::Test
{
protected:
    static constexpr auto kMaxUInt32 = std::numeric_limits<std::uint32_t>::max();
    static constexpr auto kMaxXrp = std::numeric_limits<XRPAmount::value_type>::max();
    static constexpr auto kMinXrp = std::numeric_limits<XRPAmount::value_type>::min();
};

TEST_F(XRPAmountMulRatioTest, scaling_the_maximum_by_one_is_lossless)
{
    // The intermediate product overflows; mulRatio must not lose value anyway.
    XRPAmount const big(kMaxXrp);

    EXPECT_EQ(big, mulRatio(big, kMaxUInt32, kMaxUInt32, true));
    // Rounding mode shouldn't matter as the result is exact.
    EXPECT_EQ(big, mulRatio(big, kMaxUInt32, kMaxUInt32, false));
}

TEST_F(XRPAmountMulRatioTest, three_quarters_of_the_maximum_avoids_overflow)
{
    XRPAmount big(kMaxXrp);
    big -= 0xf;  // Subtract a little so it's divisible by 4

    EXPECT_EQ(mulRatio(big, 3, 4, false).value(), (big.value() / 4) * 3);
    EXPECT_EQ(mulRatio(big, 3, 4, true).value(), (big.value() / 4) * 3);

    // The premises the two checks above rest on: exact division by 4, and an
    // input large enough that a naive multiply by 3 really would overflow.
    EXPECT_EQ(big.value() % 4, 0);
    EXPECT_GT(big.value(), kMaxXrp / 3);
    EXPECT_LE(big.value() / 4, kMaxXrp / 3);
}

TEST_F(XRPAmountMulRatioTest, scaling_the_minimum_by_one_is_lossless)
{
    XRPAmount const big(kMinXrp);  // NOLINT TODO

    EXPECT_EQ(big, mulRatio(big, kMaxUInt32, kMaxUInt32, true));
    // Rounding mode shouldn't matter as the result is exact.
    EXPECT_EQ(big, mulRatio(big, kMaxUInt32, kMaxUInt32, false));
}

TEST_F(XRPAmountMulRatioTest, three_quarters_of_the_minimum_avoids_overflow)
{
    XRPAmount const big(kMinXrp);  // NOLINT TODO

    EXPECT_EQ(mulRatio(big, 3, 4, false).value(), (big.value() / 4) * 3);
    EXPECT_EQ(mulRatio(big, 3, 4, true).value(), (big.value() / 4) * 3);

    EXPECT_EQ(big.value() % 4, 0);
    EXPECT_LT(big.value(), kMinXrp / 3);
    EXPECT_GE(big.value() / 4, kMinXrp / 3);
}

TEST_F(XRPAmountMulRatioTest, a_single_drop_rounds_up_to_a_drop_and_down_to_zero)
{
    XRPAmount const tiny(1);

    EXPECT_EQ(tiny, mulRatio(tiny, 1, kMaxUInt32, true));
    EXPECT_EQ(beast::kZero, mulRatio(tiny, 1, kMaxUInt32, false));
    EXPECT_EQ(beast::kZero, mulRatio(tiny, kMaxUInt32 - 1, kMaxUInt32, false));
}

TEST_F(XRPAmountMulRatioTest, a_single_negative_drop_rounds_up_to_zero_and_down_to_a_drop)
{
    XRPAmount const tinyNeg(-1);

    // "Up" is towards zero for a negative amount.
    EXPECT_EQ(beast::kZero, mulRatio(tinyNeg, 1, kMaxUInt32, true));
    EXPECT_EQ(beast::kZero, mulRatio(tinyNeg, kMaxUInt32 - 1, kMaxUInt32, true));
    EXPECT_EQ(tinyNeg, mulRatio(tinyNeg, kMaxUInt32 - 1, kMaxUInt32, false));
}

TEST_F(XRPAmountMulRatioTest, rounding_up_and_down_a_drop_differ_by_one_drop)
{
    XRPAmount const one(1);

    auto const roundedUp = mulRatio(one, kMaxUInt32 - 1, kMaxUInt32, true);
    auto const roundedDown = mulRatio(one, kMaxUInt32 - 1, kMaxUInt32, false);
    EXPECT_EQ(roundedUp.drops() - roundedDown.drops(), 1);
}

TEST_F(XRPAmountMulRatioTest, rounding_up_and_down_the_maximum_differ_by_one_drop)
{
    XRPAmount const big(kMaxXrp);

    auto const roundedUp = mulRatio(big, kMaxUInt32 - 1, kMaxUInt32, true);
    auto const roundedDown = mulRatio(big, kMaxUInt32 - 1, kMaxUInt32, false);
    EXPECT_EQ(roundedUp.drops() - roundedDown.drops(), 1);
}

TEST_F(XRPAmountMulRatioTest, rounding_up_and_down_a_negative_drop_differ_by_one_drop)
{
    XRPAmount const negOne(-1);

    auto const roundedUp = mulRatio(negOne, kMaxUInt32 - 1, kMaxUInt32, true);
    auto const roundedDown = mulRatio(negOne, kMaxUInt32 - 1, kMaxUInt32, false);
    EXPECT_EQ(roundedUp.drops() - roundedDown.drops(), 1);
}

TEST_F(XRPAmountMulRatioTest, dividing_by_zero_throws)
{
    XRPAmount const one(1);

    EXPECT_ANY_THROW({ mulRatio(one, 1, 0, true); });
}

TEST_F(XRPAmountMulRatioTest, overflowing_the_maximum_throws)
{
    XRPAmount const big(kMaxXrp);

    EXPECT_ANY_THROW({ mulRatio(big, 2, 1, true); });
}

TEST_F(XRPAmountMulRatioTest, underflow_saturates_at_the_minimum)
{
    // Unlike overflow, underflow clamps rather than throwing.
    XRPAmount const bigNegative(kMinXrp + 10);

    EXPECT_EQ(mulRatio(bigNegative, 2, 1, true), kMinXrp);
}

}  // namespace xrpl
