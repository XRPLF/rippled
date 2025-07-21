//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012 Ripple Labs Inc.

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

#include <xrpl/basics/RangeSet.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

using namespace ripple;

TEST_SUITE_BEGIN("RangeSet");

TEST_CASE("prevMissing")
{
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
        std::optional<std::uint32_t> expected;
        // no prev missing in domain for i <= 6
        if (i > 6)
        {
            std::uint32_t const oneBelowRange = (10 * (i / 10)) - 1;

            expected = ((i % 10) > 6) ? (i - 1) : oneBelowRange;
        }
        CHECK(prevMissing(set, i) == expected);
    }
}

TEST_CASE("toString")
{
    RangeSet<std::uint32_t> set;
    CHECK(to_string(set) == "empty");

    set.insert(1);
    CHECK(to_string(set) == "1");

    set.insert(range(4u, 6u));
    CHECK(to_string(set) == "1,4-6");

    set.insert(2);
    CHECK(to_string(set) == "1-2,4-6");

    set.erase(range(4u, 5u));
    CHECK(to_string(set) == "1-2,6");
}

TEST_CASE("fromString")
{
    RangeSet<std::uint32_t> set;

    CHECK(!from_string(set, ""));
    CHECK(boost::icl::length(set) == 0);

    CHECK(!from_string(set, "#"));
    CHECK(boost::icl::length(set) == 0);

    CHECK(!from_string(set, ","));
    CHECK(boost::icl::length(set) == 0);

    CHECK(!from_string(set, ",-"));
    CHECK(boost::icl::length(set) == 0);

    CHECK(!from_string(set, "1,,2"));
    CHECK(boost::icl::length(set) == 0);

    CHECK(from_string(set, "1"));
    CHECK(boost::icl::length(set) == 1);
    CHECK(boost::icl::first(set) == 1);

    CHECK(from_string(set, "1,1"));
    CHECK(boost::icl::length(set) == 1);
    CHECK(boost::icl::first(set) == 1);

    CHECK(from_string(set, "1-1"));
    CHECK(boost::icl::length(set) == 1);
    CHECK(boost::icl::first(set) == 1);

    CHECK(from_string(set, "1,4-6"));
    CHECK(boost::icl::length(set) == 4);
    CHECK(boost::icl::first(set) == 1);
    CHECK(!boost::icl::contains(set, 2));
    CHECK(!boost::icl::contains(set, 3));
    CHECK(boost::icl::contains(set, 4));
    CHECK(boost::icl::contains(set, 5));
    CHECK(boost::icl::last(set) == 6);

    CHECK(from_string(set, "1-2,4-6"));
    CHECK(boost::icl::length(set) == 5);
    CHECK(boost::icl::first(set) == 1);
    CHECK(boost::icl::contains(set, 2));
    CHECK(boost::icl::contains(set, 4));
    CHECK(boost::icl::last(set) == 6);

    CHECK(from_string(set, "1-2,6"));
    CHECK(boost::icl::length(set) == 3);
    CHECK(boost::icl::first(set) == 1);
    CHECK(boost::icl::contains(set, 2));
    CHECK(boost::icl::last(set) == 6);
}

TEST_SUITE_END();
