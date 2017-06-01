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
#include <ripple/basics/RangeSet.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

class RangeSet_test : public beast::unit_test::suite
{
public:
    RangeSet createPredefinedSet ()
    {
        RangeSet set;

        // Set will include:
        // [ 0, 5]
        // [10,15]
        // [20,25]
        // etc...

        for (int i = 0; i < 10; ++i)
            set.setRange (10 * i, 10 * i + 5);

        return set;
    }

    void testMembership ()
    {
        testcase ("membership");

        RangeSet r1, r2;

        r1.setRange (1, 10);
        r1.clearValue (5);
        r1.setRange (11, 20);

        r2.setRange (1, 4);
        r2.setRange (6, 10);
        r2.setRange (10, 20);

        BEAST_EXPECT(!r1.hasValue (5));

        BEAST_EXPECT(r2.hasValue (9));
    }

    void testPrevMissing ()
    {
        testcase ("prevMissing");

        RangeSet const set = createPredefinedSet ();

        for (int i = 0; i < 100; ++i)
        {
            int const oneBelowRange = (10*(i/10))-1;

            int const expectedPrevMissing =
                ((i % 10) > 6) ? (i-1) : oneBelowRange;

            BEAST_EXPECT(set.prevMissing (i) == expectedPrevMissing);
        }
    }

	void testGetPre ()
	{
		testcase("GetPrev");
		RangeSet const set = createPredefinedSet();
				
		BEAST_EXPECT(set.getPrev(15) == 14);
	}

    void run ()
    {
        testMembership ();

        testPrevMissing ();

        testGetPre ();

        // TODO: Traverse functions must be tested
    }
};

BEAST_DEFINE_TESTSUITE(RangeSet,ripple_basics,ripple);

} // ripple

