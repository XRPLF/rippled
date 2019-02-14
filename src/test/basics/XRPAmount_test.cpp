//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/basics/XRPAmount.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

class XRPAmount_test : public beast::unit_test::suite
{
public:
    void testSigNum ()
    {
        testcase ("signum");

        for (auto i : { -1, 0, 1})
        {
            XRPAmount const x(i);

            if (i < 0)
                BEAST_EXPECT(x.signum () < 0);
            else if (i > 0)
                BEAST_EXPECT(x.signum () > 0);
            else
                BEAST_EXPECT(x.signum () == 0);
        }
    }

    void testBeastZero ()
    {
        testcase ("beast::Zero Comparisons");

        using beast::zero;

        for (auto i : { -1, 0, 1})
        {
            XRPAmount const x (i);

            BEAST_EXPECT((i == 0) == (x == zero));
            BEAST_EXPECT((i != 0) == (x != zero));
            BEAST_EXPECT((i < 0) == (x < zero));
            BEAST_EXPECT((i > 0) == (x > zero));
            BEAST_EXPECT((i <= 0) == (x <= zero));
            BEAST_EXPECT((i >= 0) == (x >= zero));

            BEAST_EXPECT((0 == i) == (zero == x));
            BEAST_EXPECT((0 != i) == (zero != x));
            BEAST_EXPECT((0 < i) == (zero < x));
            BEAST_EXPECT((0 > i) == (zero > x));
            BEAST_EXPECT((0 <= i) == (zero <= x));
            BEAST_EXPECT((0 >= i) == (zero >= x));
        }
    }

    void testComparisons ()
    {
        testcase ("XRP Comparisons");

        for (auto i : { -1, 0, 1})
        {
            XRPAmount const x (i);

            for (auto j : { -1, 0, 1})
            {
                XRPAmount const y (j);

                BEAST_EXPECT((i == j) == (x == y));
                BEAST_EXPECT((i != j) == (x != y));
                BEAST_EXPECT((i < j) == (x < y));
                BEAST_EXPECT((i > j) == (x > y));
                BEAST_EXPECT((i <= j) == (x <= y));
                BEAST_EXPECT((i >= j) == (x >= y));
            }
        }
    }

    void testAddSub ()
    {
        testcase ("Addition & Subtraction");

        for (auto i : { -1, 0, 1})
        {
            XRPAmount const x (i);

            for (auto j : { -1, 0, 1})
            {
                XRPAmount const y (j);

                BEAST_EXPECT(XRPAmount(i + j) == (x + y));
                BEAST_EXPECT(XRPAmount(i - j) == (x - y));

                BEAST_EXPECT((x + y) == (y + x));   // addition is commutative
            }
        }
    }

    void testDecimal ()
    {
        // Tautology
        BEAST_EXPECT(DROPS_PER_XRP.decimalXRP() == 1);

        XRPAmount test{1};
        BEAST_EXPECT(test.decimalXRP() == 0.000001);

        test = -test;
        BEAST_EXPECT(test.decimalXRP() == -0.000001);

        test = 100'000'000;
        BEAST_EXPECT(test.decimalXRP() == 100);

        test = -test;
        BEAST_EXPECT(test.decimalXRP() == -100);
    }

    void testFunctions()
    {
        // Explicitly test every defined function for the XRPAmount class
        // since some of them are templated, but not used anywhere else.
        auto make = [&](auto x) -> XRPAmount {
            return x; };

        XRPAmount defaulted;
        (void)defaulted;
        XRPAmount test{ 0 };
        BEAST_EXPECT(test.drops() == 0);

        test = make(beast::zero);
        BEAST_EXPECT(test.drops() == 0);

        test = beast::zero;
        BEAST_EXPECT(test.drops() == 0);

        test = make(100);
        BEAST_EXPECT(test.drops() == 100);

        test = make(100u);
        BEAST_EXPECT(test.drops() == 100);

        XRPAmount const targetSame{ 200u };
        test = make(targetSame);
        BEAST_EXPECT(test.drops() == 200);
        BEAST_EXPECT(test == targetSame);
        BEAST_EXPECT(test < XRPAmount{ 1000 });
        BEAST_EXPECT(test > XRPAmount{ 100 });

        test = std::int64_t(200);
        BEAST_EXPECT(test.drops() == 200);
        test = std::uint32_t(300);
        BEAST_EXPECT(test.drops() == 300);

        test = targetSame;
        BEAST_EXPECT(test.drops() == 200);
        auto testOther = test.dropsAs<std::uint32_t>();
        BEAST_EXPECT(testOther);
        BEAST_EXPECT(*testOther == 200);
        test = std::numeric_limits<std::uint64_t>::max();
        testOther = test.dropsAs<std::uint32_t>();
        BEAST_EXPECT(!testOther);
        test = -1;
        testOther = test.dropsAs<std::uint32_t>();
        BEAST_EXPECT(!testOther);

        test = targetSame * 2;
        BEAST_EXPECT(test.drops() == 400);
        test = 3 * targetSame;
        BEAST_EXPECT(test.drops() == 600);
        test = 20;
        BEAST_EXPECT(test.drops() == 20);

        test += targetSame;
        BEAST_EXPECT(test.drops() == 220);

        test -= targetSame;
        BEAST_EXPECT(test.drops() == 20);

        test *= 5;
        BEAST_EXPECT(test.drops() == 100);
        test = 50;
        BEAST_EXPECT(test.drops() == 50);
        test -= 39;
        BEAST_EXPECT(test.drops() == 11);

        // legal with signed
        test = -test;
        BEAST_EXPECT(test.drops() == -11);
        BEAST_EXPECT(test.signum() == -1);
        BEAST_EXPECT(to_string(test) == "-11");

        BEAST_EXPECT(test);
        test = 0;
        BEAST_EXPECT(!test);
        BEAST_EXPECT(test.signum() == 0);
        test = targetSame;
        BEAST_EXPECT(test.signum() == 1);
        BEAST_EXPECT(to_string(test) == "200");
    }

