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
#include <ripple/protocol/XRPAmount.h>
#include <beast/unit_test/suite.h>

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
                expect (x.signum () < 0);
            else if (i > 0)
                expect (x.signum () > 0);
            else
                expect (x.signum () == 0);
        }
    }

    void testBeastZero ()
    {
        testcase ("beast::Zero Comparisons");

        for (auto i : { -1, 0, 1})
        {
            XRPAmount const x (i);

            expect ((i == 0) == (x == zero));
            expect ((i != 0) == (x != zero));
            expect ((i < 0) == (x < zero));
            expect ((i > 0) == (x > zero));
            expect ((i <= 0) == (x <= zero));
            expect ((i >= 0) == (x >= zero));

            expect ((0 == i) == (zero == x));
            expect ((0 != i) == (zero != x));
            expect ((0 < i) == (zero < x));
            expect ((0 > i) == (zero > x));
            expect ((0 <= i) == (zero <= x));
            expect ((0 >= i) == (zero >= x));
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

                expect ((i == j) == (x == y));
                expect ((i != j) == (x != y));
                expect ((i < j) == (x < y));
                expect ((i > j) == (x > y));
                expect ((i <= j) == (x <= y));
                expect ((i >= j) == (x >= y));
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

                expect (XRPAmount(i + j) == (x + y));
                expect (XRPAmount(i - j) == (x - y));

                expect ((x + y) == (y + x));   // addition is commutative
            }
        }
    }

    //--------------------------------------------------------------------------

    void run ()
    {
        testSigNum ();
        testBeastZero ();
        testComparisons ();
        testAddSub ();
    }
};

BEAST_DEFINE_TESTSUITE(XRPAmount,protocol,ripple);

} // ripple
