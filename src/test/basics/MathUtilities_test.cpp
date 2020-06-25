//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/basics/MathUtilities.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

class MathUtilities_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        testcase("calculate percentage");
        BEAST_EXPECT(calculatePercent(0, 100) == 0);
        BEAST_EXPECT(calculatePercent(100, 100) == 100);
        BEAST_EXPECT(calculatePercent(200, 100) == 100);
        BEAST_EXPECT(calculatePercent(1, 100) == 1);
        BEAST_EXPECT(calculatePercent(1, 99) == 2);
        BEAST_EXPECT(calculatePercent(6, 14) == 43);
        BEAST_EXPECT(calculatePercent(29, 33) == 88);
        BEAST_EXPECT(calculatePercent(1, 64) == 2);
        BEAST_EXPECT(calculatePercent(0, 100'000'000) == 0);
        BEAST_EXPECT(calculatePercent(1, 100'000'000) == 1);
        BEAST_EXPECT(calculatePercent(50'000'000, 100'000'000) == 50);
        BEAST_EXPECT(calculatePercent(50'000'001, 100'000'000) == 51);
        BEAST_EXPECT(calculatePercent(99'999'999, 100'000'000) == 100);

        constexpr std::size_t p = calculatePercent(1, 2);
        BEAST_EXPECT(p == 50);
    }
};

BEAST_DEFINE_TESTSUITE(MathUtilities, ripple_basics, ripple);
}  // namespace ripple
