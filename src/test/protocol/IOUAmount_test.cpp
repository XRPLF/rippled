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

#include <ripple/protocol/IOUAmount.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

class IOUAmount_test : public beast::unit_test::suite
{
public:
    void testZero ()
    {
        testcase ("zero");

        IOUAmount const z (0, 0);

        BEAST_EXPECT(z.mantissa () == 0);
        BEAST_EXPECT(z.exponent () == -100);
        BEAST_EXPECT(!z);
        BEAST_EXPECT(z.signum () == 0);
        BEAST_EXPECT(z == zero);

        BEAST_EXPECT((z + z) == z);
        BEAST_EXPECT((z - z) == z);
        BEAST_EXPECT(z == -z);

        IOUAmount const zz (zero);
        BEAST_EXPECT(z == zz);
    }

    void testSigNum ()
    {
        testcase ("signum");

        IOUAmount const neg (-1, 0);
        BEAST_EXPECT(neg.signum () < 0);

        IOUAmount const zer (0, 0);
        BEAST_EXPECT(zer.signum () == 0);

        IOUAmount const pos (1, 0);
        BEAST_EXPECT(pos.signum () > 0);
    }

    void testBeastZero ()
    {
        testcase ("beast::Zero Comparisons");

        {
            IOUAmount z (zero);
            BEAST_EXPECT(z == zero);
            BEAST_EXPECT(z >= zero);
            BEAST_EXPECT(z <= zero);
            unexpected (z != zero);
            unexpected (z > zero);
            unexpected (z < zero);
        }

        {
            IOUAmount const neg (-2, 0);
            BEAST_EXPECT(neg < zero);
            BEAST_EXPECT(neg <= zero);
            BEAST_EXPECT(neg != zero);
            unexpected (neg == zero);
        }

        {
            IOUAmount const pos (2, 0);
            BEAST_EXPECT(pos > zero);
            BEAST_EXPECT(pos >= zero);
            BEAST_EXPECT(pos != zero);
            unexpected (pos == zero);
        }
    }

    void testComparisons ()
    {
        testcase ("IOU Comparisons");

        IOUAmount const n (-2, 0);
        IOUAmount const z (0, 0);
        IOUAmount const p (2, 0);

        BEAST_EXPECT(z == z);
        BEAST_EXPECT(z >= z);
        BEAST_EXPECT(z <= z);
        BEAST_EXPECT(z == -z);
        unexpected (z > z);
        unexpected (z < z);
        unexpected (z != z);
        unexpected (z != -z);

        BEAST_EXPECT(n < z);
        BEAST_EXPECT(n <= z);
        BEAST_EXPECT(n != z);
        unexpected (n > z);
        unexpected (n >= z);
        unexpected (n == z);

        BEAST_EXPECT(p > z);
        BEAST_EXPECT(p >= z);
        BEAST_EXPECT(p != z);
        unexpected (p < z);
        unexpected (p <= z);
        unexpected (p == z);

        BEAST_EXPECT(n < p);
        BEAST_EXPECT(n <= p);
        BEAST_EXPECT(n != p);
        unexpected (n > p);
        unexpected (n >= p);
        unexpected (n == p);

        BEAST_EXPECT(p > n);
        BEAST_EXPECT(p >= n);
        BEAST_EXPECT(p != n);
        unexpected (p < n);
        unexpected (p <= n);
        unexpected (p == n);

        BEAST_EXPECT(p > -p);
        BEAST_EXPECT(p >= -p);
        BEAST_EXPECT(p != -p);

        BEAST_EXPECT(n < -n);
        BEAST_EXPECT(n <= -n);
        BEAST_EXPECT(n != -n);
    }

    void testToString()
    {
        testcase("IOU strings");

        BEAST_EXPECT(to_string(IOUAmount (-2, 0)) == "-2");
        BEAST_EXPECT(to_string(IOUAmount (0, 0)) == "0");
        BEAST_EXPECT(to_string(IOUAmount (2, 0)) == "2");
        BEAST_EXPECT(to_string(IOUAmount (25, -3)) == "0.025");
        BEAST_EXPECT(to_string(IOUAmount (-25, -3)) == "-0.025");
        BEAST_EXPECT(to_string(IOUAmount (25, 1)) == "250");
        BEAST_EXPECT(to_string(IOUAmount (-25, 1)) == "-250");
        BEAST_EXPECT(to_string(IOUAmount (2, 20)) == "2000000000000000e5");
        BEAST_EXPECT(to_string(IOUAmount (-2, -20)) == "-2000000000000000e-35");
    }

