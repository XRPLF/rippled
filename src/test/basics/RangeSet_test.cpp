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

#include <ripple/basics/RangeSet.h>
#include <ripple/beast/unit_test.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

namespace ripple
{
class RangeSet_test : public beast::unit_test::suite
{
public:
    void
    testPrevMissing()
    {
        testcase("prevMissing");

        // Set will include:
        // [ 0, 5]
        // [10,15]
        // [20,25]
        // etc...

        RangeSet<std::uint32_t> set;
        for (std::uint32_t i = 0; i < 10; ++i)
            set.insert(range(10 * i, 10 * i + 5));

        for (std::uint32_t i = 1; i < 100; ++i)
        {
            boost::optional<std::uint32_t> expected;
            // no prev missing in domain for i <= 6
            if (i > 6)
            {
                std::uint32_t const oneBelowRange = (10 * (i / 10)) - 1;

                expected = ((i % 10) > 6) ? (i - 1) : oneBelowRange;
            }
            BEAST_EXPECT(prevMissing(set, i) == expected);
        }
    }

    void
    testIntersection()
    {
        testcase("testIntersection");

        RangeSet<std::uint32_t> a;
        RangeSet<std::uint32_t> b;
        RangeSet<std::uint32_t> expected;

        auto clear = [&]()
        {
            a.clear();
            b.clear();
            expected.clear();
        };

        // Test 1
        a.insert(range<std::uint32_t>(1, 100));
        a.insert(range<std::uint32_t>(110, 150));
        a.insert(range<std::uint32_t>(160, 200));

        b.insert(range<std::uint32_t>(5, 105));
        b.insert(range<std::uint32_t>(110, 150));
        b.insert(range<std::uint32_t>(160, 210));

        auto intersection = getIntersection<std::uint32_t>({a,b});

        expected.insert(range<std::uint32_t>(5, 100));
        expected.insert(range<std::uint32_t>(110, 150));
        expected.insert(range<std::uint32_t>(160, 200));

        BEAST_EXPECT(intersection == expected);
        clear();

        // Test 2
        a.insert(range<std::uint32_t>(1000, 2000));
        b.insert(range<std::uint32_t>(1001, 2001));

        intersection = getIntersection<std::uint32_t>({a,b});

        expected.insert(range<std::uint32_t>(1001, 2000));

        BEAST_EXPECT(intersection == expected);
        clear();

        // Test 3
        a.insert(range<std::uint32_t>(1000, 2000));
        b.insert(range<std::uint32_t>(2000, 3000));

        intersection = getIntersection<std::uint32_t>({a,b});

        expected.insert(range<std::uint32_t>(2000, 2000));

        BEAST_EXPECT(intersection == expected);
        clear();

        // Test 4
        a.insert(range<std::uint32_t>(1, 100));
        a.insert(range<std::uint32_t>(110, 150));
        a.insert(range<std::uint32_t>(160, 200));

        b.insert(range<std::uint32_t>(5, 170));

        intersection = getIntersection<std::uint32_t>({a,b});

        expected.insert(range<std::uint32_t>(5, 100));
        expected.insert(range<std::uint32_t>(110, 150));
        expected.insert(range<std::uint32_t>(160, 170));

        BEAST_EXPECT(intersection == expected);
        clear();

        // Test 5
        a.insert(range<std::uint32_t>(1, 100));
        a.insert(range<std::uint32_t>(110, 150));
        a.insert(range<std::uint32_t>(160, 200));

        b.insert(range<std::uint32_t>(1000, 3000));

        intersection = getIntersection<std::uint32_t>({a,b});

        expected.clear();

        BEAST_EXPECT(intersection == expected);
        clear();

        // Test 6
        a.insert(range<std::uint32_t>(100, 1000));
        b.insert(range<std::uint32_t>(100, 1000));

        intersection = getIntersection<std::uint32_t>({a,b});

        expected.insert(range<std::uint32_t>(100, 1000));

        BEAST_EXPECT(intersection == expected);
        clear();

        // Test 7
        a.insert(range<std::uint32_t>(1, 100));
        a.insert(range<std::uint32_t>(110, 150));
        a.insert(range<std::uint32_t>(160, 200));
        a.insert(range<std::uint32_t>(240, 300));
        a.insert(range<std::uint32_t>(340, 400));

        b.insert(range<std::uint32_t>(120, 150));
        b.insert(range<std::uint32_t>(155, 220));
        b.insert(range<std::uint32_t>(240, 300));
        b.insert(range<std::uint32_t>(340, 400));

        intersection = getIntersection<std::uint32_t>({a,b});

        expected.insert(range<std::uint32_t>(120, 150));
        expected.insert(range<std::uint32_t>(160, 200));
        expected.insert(range<std::uint32_t>(240, 300));
        expected.insert(range<std::uint32_t>(340, 400));

        BEAST_EXPECT(intersection == expected);
        clear();

        // Test 8
        a.insert(range<std::uint32_t>(1, 100));
        a.insert(range<std::uint32_t>(200, 300));

        b.insert(range<std::uint32_t>(50, 150));
        b.insert(range<std::uint32_t>(250, 350));

        intersection = getIntersection<std::uint32_t>({a,b});

        expected.insert(range<std::uint32_t>(50, 100));
        expected.insert(range<std::uint32_t>(250, 300));

        BEAST_EXPECT(intersection == expected);
        clear();
    }

    void
    testToString()
    {
        testcase("toString");

        RangeSet<std::uint32_t> set;
        BEAST_EXPECT(to_string(set) == "empty");

        set.insert(1);
        BEAST_EXPECT(to_string(set) == "1");

        set.insert(range(4u, 6u));
        BEAST_EXPECT(to_string(set) == "1,4-6");

        set.insert(2);
        BEAST_EXPECT(to_string(set) == "1-2,4-6");

        set.erase(range(4u, 5u));
        BEAST_EXPECT(to_string(set) == "1-2,6");
    }

    void
    testSerialization()
    {

        auto works = [](RangeSet<std::uint32_t> const & orig)
        {
            std::stringstream ss;
            boost::archive::binary_oarchive oa(ss);
            oa << orig;

            boost::archive::binary_iarchive ia(ss);
            RangeSet<std::uint32_t> deser;
            ia >> deser;

            return orig == deser;
        };

        RangeSet<std::uint32_t> rs;

        BEAST_EXPECT(works(rs));

        rs.insert(3);
        BEAST_EXPECT(works(rs));

        rs.insert(range(7u, 10u));
        BEAST_EXPECT(works(rs));

    }

    void
    run() override
    {
        testPrevMissing();
        testIntersection();
        testToString();
        testSerialization();
    }
};

BEAST_DEFINE_TESTSUITE(RangeSet, ripple_basics, ripple);

}  // namespace ripple
