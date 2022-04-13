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
    run() override
    {
        testZero();
        test_add();
        test_sub();
        test_div();
        test_root();
        testConversions();
    }
};

BEAST_DEFINE_TESTSUITE(Number, ripple_basics, ripple);

}  // namespace ripple