    void testMulRatio()
    {
        testcase ("mulRatio");

        /* The range for the mantissa when normalized */
        constexpr std::int64_t minMantissa = 1000000000000000ull;
        constexpr std::int64_t maxMantissa = 9999999999999999ull;
        // log(2,maxMantissa) ~ 53.15
        /* The range for the exponent when normalized */
        constexpr int minExponent = -96;
        constexpr int maxExponent = 80;
        constexpr auto maxUInt = std::numeric_limits<std::uint32_t>::max ();

        {
            // multiply by a number that would overflow the mantissa, then
            // divide by the same number, and check we didn't lose any value
            IOUAmount bigMan (maxMantissa, 0);
            BEAST_EXPECT(bigMan == mulRatio (bigMan, maxUInt, maxUInt, true));
            // rounding mode shouldn't matter as the result is exact
            BEAST_EXPECT(bigMan == mulRatio (bigMan, maxUInt, maxUInt, false));
        }
        {
            // Similar test as above, but for negative values
            IOUAmount bigMan (-maxMantissa, 0);
            BEAST_EXPECT(bigMan == mulRatio (bigMan, maxUInt, maxUInt, true));
            // rounding mode shouldn't matter as the result is exact
            BEAST_EXPECT(bigMan == mulRatio (bigMan, maxUInt, maxUInt, false));
        }

        {
            // small amounts
            IOUAmount tiny (minMantissa, minExponent);
            // Round up should give the smallest allowable number
            BEAST_EXPECT(tiny == mulRatio (tiny, 1, maxUInt, true));
            BEAST_EXPECT(tiny == mulRatio (tiny, maxUInt - 1, maxUInt, true));
            // rounding down should be zero
            BEAST_EXPECT(beast::zero == mulRatio (tiny, 1, maxUInt, false));
            BEAST_EXPECT(beast::zero == mulRatio (tiny, maxUInt - 1, maxUInt, false));

            // tiny negative numbers
            IOUAmount tinyNeg (-minMantissa, minExponent);
            // Round up should give zero
            BEAST_EXPECT(zero == mulRatio (tinyNeg, 1, maxUInt, true));
            BEAST_EXPECT(zero == mulRatio (tinyNeg, maxUInt - 1, maxUInt, true));
            // rounding down should be tiny
            BEAST_EXPECT(tinyNeg == mulRatio (tinyNeg, 1, maxUInt, false));
            BEAST_EXPECT(tinyNeg == mulRatio (tinyNeg, maxUInt - 1, maxUInt, false));
        }

        {
            // rounding
            {
                IOUAmount one (1, 0);
                auto const rup = mulRatio (one, maxUInt - 1, maxUInt, true);
                auto const rdown = mulRatio (one, maxUInt - 1, maxUInt, false);
                BEAST_EXPECT(rup.mantissa () - rdown.mantissa () == 1);
            }
            {
                IOUAmount big (maxMantissa, maxExponent);
                auto const rup = mulRatio (big, maxUInt - 1, maxUInt, true);
                auto const rdown = mulRatio (big, maxUInt - 1, maxUInt, false);
                BEAST_EXPECT(rup.mantissa () - rdown.mantissa () == 1);
            }

            {
                IOUAmount negOne (-1, 0);
                auto const rup = mulRatio (negOne, maxUInt - 1, maxUInt, true);
                auto const rdown = mulRatio (negOne, maxUInt - 1, maxUInt, false);
                BEAST_EXPECT(rup.mantissa () - rdown.mantissa () == 1);
            }
        }

        {
            // division by zero
            IOUAmount one (1, 0);
            except ([&] {mulRatio (one, 1, 0, true);});
        }

        {
            // overflow
            IOUAmount big (maxMantissa, maxExponent);
            except ([&] {mulRatio (big, 2, 0, true);});
        }
    }

    //--------------------------------------------------------------------------

    void run () override
    {
        testZero ();
        testSigNum ();
        testBeastZero ();
        testComparisons ();
        testToString ();
        testMulRatio ();
    }
};

BEAST_DEFINE_TESTSUITE(IOUAmount,protocol,ripple);

} // ripple
