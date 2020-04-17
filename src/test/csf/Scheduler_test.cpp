//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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
#include <set>
#include <test/csf/Scheduler.h>

namespace ripple {
namespace test {

class Scheduler_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        using namespace std::chrono_literals;
        csf::Scheduler scheduler;
        std::set<int> seen;

        scheduler.in(1s, [&] { seen.insert(1); });
        scheduler.in(2s, [&] { seen.insert(2); });
        auto token = scheduler.in(3s, [&] { seen.insert(3); });
        scheduler.at(scheduler.now() + 4s, [&] { seen.insert(4); });
        scheduler.at(scheduler.now() + 8s, [&] { seen.insert(8); });

        auto start = scheduler.now();

        // Process first event
        BEAST_EXPECT(seen.empty());
        BEAST_EXPECT(scheduler.step_one());
        BEAST_EXPECT(seen == std::set<int>({1}));
        BEAST_EXPECT(scheduler.now() == (start + 1s));

        // No processing if stepping until current time
        BEAST_EXPECT(scheduler.step_until(scheduler.now()));
        BEAST_EXPECT(seen == std::set<int>({1}));
        BEAST_EXPECT(scheduler.now() == (start + 1s));

        // Process next event
        BEAST_EXPECT(scheduler.step_for(1s));
        BEAST_EXPECT(seen == std::set<int>({1, 2}));
        BEAST_EXPECT(scheduler.now() == (start + 2s));

        // Don't process cancelled event, but advance clock
        scheduler.cancel(token);
        BEAST_EXPECT(scheduler.step_for(1s));
        BEAST_EXPECT(seen == std::set<int>({1, 2}));
        BEAST_EXPECT(scheduler.now() == (start + 3s));

        // Process until 3 seen ints
        BEAST_EXPECT(scheduler.step_while([&]() { return seen.size() < 3; }));
        BEAST_EXPECT(seen == std::set<int>({1, 2, 4}));
        BEAST_EXPECT(scheduler.now() == (start + 4s));

        // Process the rest
        BEAST_EXPECT(scheduler.step());
        BEAST_EXPECT(seen == std::set<int>({1, 2, 4, 8}));
        BEAST_EXPECT(scheduler.now() == (start + 8s));

        // Process the rest again doesn't advance
        BEAST_EXPECT(!scheduler.step());
        BEAST_EXPECT(seen == std::set<int>({1, 2, 4, 8}));
        BEAST_EXPECT(scheduler.now() == (start + 8s));
    }
};

BEAST_DEFINE_TESTSUITE(Scheduler, test, ripple);

}  // namespace test
}  // namespace ripple
