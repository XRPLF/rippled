//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2016 Ripple Labs Inc.

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
#include <ripple/ledger/CashDiff.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

class CashDiff_test : public beast::unit_test::suite
{

    // Exercise diffIsDust (STAmount, STAmount)
    void
    testDust ()
    {
        testcase ("diffIsDust (STAmount, STAmount)");

        Issue const usd (Currency (0x5553440000000000), AccountID (0x4985601));
        Issue const usf (Currency (0x5553460000000000), AccountID (0x4985601));

        // Positive and negative are never dust.
        expect (!diffIsDust (STAmount{usd, 1}, STAmount{usd, -1}));

        // Different issues are never dust.
        expect (!diffIsDust (STAmount{usd, 1}, STAmount{usf, 1}));

        // Native and non-native are never dust.
        expect (!diffIsDust (STAmount{usd, 1}, STAmount{1}));

        // Equal values are always dust.
        expect (diffIsDust (STAmount{0}, STAmount{0}));
        {
            // Test IOU.
            std::uint64_t oldProbe = 0;
            std::uint64_t newProbe = 10;
            std::uint8_t e10 = 1;
            do
            {
                STAmount large (usd, newProbe + 1);
                STAmount small (usd, newProbe);

                expect (diffIsDust (large, small, e10));
                expect (diffIsDust (large, small, e10+1) == (e10 > 13));

                oldProbe = newProbe;
                newProbe = oldProbe * 10;
                e10 += 1;
            } while (newProbe > oldProbe);
        }
        {
            // Test XRP.
            // A delta of 2 or less is always dust.
            expect (diffIsDust (STAmount{2}, STAmount{0}));

            std::uint64_t oldProbe = 0;
            std::uint64_t newProbe = 10;
            std::uint8_t e10 = 0;
            do
            {
                // Differences of 2 of fewer drops are always treated as dust,
                // so use a delta of 3.
                STAmount large (newProbe + 3);
                STAmount small (newProbe);

                expect (diffIsDust (large, small, e10));
                expect (diffIsDust (large, small, e10+1) == (e10 >= 20));

                oldProbe = newProbe;
                newProbe = oldProbe * 10;
                e10 += 1;
            } while (newProbe > oldProbe);
        }
    }

public:
    void run ()
    {
        testDust();
    }
};

BEAST_DEFINE_TESTSUITE (CashDiff, ledger, ripple);

}  // test
}  // ripple
