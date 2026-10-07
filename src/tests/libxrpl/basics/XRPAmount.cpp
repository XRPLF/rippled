#include <xrpl/protocol/XRPAmount.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SystemParameters.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>

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

TEST(XRPAmountTest, mul_ratio)
{
    constexpr auto kMaxUInt32 = std::numeric_limits<std::uint32_t>::max();
    constexpr auto kMaxXrp = std::numeric_limits<XRPAmount::value_type>::max();
    constexpr auto kMinXrp = std::numeric_limits<XRPAmount::value_type>::min();

    {
        // multiply by a number that would overflow then divide by the same
        // number, and check we didn't lose any value
        XRPAmount big(kMaxXrp);
        EXPECT_EQ(big, mulRatio(big, kMaxUInt32, kMaxUInt32, true));
        // rounding mode shouldn't matter as the result is exact
        EXPECT_EQ(big, mulRatio(big, kMaxUInt32, kMaxUInt32, false));

        // multiply and divide by values that would overflow if done
        // naively, and check that it gives the correct answer
        big -= 0xf;  // Subtract a little so it's divisible by 4
        EXPECT_EQ(mulRatio(big, 3, 4, false).value(), (big.value() / 4) * 3);
        EXPECT_EQ(mulRatio(big, 3, 4, true).value(), (big.value() / 4) * 3);
        EXPECT_EQ(big.value() % 4, 0);
        EXPECT_GT(big.value(), kMaxXrp / 3);
        EXPECT_LE(big.value() / 4, kMaxXrp / 3);
    }

    {
        // Similar test as above, but for negative values
        XRPAmount big(kMinXrp);  // NOLINT TODO
        EXPECT_EQ(big, mulRatio(big, kMaxUInt32, kMaxUInt32, true));
        // rounding mode shouldn't matter as the result is exact
        EXPECT_EQ(big, mulRatio(big, kMaxUInt32, kMaxUInt32, false));

        // multiply and divide by values that would overflow if done
        // naively, and check that it gives the correct answer
        EXPECT_EQ(mulRatio(big, 3, 4, false).value(), (big.value() / 4) * 3);
        EXPECT_EQ(mulRatio(big, 3, 4, true).value(), (big.value() / 4) * 3);
        EXPECT_EQ(big.value() % 4, 0);
        EXPECT_LT(big.value(), kMinXrp / 3);
        EXPECT_GE(big.value() / 4, kMinXrp / 3);
    }

    {
        // small amounts
        XRPAmount const tiny(1);
        // Round up should give the smallest allowable number
        EXPECT_EQ(tiny, mulRatio(tiny, 1, kMaxUInt32, true));
        // rounding down should be zero
        EXPECT_EQ(beast::kZero, mulRatio(tiny, 1, kMaxUInt32, false));
        EXPECT_EQ(beast::kZero, mulRatio(tiny, kMaxUInt32 - 1, kMaxUInt32, false));

        // tiny negative numbers
        XRPAmount const tinyNeg(-1);
        // Round up should give zero
        EXPECT_EQ(beast::kZero, mulRatio(tinyNeg, 1, kMaxUInt32, true));
        EXPECT_EQ(beast::kZero, mulRatio(tinyNeg, kMaxUInt32 - 1, kMaxUInt32, true));
        // rounding down should be tiny
        EXPECT_EQ(tinyNeg, mulRatio(tinyNeg, kMaxUInt32 - 1, kMaxUInt32, false));
    }

    {  // rounding
        {
            XRPAmount const one(1);
            auto const rup = mulRatio(one, kMaxUInt32 - 1, kMaxUInt32, true);
            auto const rdown = mulRatio(one, kMaxUInt32 - 1, kMaxUInt32, false);
            EXPECT_EQ(rup.drops() - rdown.drops(), 1);
        }

        {
            XRPAmount const big(kMaxXrp);
            auto const rup = mulRatio(big, kMaxUInt32 - 1, kMaxUInt32, true);
            auto const rdown = mulRatio(big, kMaxUInt32 - 1, kMaxUInt32, false);
            EXPECT_EQ(rup.drops() - rdown.drops(), 1);
        }

        {
            XRPAmount const negOne(-1);
            auto const rup = mulRatio(negOne, kMaxUInt32 - 1, kMaxUInt32, true);
            auto const rdown = mulRatio(negOne, kMaxUInt32 - 1, kMaxUInt32, false);
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
        XRPAmount const big(kMaxXrp);
        EXPECT_ANY_THROW({ mulRatio(big, 2, 1, true); });
    }

    {
        // underflow
        XRPAmount const bigNegative(kMinXrp + 10);
        EXPECT_EQ(mulRatio(bigNegative, 2, 1, true), kMinXrp);
    }
}

namespace {

constexpr auto kMaxDrops = std::numeric_limits<XRPAmount::value_type>::max();
constexpr auto kMinDrops = std::numeric_limits<XRPAmount::value_type>::min();

Rules
makeRules(bool withFix)
{
    // Rules keeps a reference to its presets, so they must outlive it.
    static std::unordered_set<uint256, beast::Uhash<>> const kWithFix{fixCleanup3_5_0};
    static std::unordered_set<uint256, beast::Uhash<>> const kWithoutFix;
    return Rules{withFix ? kWithFix : kWithoutFix};
}

void
expectBoundariesDoNotThrow()
{
    XRPAmount const max(kMaxDrops);
    XRPAmount const min(kMinDrops);

    EXPECT_EQ(XRPAmount(kMaxDrops - 1) + XRPAmount(1), max);
    EXPECT_EQ(XRPAmount(kMinDrops + 1) - XRPAmount(1), min);
    EXPECT_EQ(max + min, XRPAmount(-1));
    EXPECT_EQ(min - min, XRPAmount(0));

    XRPAmount a(kMaxDrops - 1);
    a += XRPAmount::value_type{1};
    EXPECT_EQ(a, max);
    a -= kMaxDrops;
    EXPECT_EQ(a, XRPAmount(0));

    EXPECT_EQ(XRPAmount(kMaxDrops / 2) * 2, XRPAmount(kMaxDrops - 1));
    EXPECT_EQ(2 * XRPAmount(kMaxDrops / 2), XRPAmount(kMaxDrops - 1));
    EXPECT_EQ(min * 1, min);
    EXPECT_EQ(min * 0, XRPAmount(0));
    EXPECT_EQ(0 * min, XRPAmount(0));
    EXPECT_EQ(max * -1, XRPAmount(-kMaxDrops));
    EXPECT_EQ(XRPAmount(-2) * -3, XRPAmount(6));
    EXPECT_EQ(XRPAmount(3) * -2, XRPAmount(-6));
    EXPECT_EQ(XRPAmount(-3) * 2, XRPAmount(-6));

    XRPAmount b(kMinDrops / 2);
    b *= 2;
    EXPECT_EQ(b, min);

    // The largest legal XRP amounts cannot overflow a single operation
    XRPAmount const maxLegal(kInitialXrp);
    EXPECT_EQ(maxLegal + maxLegal, XRPAmount(2 * kInitialXrp.drops()));
    EXPECT_EQ(XRPAmount(0) - maxLegal - maxLegal, XRPAmount(-2 * kInitialXrp.drops()));
}

void
expectOverflowsThrow()
{
    // += and -= with an XRPAmount leave the lhs unchanged after the throw
    {
        XRPAmount a(kMaxDrops);
        EXPECT_THROW(a += XRPAmount(1), std::overflow_error);
        EXPECT_EQ(a, XRPAmount(kMaxDrops));

        XRPAmount b(kMinDrops);
        EXPECT_THROW(b += XRPAmount(-1), std::overflow_error);
        EXPECT_EQ(b, XRPAmount(kMinDrops));

        XRPAmount c(kMinDrops);
        EXPECT_THROW(c -= XRPAmount(1), std::overflow_error);
        EXPECT_EQ(c, XRPAmount(kMinDrops));

        XRPAmount d(kMaxDrops);
        EXPECT_THROW(d -= XRPAmount(-1), std::overflow_error);
        EXPECT_EQ(d, XRPAmount(kMaxDrops));
    }

    // += and -= with a scalar
    {
        XRPAmount a(kMaxDrops);
        EXPECT_THROW(a += XRPAmount::value_type{1}, std::overflow_error);
        EXPECT_EQ(a, XRPAmount(kMaxDrops));

        XRPAmount b(kMinDrops);
        EXPECT_THROW(b -= XRPAmount::value_type{1}, std::overflow_error);
        EXPECT_EQ(b, XRPAmount(kMinDrops));

        XRPAmount c(kMinDrops);
        EXPECT_THROW(c += XRPAmount::value_type{-1}, std::overflow_error);
        EXPECT_EQ(c, XRPAmount(kMinDrops));

        XRPAmount d(kMaxDrops);
        EXPECT_THROW(d -= XRPAmount::value_type{-1}, std::overflow_error);
        EXPECT_EQ(d, XRPAmount(kMaxDrops));
    }

    // Binary + and - are built from the compound operators
    {
        XRPAmount const max(kMaxDrops);
        EXPECT_THROW((void)(max + XRPAmount(1)), std::overflow_error);
        EXPECT_THROW((void)((XRPAmount(0) - max) - max), std::overflow_error);
    }

    // *= and both orders of binary *
    {
        XRPAmount a(kMaxDrops);
        EXPECT_THROW(a *= 2, std::overflow_error);
        EXPECT_EQ(a, XRPAmount(kMaxDrops));

        XRPAmount b(kMinDrops);
        EXPECT_THROW(b *= -1, std::overflow_error);
        EXPECT_EQ(b, XRPAmount(kMinDrops));

        XRPAmount const half((kMaxDrops / 2) + 1);
        EXPECT_THROW((void)(half * 2), std::overflow_error);
        EXPECT_THROW((void)(2 * half), std::overflow_error);
        EXPECT_THROW((void)(XRPAmount(kMinDrops) * -1), std::overflow_error);
        EXPECT_THROW((void)(-1 * XRPAmount(kMinDrops)), std::overflow_error);
        // Mixed signs
        EXPECT_THROW((void)(XRPAmount(kMaxDrops) * -2), std::overflow_error);
        EXPECT_THROW((void)(XRPAmount(kMinDrops) * 2), std::overflow_error);
    }
}

}  // namespace

TEST(XRPAmountTest, overflow_throws_with_fix)
{
    CurrentTransactionRulesGuard const rg(makeRules(true));
    expectOverflowsThrow();
    expectBoundariesDoNotThrow();
}

// With no rules set (RPC, pathfinding) the operators throw as if the
// amendment were enabled.
TEST(XRPAmountTest, overflow_throws_without_rules)
{
    ASSERT_FALSE(getCurrentTransactionRules());
    expectOverflowsThrow();
    expectBoundariesDoNotThrow();
}

// Without the amendment an overflow keeps the legacy signed arithmetic, which
// is undefined behavior, so only results that do not overflow are checked.
TEST(XRPAmountTest, boundaries_without_fix)
{
    CurrentTransactionRulesGuard const rg(makeRules(false));
    expectBoundariesDoNotThrow();
}

}  // namespace xrpl
