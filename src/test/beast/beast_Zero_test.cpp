//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Nikolaos D. Bougalis <nikb@bougalis.net>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/beast/utility/Zero.h>

#include <ripple/beast/unit_test.h>

namespace beast {

struct adl_tester
{
};

int signum(adl_tester)
{
    return 0;
}

namespace inner_adl_test {

struct adl_tester2
{
};

int signum(adl_tester2)
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

BEAST_DEFINE_TESTSUITE(Zero, types, beast);

}  // namespace beast
