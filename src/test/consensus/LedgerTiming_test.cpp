//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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
#include <ripple/beast/unit_test.h>
#include <ripple/consensus/LedgerTiming.h>

namespace ripple {
namespace test {

class LedgerTiming_test : public beast::unit_test::suite
{
    void testGetNextLedgerTimeResolution()
    {
        // helper to iteratively call into getNextLedgerTimeResolution
        struct test_res
        {
            std::uint32_t decrease = 0;
            std::uint32_t equal = 0;
            std::uint32_t increase = 0;

            static test_res run(bool previousAgree, std::uint32_t rounds)
            {
                test_res res;
                auto closeResolution = ledgerDefaultTimeResolution;
                auto nextCloseResolution = closeResolution;
                std::uint32_t round = 0;
                do
                {
                   nextCloseResolution = getNextLedgerTimeResolution(
                       closeResolution, previousAgree, ++round);
                   if (nextCloseResolution < closeResolution)
                       ++res.decrease;
                   else if (nextCloseResolution > closeResolution)
                       ++res.increase;
                   else
                       ++res.equal;
                   std::swap(nextCloseResolution, closeResolution);
                } while (round < rounds);
                return res;
            }
        };


        // If we never agree on close time, only can increase resolution
        // until hit the max
        auto decreases = test_res::run(false, 10);
        BEAST_EXPECT(decreases.increase == 3);
        BEAST_EXPECT(decreases.decrease == 0);
        BEAST_EXPECT(decreases.equal    == 7);

        // If we always agree on close time, only can decrease resolution
        // until hit the min
        auto increases = test_res::run(false, 100);
        BEAST_EXPECT(increases.increase == 3);
        BEAST_EXPECT(increases.decrease == 0);
        BEAST_EXPECT(increases.equal    == 97);

    }

    void testRoundCloseTime()
    {
        using namespace std::chrono_literals;
        // A closeTime equal to the epoch is not modified
        using tp = NetClock::time_point;
        tp def;
        BEAST_EXPECT(def == roundCloseTime(def, 30s));

        // Otherwise, the closeTime is rounded to the nearest
        // rounding up on ties
        BEAST_EXPECT(tp{ 0s } == roundCloseTime(tp{ 29s }, 60s));
        BEAST_EXPECT(tp{ 30s } == roundCloseTime(tp{ 30s }, 1s));
        BEAST_EXPECT(tp{ 60s } == roundCloseTime(tp{ 31s }, 60s));
        BEAST_EXPECT(tp{ 60s } == roundCloseTime(tp{ 30s }, 60s));
        BEAST_EXPECT(tp{ 60s } == roundCloseTime(tp{ 59s }, 60s));
        BEAST_EXPECT(tp{ 60s } == roundCloseTime(tp{ 60s }, 60s));
        BEAST_EXPECT(tp{ 60s } == roundCloseTime(tp{ 61s }, 60s));

    }

    void testEffCloseTime()
    {
        using namespace std::chrono_literals;
        using tp = NetClock::time_point;
        tp close = effCloseTime(tp{10s}, 30s, tp{0s});
        BEAST_EXPECT(close == tp{1s});

        close = effCloseTime(tp{16s}, 30s, tp{0s});
        BEAST_EXPECT(close == tp{30s});

        close = effCloseTime(tp{16s}, 30s, tp{30s});
        BEAST_EXPECT(close == tp{31s});

        close = effCloseTime(tp{16s}, 30s, tp{60s});
        BEAST_EXPECT(close == tp{61s});

        close = effCloseTime(tp{31s}, 30s, tp{0s});
        BEAST_EXPECT(close == tp{30s});
    }

    void
    run() override
    {
        testGetNextLedgerTimeResolution();
        testRoundCloseTime();
        testEffCloseTime();
    }

};

BEAST_DEFINE_TESTSUITE(LedgerTiming, consensus, ripple);
} // test
} // ripple
