#include <xrpl/beast/unit_test.h>
#include <xrpl/beast/utility/Zero.h>

namespace beast {

struct adl_tester
{
};

int
signum(adl_tester)
{
    return 0;
}

namespace inner_adl_test {

struct adl_tester2
{
};

int
signum(adl_tester2)
{
    return 0;
}

}  // namespace inner_adl_test

class Zero_test : public beast::unit_test::suite
{
private:
    struct IntegerWrapper
    {
        int value;

        IntegerWrapper(int v) : value(v)
        {
        }

        int
        signum() const
        {
            return value;
        }
    };

public:
    void
    expect_same(bool result, bool correct, char const* message)
    {
        expect(result == correct, message);
    }

    void
    test_lhs_zero(IntegerWrapper x)
    {
        expect_same(x >= zero, x.signum() >= 0, "lhs greater-than-or-equal-to");
        expect_same(x > zero, x.signum() > 0, "lhs greater than");
        expect_same(x == zero, x.signum() == 0, "lhs equal to");
        expect_same(x != zero, x.signum() != 0, "lhs not equal to");
        expect_same(x < zero, x.signum() < 0, "lhs less than");
        expect_same(x <= zero, x.signum() <= 0, "lhs less-than-or-equal-to");
    }

    void
    test_lhs_zero()
    {
        testcase("lhs zero");

        test_lhs_zero(-7);
        test_lhs_zero(0);
        test_lhs_zero(32);
    }

    void
    test_rhs_zero(IntegerWrapper x)
    {
        expect_same(zero >= x, 0 >= x.signum(), "rhs greater-than-or-equal-to");
        expect_same(zero > x, 0 > x.signum(), "rhs greater than");
        expect_same(zero == x, 0 == x.signum(), "rhs equal to");
        expect_same(zero != x, 0 != x.signum(), "rhs not equal to");
        expect_same(zero < x, 0 < x.signum(), "rhs less than");
        expect_same(zero <= x, 0 <= x.signum(), "rhs less-than-or-equal-to");
    }

    void
    test_rhs_zero()
    {
        testcase("rhs zero");

        test_rhs_zero(-4);
        test_rhs_zero(0);
        test_rhs_zero(64);
    }

    void
    test_adl()
    {
        expect(adl_tester{} == zero, "ADL failure!");
        expect(inner_adl_test::adl_tester2{} == zero, "ADL failure!");
    }

    void
    run() override
    {
        test_lhs_zero();
        test_rhs_zero();
        test_adl();
    }
};

BEAST_DEFINE_TESTSUITE(Zero, beast, beast);

}  // namespace beast