    void testMulRatio()
    {
        testcase ("mulRatio");

        constexpr auto maxUInt32 = std::numeric_limits<std::uint32_t>::max ();
        constexpr auto maxXRP = std::numeric_limits<XRPAmount::value_type>::max ();
        constexpr auto minXRP = std::numeric_limits<XRPAmount::value_type>::min ();

        {
            // multiply by a number that would overflow then divide by the same
            // number, and check we didn't lose any value
            XRPAmount big (maxXRP);
            BEAST_EXPECT(big == mulRatio (big, maxUInt32, maxUInt32, true));
            // rounding mode shouldn't matter as the result is exact
            BEAST_EXPECT(big == mulRatio (big, maxUInt32, maxUInt32, false));

            // multiply and divide by values that would overflow if done naively,
            // and check that it gives the correct answer
            big -= 0xf; // Subtract a little so it's divisable by 4
            BEAST_EXPECT(mulRatio(big, 3, 4, false).value() == (big.value() / 4) * 3);
            BEAST_EXPECT(mulRatio(big, 3, 4, true).value() == (big.value() / 4) * 3);
            BEAST_EXPECT((big.value() * 3) / 4 != (big.value() / 4) * 3);
        }

        {
            // Similar test as above, but for negative values
            XRPAmount big (minXRP);
            BEAST_EXPECT(big == mulRatio (big, maxUInt32, maxUInt32, true));
            // rounding mode shouldn't matter as the result is exact
            BEAST_EXPECT(big == mulRatio (big, maxUInt32, maxUInt32, false));

            // multiply and divide by values that would overflow if done naively,
            // and check that it gives the correct answer
            BEAST_EXPECT(mulRatio(big, 3, 4, false).value() == (big.value() / 4) * 3);
            BEAST_EXPECT(mulRatio(big, 3, 4, true).value() == (big.value() / 4) * 3);
            BEAST_EXPECT((big.value() * 3) / 4 != (big.value() / 4) * 3);
        }

        {
            // small amounts
            XRPAmount tiny (1);
            // Round up should give the smallest allowable number
            BEAST_EXPECT(tiny == mulRatio (tiny, 1, maxUInt32, true));
            // rounding down should be zero
            BEAST_EXPECT(beast::zero == mulRatio (tiny, 1, maxUInt32, false));
            BEAST_EXPECT(beast::zero ==
                mulRatio (tiny, maxUInt32 - 1, maxUInt32, false));

            // tiny negative numbers
            XRPAmount tinyNeg (-1);
            // Round up should give zero
            BEAST_EXPECT(beast::zero == mulRatio (tinyNeg, 1, maxUInt32, true));
            BEAST_EXPECT(beast::zero == mulRatio (tinyNeg, maxUInt32 - 1, maxUInt32, true));
            // rounding down should be tiny
            BEAST_EXPECT(tinyNeg == mulRatio (tinyNeg, maxUInt32 - 1, maxUInt32, false));
        }

        {
            // rounding
            {
                XRPAmount one (1);
                auto const rup = mulRatio (one, maxUInt32 - 1, maxUInt32, true);
                auto const rdown = mulRatio (one, maxUInt32 - 1, maxUInt32, false);
                BEAST_EXPECT(rup.drops () - rdown.drops () == 1);
            }

            {
                XRPAmount big (maxXRP);
                auto const rup = mulRatio (big, maxUInt32 - 1, maxUInt32, true);
                auto const rdown = mulRatio (big, maxUInt32 - 1, maxUInt32, false);
                BEAST_EXPECT(rup.drops () - rdown.drops () == 1);
            }

            {
                XRPAmount negOne (-1);
                auto const rup = mulRatio (negOne, maxUInt32 - 1, maxUInt32, true);
                auto const rdown = mulRatio (negOne, maxUInt32 - 1, maxUInt32, false);
                BEAST_EXPECT(rup.drops () - rdown.drops () == 1);
            }
        }

        {
            // division by zero
            XRPAmount one (1);
            except ([&] {mulRatio (one, 1, 0, true);});
        }

        {
            // overflow
            XRPAmount big (maxXRP);
            except ([&] {mulRatio (big, 2, 1, true);});
        }

        {
            // underflow
            XRPAmount bigNegative (minXRP + 10);
            BEAST_EXPECT(mulRatio(bigNegative, 2, 1, true) == minXRP);
        }
    }

    //--------------------------------------------------------------------------

    void run () override
    {
        testSigNum ();
        testBeastZero ();
        testComparisons ();
        testAddSub ();
        testDecimal ();
        testFunctions ();
        testMulRatio ();
    }
};

BEAST_DEFINE_TESTSUITE(XRPAmount,protocol,ripple);

} // ripple
