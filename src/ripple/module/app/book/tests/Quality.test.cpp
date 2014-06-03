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

#include <ripple/module/app/book/Quality.h>

#include <beast/unit_test/suite.h>
#include <beast/cxx14/type_traits.h>

namespace ripple {
namespace core {

class Quality_test : public beast::unit_test::suite
{
public:
    // Create a raw, non-integral amount from mantissa and exponent
    Amount
    static raw (std::uint64_t mantissa, int exponent)
    {
        return Amount (uint160(3), uint160(3), mantissa, exponent);
    }

    template <class Integer>
    static
    Amount
    amount (Integer integer,
        std::enable_if_t <std::is_signed <Integer>::value>* = 0)
    {
        static_assert (std::is_integral <Integer>::value, "");
        return Amount (integer, false);
    }

    template <class Integer>
    static
    Amount
    amount (Integer integer,
        std::enable_if_t <! std::is_signed <Integer>::value>* = 0)
    {
        static_assert (std::is_integral <Integer>::value, "");
        if (integer < 0)
            return Amount (-integer, true);
        return Amount (integer, false);
    }

    template <class In, class Out>
    static
    Amounts
    amounts (In in, Out out)
    {
        return Amounts (amount(in), amount(out));
    }

    template <class In1, class Out1, class Int, class In2, class Out2>
    void
    ceil_in (Quality const& q,
        In1 in, Out1 out, Int limit, In2 in_expected, Out2 out_expected)
    {
        auto expect_result (amounts (in_expected, out_expected));
        auto actual_result (q.ceil_in (amounts(in, out), amount(limit)));

        expect (actual_result == expect_result);
    }

    template <class In1, class Out1, class Int, class In2, class Out2>
    void
    ceil_out (Quality const& q,
        In1 in, Out1 out, Int limit, In2 in_expected, Out2 out_expected)
    {
        auto const expect_result (amounts (in_expected, out_expected));
        auto const actual_result (q.ceil_out (amounts(in, out), amount(limit)));

        expect (actual_result == expect_result);
    }

    void
    test_ceil_in ()
    {
        testcase ("ceil_in");

        {
            // 1 in, 1 out:
            Quality q (Amounts (amount(1), amount(1)));

            ceil_in (q,
                1,  1,   // 1 in, 1 out
                1,       // limit: 1
                1,  1);  // 1 in, 1 out

            ceil_in (q,
                10, 10, // 10 in, 10 out
                5,      // limit: 5
                5, 5);  // 5 in, 5 out

            ceil_in (q,
                5, 5,   // 5 in, 5 out
                10,     // limit: 10
                5, 5);  // 5 in, 5 out
        }

        {
            // 1 in, 2 out:
            Quality q (Amounts (amount(1), amount(2)));

            ceil_in (q,
                40, 80,   // 40 in, 80 out
                40,       // limit: 40
                40, 80);  // 40 in, 20 out

            ceil_in (q,
                40, 80,   // 40 in, 80 out
                20,       // limit: 20
                20, 40);  // 20 in, 40 out

            ceil_in (q,
                40, 80,   // 40 in, 80 out
                60,       // limit: 60
                40, 80);  // 40 in, 80 out
        }

        {
            // 2 in, 1 out:
            Quality q (Amounts (amount(2), amount(1)));

            ceil_in (q,
                40, 20,   // 40 in, 20 out
                20,       // limit: 20
                20, 10);  // 20 in, 10 out

            ceil_in (q,
                40, 20,   // 40 in, 20 out
                40,       // limit: 40
                40, 20);  // 40 in, 20 out

            ceil_in (q,
                40, 20,   // 40 in, 20 out
                50,       // limit: 40
                40, 20);  // 40 in, 20 out
        }
    }

    void
    test_ceil_out ()
    {
        testcase ("ceil_out");

        {
            // 1 in, 1 out:
            Quality q (Amounts (amount(1),amount(1)));

            ceil_out (q,
                1,  1,    // 1 in, 1 out
                1,        // limit 1
                1,  1);   // 1 in, 1 out

            ceil_out (q,
                10, 10,   // 10 in, 10 out
                5,        // limit 5
                5, 5);    // 5 in, 5 out

            ceil_out (q,
                10, 10,   // 10 in, 10 out
                20,       // limit 20
                10, 10);  // 10 in, 10 out
        }

        {
            // 1 in, 2 out:
            Quality q (Amounts (amount(1),amount(2)));

            ceil_out (q,
                40, 80,    // 40 in, 80 out
                40,        // limit 40
                20, 40);   // 20 in, 40 out

            ceil_out (q,
                40, 80,    // 40 in, 80 out
                80,        // limit 80
                40, 80);   // 40 in, 80 out

            ceil_out (q,
                40, 80,    // 40 in, 80 out
                100,       // limit 100
                40, 80);   // 40 in, 80 out
        }

        {
            // 2 in, 1 out:
            Quality q (Amounts (amount(2),amount(1)));

            ceil_out (q,
                40, 20,    // 40 in, 20 out
                20,        // limit 20
                40, 20);   // 40 in, 20 out

            ceil_out (q,
                40, 20,    // 40 in, 20 out
                40,        // limit 40
                40, 20);   // 40 in, 20 out

            ceil_out (q,
                40, 20,    // 40 in, 20 out
                10,        // limit 10
                20, 10);   // 20 in, 10 out
        }
    }

    void
    test_raw()
    {
        {
            Quality q (0x5d048191fb9130daull);      // 126836389.7680090
            Amounts const value (
                amount(349469768),                  // 349.469768 XRP
                raw (2755280000000000ull, -15));    // 2.75528
            Amount const limit (
                raw (4131113916555555, -16));       // .4131113916555555
            Amounts const result (q.ceil_out (value, limit));
            expect (result.in != zero);
        }
    }

    void
    run()
    {
        test_ceil_in ();
        test_ceil_out ();
        test_raw();
    }
};

BEAST_DEFINE_TESTSUITE(Quality,core,ripple);

}
}
