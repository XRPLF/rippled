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
#include <ripple/protocol/STAmount.h>
#include <sstream>
#include <tuple>

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
    test_limits()
    {
        testcase("test_limits");
        bool caught = false;
        try
        {
            Number x{10'000'000'000'000'000, 32768};
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
        Number x{10'000'000'000'000'000, 32767};
        BEAST_EXPECT((x == Number{1'000'000'000'000'000, 32768}));
        Number z{1'000'000'000'000'000, -32769};
        BEAST_EXPECT(z == Number{});
        Number y{1'000'000'000'000'001'500, 32000};
        BEAST_EXPECT((y == Number{1'000'000'000'000'002, 32003}));
        Number m{std::numeric_limits<std::int64_t>::min()};
        BEAST_EXPECT((m == Number{-9'223'372'036'854'776, 3}));
        Number M{std::numeric_limits<std::int64_t>::max()};
        BEAST_EXPECT((M == Number{9'223'372'036'854'776, 3}));
        caught = false;
        try
        {
            Number q{99'999'999'999'999'999, 32767};
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
    }

    void
    test_add()
    {
        testcase("test_add");
        using Case = std::tuple<Number, Number, Number>;
        Case c[]{
            {Number{1'000'000'000'000'000, -15},
             Number{6'555'555'555'555'555, -29},
             Number{1'000'000'000'000'066, -15}},
            {Number{-1'000'000'000'000'000, -15},
             Number{-6'555'555'555'555'555, -29},
             Number{-1'000'000'000'000'066, -15}},
            {Number{-1'000'000'000'000'000, -15},
             Number{6'555'555'555'555'555, -29},
             Number{-9'999'999'999'999'344, -16}},
            {Number{-6'555'555'555'555'555, -29},
             Number{1'000'000'000'000'000, -15},
             Number{9'999'999'999'999'344, -16}},
            {Number{}, Number{5}, Number{5}},
            {Number{5'555'555'555'555'555, -32768},
             Number{-5'555'555'555'555'554, -32768},
             Number{0}},
            {Number{-9'999'999'999'999'999, -31},
             Number{1'000'000'000'000'000, -15},
             Number{9'999'999'999'999'990, -16}}};
        for (auto const& [x, y, z] : c)
            BEAST_EXPECT(x + y == z);
        bool caught = false;
        try
        {
            Number{9'999'999'999'999'999, 32768} +
                Number{5'000'000'000'000'000, 32767};
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
    }

    void
    test_sub()
    {
        testcase("test_sub");
        using Case = std::tuple<Number, Number, Number>;
        Case c[]{
            {Number{1'000'000'000'000'000, -15},
             Number{6'555'555'555'555'555, -29},
             Number{9'999'999'999'999'344, -16}},
            {Number{6'555'555'555'555'555, -29},
             Number{1'000'000'000'000'000, -15},
             Number{-9'999'999'999'999'344, -16}},
            {Number{1'000'000'000'000'000, -15},
             Number{1'000'000'000'000'000, -15},
             Number{0}},
            {Number{1'000'000'000'000'000, -15},
             Number{1'000'000'000'000'001, -15},
             Number{-1'000'000'000'000'000, -30}},
            {Number{1'000'000'000'000'001, -15},
             Number{1'000'000'000'000'000, -15},
             Number{1'000'000'000'000'000, -30}}};
        for (auto const& [x, y, z] : c)
            BEAST_EXPECT(x - y == z);
    }

    void
    test_mul()
    {
        testcase("test_mul");
        using Case = std::tuple<Number, Number, Number>;
        Case c[]{
            {Number{7}, Number{8}, Number{56}},
            {Number{1414213562373095, -15},
             Number{1414213562373095, -15},
             Number{2000000000000000, -15}},
            {Number{-1414213562373095, -15},
             Number{1414213562373095, -15},
             Number{-2000000000000000, -15}},
            {Number{-1414213562373095, -15},
             Number{-1414213562373095, -15},
             Number{2000000000000000, -15}},
            {Number{3214285714285706, -15},
             Number{3111111111111119, -15},
             Number{1000000000000000, -14}},
            {Number{1000000000000000, -32768},
             Number{1000000000000000, -32768},
             Number{0}}};
        for (auto const& [x, y, z] : c)
            BEAST_EXPECT(x * y == z);
        bool caught = false;
        try
        {
            Number{9'999'999'999'999'999, 32768} *
                Number{5'000'000'000'000'000, 32767};
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
    }

    void
    test_div()
    {
        testcase("test_div");
        using Case = std::tuple<Number, Number, Number>;
        Case c[]{
            {Number{1}, Number{2}, Number{5, -1}},
            {Number{1}, Number{10}, Number{1, -1}},
            {Number{1}, Number{-10}, Number{-1, -1}},
            {Number{0}, Number{100}, Number{0}}};
        for (auto const& [x, y, z] : c)
            BEAST_EXPECT(x / y == z);
        bool caught = false;
        try
        {
            Number{1000000000000000, -15} / Number{0};
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
    }

    void
    test_root()
    {
        testcase("test_root");
        using Case = std::tuple<Number, unsigned, Number>;
        Case c[]{
            {Number{2}, 2, Number{1414213562373095, -15}},
            {Number{2'000'000}, 2, Number{1414213562373095, -12}},
            {Number{2, -30}, 2, Number{1414213562373095, -30}},
            {Number{-27}, 3, Number{-3}},
            {Number{1}, 5, Number{1}},
            {Number{-1}, 0, Number{1}},
            {Number{5, -1}, 0, Number{0}},
            {Number{0}, 5, Number{0}},
            {Number{5625, -4}, 2, Number{75, -2}}};
        for (auto const& [x, y, z] : c)
            BEAST_EXPECT((root(x, y) == z));
        bool caught = false;
        try
        {
            (void)root(Number{-2}, 0);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
        caught = false;
        try
        {
            (void)root(Number{-2}, 4);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
    }

    void
    test_power1()
    {
        testcase("test_power1");
        using Case = std::tuple<Number, unsigned, Number>;
        Case c[]{
            {Number{64}, 0, Number{1}},
            {Number{64}, 1, Number{64}},
            {Number{64}, 2, Number{4096}},
            {Number{-64}, 2, Number{4096}},
            {Number{64}, 3, Number{262144}},
            {Number{-64}, 3, Number{-262144}}};
        for (auto const& [x, y, z] : c)
            BEAST_EXPECT((power(x, y) == z));
    }

    void
    test_power2()
    {
        testcase("test_power2");
        using Case = std::tuple<Number, unsigned, unsigned, Number>;
        Case c[]{
            {Number{1}, 3, 7, Number{1}},
            {Number{-1}, 1, 0, Number{1}},
            {Number{-1, -1}, 1, 0, Number{0}},
            {Number{16}, 0, 5, Number{1}},
            {Number{34}, 3, 3, Number{34}},
            {Number{4}, 3, 2, Number{8}}};
        for (auto const& [x, n, d, z] : c)
            BEAST_EXPECT((power(x, n, d) == z));
        bool caught = false;
        try
        {
            (void)power(Number{7}, 0, 0);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
        caught = false;
        try
        {
            (void)power(Number{7}, 1, 0);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
        caught = false;
        try
        {
            (void)power(Number{-1, -1}, 3, 2);
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
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
        XRPAmount xrp{500};
        STAmount st = xrp;
        Number n = st;
        BEAST_EXPECT(XRPAmount{n} == xrp);
        IOUAmount x0{0, 0};
        Number y0 = x0;
        BEAST_EXPECT((y0 == Number{0}));
        IOUAmount z0{y0};
        BEAST_EXPECT(x0 == z0);
        XRPAmount xrp0{0};
        Number n0 = xrp0;
        BEAST_EXPECT(n0 == Number{0});
        XRPAmount xrp1{n0};
        BEAST_EXPECT(xrp1 == xrp0);
    }

    void
    test_to_integer()
    {
        testcase("test_to_integer");
        using Case = std::tuple<Number, std::int64_t>;
        Case c[]{
            {Number{0}, 0},
            {Number{1}, 1},
            {Number{2}, 2},
            {Number{3}, 3},
            {Number{-1}, -1},
            {Number{-2}, -2},
            {Number{-3}, -3},
            {Number{10}, 10},
            {Number{99}, 99},
            {Number{1155}, 1155},
            {Number{9'999'999'999'999'999, 0}, 9'999'999'999'999'999},
            {Number{9'999'999'999'999'999, 1}, 99'999'999'999'999'990},
            {Number{9'999'999'999'999'999, 2}, 999'999'999'999'999'900},
            {Number{-9'999'999'999'999'999, 2}, -999'999'999'999'999'900},
            {Number{15, -1}, 2},
            {Number{14, -1}, 1},
            {Number{16, -1}, 2},
            {Number{25, -1}, 2},
            {Number{6, -1}, 1},
            {Number{5, -1}, 0},
            {Number{4, -1}, 0},
            {Number{-15, -1}, -2},
            {Number{-14, -1}, -1},
            {Number{-16, -1}, -2},
            {Number{-25, -1}, -2},
            {Number{-6, -1}, -1},
            {Number{-5, -1}, 0},
            {Number{-4, -1}, 0}};
        for (auto const& [x, y] : c)
        {
            auto j = static_cast<std::int64_t>(x);
            BEAST_EXPECT(j == y);
        }
        bool caught = false;
        try
        {
            (void)static_cast<std::int64_t>(Number{9223372036854776, 3});
        }
        catch (std::overflow_error const&)
        {
            caught = true;
        }
        BEAST_EXPECT(caught);
    }

    void
    test_squelch()
    {
        testcase("test_squelch");
        Number limit{1, -6};
        BEAST_EXPECT((squelch(Number{2, -6}, limit) == Number{2, -6}));
        BEAST_EXPECT((squelch(Number{1, -6}, limit) == Number{1, -6}));
        BEAST_EXPECT((squelch(Number{9, -7}, limit) == Number{0}));
        BEAST_EXPECT((squelch(Number{-2, -6}, limit) == Number{-2, -6}));
        BEAST_EXPECT((squelch(Number{-1, -6}, limit) == Number{-1, -6}));
        BEAST_EXPECT((squelch(Number{-9, -7}, limit) == Number{0}));
    }

    void
    testToString()
    {
        testcase("testToString");
        BEAST_EXPECT(to_string(Number(-2, 0)) == "-2");
        BEAST_EXPECT(to_string(Number(0, 0)) == "0");
        BEAST_EXPECT(to_string(Number(2, 0)) == "2");
        BEAST_EXPECT(to_string(Number(25, -3)) == "0.025");
        BEAST_EXPECT(to_string(Number(-25, -3)) == "-0.025");
        BEAST_EXPECT(to_string(Number(25, 1)) == "250");
        BEAST_EXPECT(to_string(Number(-25, 1)) == "-250");
        BEAST_EXPECT(to_string(Number(2, 20)) == "2000000000000000e5");
        BEAST_EXPECT(to_string(Number(-2, -20)) == "-2000000000000000e-35");
    }

    void
    test_relationals()
    {
        testcase("test_relationals");
        BEAST_EXPECT(!(Number{100} < Number{10}));
        BEAST_EXPECT(Number{100} > Number{10});
        BEAST_EXPECT(Number{100} >= Number{10});
        BEAST_EXPECT(!(Number{100} <= Number{10}));
    }

    void
    test_stream()
    {
        testcase("test_stream");
        Number x{100};
        std::ostringstream os;
        os << x;
        BEAST_EXPECT(os.str() == to_string(x));
    }

    void
    test_inc_dec()
    {
        testcase("test_inc_dec");
        Number x{100};
        Number y = +x;
        BEAST_EXPECT(x == y);
        BEAST_EXPECT(x++ == y);
        BEAST_EXPECT(x == Number{101});
        BEAST_EXPECT(x-- == Number{101});
        BEAST_EXPECT(x == y);
    }

    void
    run() override
    {
        testZero();
        test_limits();
        test_add();
        test_sub();
        test_mul();
        test_div();
        test_root();
        test_power1();
        test_power2();
        testConversions();
        test_to_integer();
        test_squelch();
        testToString();
        test_relationals();
        test_stream();
        test_inc_dec();
    }
};

BEAST_DEFINE_TESTSUITE(Number, ripple_basics, ripple);

}  // namespace ripple
