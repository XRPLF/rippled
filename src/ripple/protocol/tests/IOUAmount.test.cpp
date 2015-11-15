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

#include <BeastConfig.h>
#include <ripple/protocol/IOUAmount.h>
#include <beast/unit_test/suite.h>

namespace ripple {

class IOUAmount_test : public beast::unit_test::suite
{
public:
    void testZero ()
    {
        testcase ("zero");

        IOUAmount const z (0, 0);

        expect (z.mantissa () == 0);
        expect (z.exponent () == -100);
        expect (!z);
        expect (z.signum () == 0);
        expect (z == zero);

        expect ((z + z) == z);
        expect ((z - z) == z);
        expect (z == -z);

        IOUAmount const zz (zero);
        expect (z == zz);
    }

    void testSigNum ()
    {
        testcase ("signum");

        IOUAmount const neg (-1, 0);
        expect (neg.signum () < 0);

        IOUAmount const zer (0, 0);
        expect (zer.signum () == 0);

        IOUAmount const pos (1, 0);
        expect (pos.signum () > 0);
    }

    void testBeastZero ()
    {
        testcase ("beast::Zero Comparisons");

        {
            IOUAmount z (zero);
            expect (z == zero);
            expect (z >= zero);
            expect (z <= zero);
            unexpected (z != zero);
            unexpected (z > zero);
            unexpected (z < zero);
        }

        {
            IOUAmount const neg (-2, 0);
            expect (neg < zero);
            expect (neg <= zero);
            expect (neg != zero);
            unexpected (neg == zero);
        }

        {
            IOUAmount const pos (2, 0);
            expect (pos > zero);
            expect (pos >= zero);
            expect (pos != zero);
            unexpected (pos == zero);
        }
    }

    void testComparisons ()
    {
        testcase ("IOU Comparisons");

        IOUAmount const n (-2, 0);
        IOUAmount const z (0, 0);
        IOUAmount const p (2, 0);

        expect (z == z);
        expect (z >= z);
        expect (z <= z);
        expect (z == -z);
        unexpected (z > z);
        unexpected (z < z);
        unexpected (z != z);
        unexpected (z != -z);

        expect (n < z);
        expect (n <= z);
        expect (n != z);
        unexpected (n > z);
        unexpected (n >= z);
        unexpected (n == z);

        expect (p > z);
        expect (p >= z);
        expect (p != z);
        unexpected (p < z);
        unexpected (p <= z);
        unexpected (p == z);

        expect (n < p);
        expect (n <= p);
        expect (n != p);
        unexpected (n > p);
        unexpected (n >= p);
        unexpected (n == p);

        expect (p > n);
        expect (p >= n);
        expect (p != n);
        unexpected (p < n);
        unexpected (p <= n);
        unexpected (p == n);

        expect (p > -p);
        expect (p >= -p);
        expect (p != -p);

        expect (n < -n);
        expect (n <= -n);
        expect (n != -n);
    }

    void testToString()
    {
        testcase("IOU strings");

        expect(to_string(IOUAmount (-2, 0)) == "-2");
        expect(to_string(IOUAmount (0, 0)) == "0");
        expect(to_string(IOUAmount (2, 0)) == "2");
        expect(to_string(IOUAmount (25, -3)) == "0.025");
        expect(to_string(IOUAmount (-25, -3)) == "-0.025");
        expect(to_string(IOUAmount (25, 1)) == "250");
        expect(to_string(IOUAmount (-25, 1)) == "-250");
        expect(to_string(IOUAmount (2, 20)) == "2000000000000000e5");
        expect(to_string(IOUAmount (-2, -20)) == "-2000000000000000e-35");
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
            expect (bigMan == mulRatio (bigMan, maxUInt, maxUInt, true));
            // rounding mode shouldn't matter as the result is exact
            expect (bigMan == mulRatio (bigMan, maxUInt, maxUInt, false));
        }
        {
            // Similar test as above, but for negative values
            IOUAmount bigMan (-maxMantissa, 0);
            expect (bigMan == mulRatio (bigMan, maxUInt, maxUInt, true));
            // rounding mode shouldn't matter as the result is exact
            expect (bigMan == mulRatio (bigMan, maxUInt, maxUInt, false));
        }

        {
            // small amounts
            IOUAmount tiny (minMantissa, minExponent);
            // Round up should give the smallest allowable number
            expect (tiny == mulRatio (tiny, 1, maxUInt, true));
            expect (tiny == mulRatio (tiny, maxUInt - 1, maxUInt, true));
            // rounding down should be zero
            expect (beast::zero == mulRatio (tiny, 1, maxUInt, false));
            expect (beast::zero == mulRatio (tiny, maxUInt - 1, maxUInt, false));

            // tiny negative numbers
            IOUAmount tinyNeg (-minMantissa, minExponent);
            // Round up should give zero
            expect (zero == mulRatio (tinyNeg, 1, maxUInt, true));
            expect (zero == mulRatio (tinyNeg, maxUInt - 1, maxUInt, true));
            // rounding down should be tiny
            expect (tinyNeg == mulRatio (tinyNeg, 1, maxUInt, false));
            expect (tinyNeg == mulRatio (tinyNeg, maxUInt - 1, maxUInt, false));
        }

        {
            // rounding
            {
                IOUAmount one (1, 0);
                auto const rup = mulRatio (one, maxUInt - 1, maxUInt, true);
                auto const rdown = mulRatio (one, maxUInt - 1, maxUInt, false);
                expect (rup.mantissa () - rdown.mantissa () == 1);
            }
            {
                IOUAmount big (maxMantissa, maxExponent);
                auto const rup = mulRatio (big, maxUInt - 1, maxUInt, true);
                auto const rdown = mulRatio (big, maxUInt - 1, maxUInt, false);
                expect (rup.mantissa () - rdown.mantissa () == 1);
            }

            {
                IOUAmount negOne (-1, 0);
                auto const rup = mulRatio (negOne, maxUInt - 1, maxUInt, true);
                auto const rdown = mulRatio (negOne, maxUInt - 1, maxUInt, false);
                expect (rup.mantissa () - rdown.mantissa () == 1);
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

    void run ()
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
