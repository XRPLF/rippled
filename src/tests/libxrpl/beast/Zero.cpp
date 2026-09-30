#include <xrpl/beast/utility/Zero.h>

#include <gtest/gtest.h>

namespace beast {

struct AdlTester
{
};

int
signum(AdlTester)
{
    return 0;
}

namespace inner_adl_test {

struct AdlTester2
{
};

int
signum(AdlTester2)
{
    return 0;
}

}  // namespace inner_adl_test

namespace {

struct IntegerWrapper
{
    int value;

    IntegerWrapper(int v) : value(v)
    {
    }

    [[nodiscard]] int
    signum() const
    {
        return value;
    }
};

void
testLhsZero(IntegerWrapper x)
{
    EXPECT_EQ(x >= kZero, x.signum() >= 0);
    EXPECT_EQ(x > kZero, x.signum() > 0);
    EXPECT_EQ(x == kZero, x.signum() == 0);
    EXPECT_EQ(x != kZero, x.signum() != 0);
    EXPECT_EQ(x < kZero, x.signum() < 0);
    EXPECT_EQ(x <= kZero, x.signum() <= 0);
}

void
testRhsZero(IntegerWrapper x)
{
    EXPECT_EQ(kZero >= x, 0 >= x.signum());
    EXPECT_EQ(kZero > x, 0 > x.signum());
    EXPECT_EQ(kZero == x, 0 == x.signum());
    EXPECT_EQ(kZero != x, 0 != x.signum());
    EXPECT_EQ(kZero < x, 0 < x.signum());
    EXPECT_EQ(kZero <= x, 0 <= x.signum());
}

}  // namespace

TEST(Zero, lhs)
{
    testLhsZero(-7);
    testLhsZero(0);
    testLhsZero(32);
}

TEST(Zero, rhs)
{
    testRhsZero(-4);
    testRhsZero(0);
    testRhsZero(64);
}

TEST(Zero, adl)
{
    EXPECT_TRUE(AdlTester{} == kZero);
    EXPECT_TRUE(inner_adl_test::AdlTester2{} == kZero);
}

}  // namespace beast
