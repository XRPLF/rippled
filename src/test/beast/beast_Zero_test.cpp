#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>

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

class Zero_test : public beast::unit_test::Suite
{
private:
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

public:
    void
    expectSame(bool result, bool correct, char const* message)
    {
        expect(result == correct, message);
    }

    void
    testLhsZero(IntegerWrapper x)
    {
        expectSame(x >= kZero, x.signum() >= 0, "lhs greater-than-or-equal-to");
        expectSame(x > kZero, x.signum() > 0, "lhs greater than");
        expectSame(x == kZero, x.signum() == 0, "lhs equal to");
        expectSame(x != kZero, x.signum() != 0, "lhs not equal to");
        expectSame(x < kZero, x.signum() < 0, "lhs less than");
        expectSame(x <= kZero, x.signum() <= 0, "lhs less-than-or-equal-to");
    }

    void
    testLhsZero()
    {
        testcase("lhs zero");

        testLhsZero(-7);
        testLhsZero(0);
        testLhsZero(32);
    }

    void
    testRhsZero(IntegerWrapper x)
    {
        expectSame(kZero >= x, 0 >= x.signum(), "rhs greater-than-or-equal-to");
        expectSame(kZero > x, 0 > x.signum(), "rhs greater than");
        expectSame(kZero == x, 0 == x.signum(), "rhs equal to");
        expectSame(kZero != x, 0 != x.signum(), "rhs not equal to");
        expectSame(kZero < x, 0 < x.signum(), "rhs less than");
        expectSame(kZero <= x, 0 <= x.signum(), "rhs less-than-or-equal-to");
    }

    void
    testRhsZero()
    {
        testcase("rhs zero");

        testRhsZero(-4);
        testRhsZero(0);
        testRhsZero(64);
    }

    void
    testAdl()
    {
        expect(AdlTester{} == kZero, "ADL failure!");
        expect(inner_adl_test::AdlTester2{} == kZero, "ADL failure!");
    }

    void
    run() override
    {
        testLhsZero();
        testRhsZero();
        testAdl();
    }
};

BEAST_DEFINE_TESTSUITE(Zero, beast, beast);

}  // namespace beast
