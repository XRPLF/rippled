//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/Number.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

class Number_test : public beast::unit_test::suite
{
public:
    void
    testZero()
    {
        testcase("zero");

        Number const z{0, 0};

        BEAST_EXPECT(z.mantissa() == 0);
        BEAST_EXPECT(z.exponent() == Number{}.exponent());

        BEAST_EXPECT((z + z) == z);
        BEAST_EXPECT((z - z) == z);
        BEAST_EXPECT(z == -z);
    }

    void
    test_add()
    {
        testcase("test_add");
        Number x[]{
            Number{1'000'000'000'000'000, -15},
            Number{-1'000'000'000'000'000, -15},
            Number{-1'000'000'000'000'000, -15},
            Number{-6'555'555'555'555'555, -29}};
        Number y[]{
            Number{6'555'555'555'555'555, -29},
            Number{-6'555'555'555'555'555, -29},
            Number{6'555'555'555'555'555, -29},
            Number{1'000'000'000'000'000, -15}};
        Number z[]{
            Number{1'000'000'000'000'066, -15},
            Number{-1'000'000'000'000'066, -15},
            Number{-9'999'999'999'999'344, -16},
            Number{9'999'999'999'999'344, -16}};
        for (unsigned i = 0; i < std::size(x); ++i)
        {
            BEAST_EXPECT(x[i] + y[i] == z[i]);
        }
    }

    void
    test_sub()
    {
        testcase("test_sub");
        Number x[]{
            Number{1'000'000'000'000'000, -15},
            Number{6'555'555'555'555'555, -29}};
        Number y[]{
            Number{6'555'555'555'555'555, -29},
            Number{1'000'000'000'000'000, -15}};
        Number z[]{
            Number{9'999'999'999'999'344, -16},
            Number{-9'999'999'999'999'344, -16}};
        for (unsigned i = 0; i < std::size(x); ++i)
        {
            BEAST_EXPECT(x[i] - y[i] == z[i]);
        }
    }

    void
    test_div()
    {
        testcase("test_div");
        Number x[]{Number{1}, Number{1}, Number{0}};
        Number y[]{Number{2}, Number{10}, Number{100}};
        Number z[]{Number{5, -1}, Number{1, -1}, Number{0}};
        for (unsigned i = 0; i < std::size(x); ++i)
        {
            BEAST_EXPECT(x[i] / y[i] == z[i]);
        }
    }

    void
    test_root()
    {
        testcase("test_root");
        Number x[]{Number{2}, Number{2'000'000}, Number{2, -30}};
        unsigned y[]{2, 2, 2};
        Number z[]{
            Number{1414213562373095, -15},
            Number{1414213562373095, -12},
            Number{1414213562373095, -30}};
        for (unsigned i = 0; i < std::size(x); ++i)
        {
            BEAST_EXPECT(root(x[i], y[i]) == z[i]);
        }
    }

    void
    testConversions()
    {
        testcase("testConversions");

        IOUAmount x{5, 6};
        Number y = x;
        BEAST_EXPECT((y == Number{5, 6}));
        IOUAmount z{y};
        BEAST_EXPECT(x == z);
    }

    void
    test_to_integer()
    {
        testcase("test_to_integer");
        Number x[]{
            Number{0},
            Number{1},
            Number{2},
            Number{3},
            Number{-1},
            Number{-2},
            Number{-3},
            Number{10},
            Number{99},
            Number{1155},
            Number{9'999'999'999'999'999, 0},
            Number{9'999'999'999'999'999, 1},
            Number{9'999'999'999'999'999, 2},
            Number{-9'999'999'999'999'999, 2},
            Number{15, -1},
            Number{14, -1},
            Number{16, -1},
            Number{25, -1},
            Number{6, -1},
            Number{5, -1},
            Number{4, -1},
            Number{-15, -1},
            Number{-14, -1},
            Number{-16, -1},
            Number{-25, -1},
            Number{-6, -1},
            Number{-5, -1},
            Number{-4, -1}};
        std::int64_t y[]{
            0,
            1,
            2,
            3,
            -1,
            -2,
            -3,
            10,
            99,
            1155,
            9'999'999'999'999'999,
            99'999'999'999'999'990,
            999'999'999'999'999'900,
            -999'999'999'999'999'900,
            2,
            1,
            2,
            2,
            1,
            0,
            0,
            -2,
            -1,
            -2,
            -2,
            -1,
            0,
            0};
        static_assert(std::size(x) == std::size(y));
        for (unsigned u = 0; u < std::size(x); ++u)
        {
            auto j = static_cast<std::int64_t>(x[u]);
            BEAST_EXPECT(j == y[u]);
        }
    }

    void
    test_clip()
    {
        testcase("test_clip");
        Number limit{1, -6};
        BEAST_EXPECT((clip(Number{2, -6}, limit) == Number{2, -6}));
        BEAST_EXPECT((clip(Number{1, -6}, limit) == Number{1, -6}));
        BEAST_EXPECT((clip(Number{9, -7}, limit) == Number{0}));
        BEAST_EXPECT((clip(Number{-2, -6}, limit) == Number{-2, -6}));
        BEAST_EXPECT((clip(Number{-1, -6}, limit) == Number{-1, -6}));
        BEAST_EXPECT((clip(Number{-9, -7}, limit) == Number{0}));
    }

    void
    run() override
    {
        testZero();
        test_add();
        test_sub();
        test_div();
        test_root();
        testConversions();
        test_to_integer();
        test_clip();
    }
};

BEAST_DEFINE_TESTSUITE(Number, ripple_basics, ripple);

}  // namespace ripple
