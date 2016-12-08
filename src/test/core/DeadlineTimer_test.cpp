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

#include <ripple/core/DeadlineTimer.h>
#include <ripple/beast/unit_test.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace ripple {

//------------------------------------------------------------------------------

class DeadlineTimer_test : public beast::unit_test::suite
{
public:
    struct TestCallback : DeadlineTimer::Listener
    {
        TestCallback() = default;

        void onDeadlineTimer (DeadlineTimer&) override
        {
            ++count;
        }

        std::atomic<int> count {0};
    };

    void testExpiration()
    {
        using clock = DeadlineTimer::clock;

        using namespace std::chrono_literals;
        using namespace std::this_thread;

        TestCallback cb;
        DeadlineTimer dt {&cb};

        // There are parts of this test that are somewhat race conditional.
        // The test is designed to avoid spurious failures, rather than
        // fail occasionally but randomly, whereever possible.  So there may
        // be occasional gratuitous passes.  Unfortunately, since it is a
        // time-based test, there may also be occasional spurious failures
        // on low-powered continuous integration platforms.
        {
            testcase("Expiration");

            // Set a deadline timer that should only fire once in 5ms.
            cb.count = 0;
            auto const startTime = clock::now();
            dt.setExpiration (5ms);

             // Make sure the timer didn't fire immediately.
            int const count = cb.count.load();
            if (clock::now() < startTime + 4ms)
            {
                BEAST_EXPECT (count == 0);
            }

            // Wait until the timer should have fired and check that it did.
            // In fact, we wait long enough that if it were to fire multiple
            // times we'd see that.
            sleep_until (startTime + 50ms);
            BEAST_EXPECT (cb.count.load() == 1);
        }
        {
            testcase("RecurringExpiration");

            // Set a deadline timer that should fire once every 5ms.
            cb.count = 0;
            auto const startTime = clock::now();
            dt.setRecurringExpiration (5ms);

             // Make sure the timer didn't fire immediately.
            {
                int const count = cb.count.load();
                if (clock::now() < startTime + 4ms)
                {
                    BEAST_EXPECT (count == 0);
                }
            }

            // Wait until the timer should have fired several times and
            // check that it did.
            sleep_until (startTime + 100ms);
            {
                auto const count = cb.count.load();
                BEAST_EXPECT ((count > 1) && (count < 21));
            }

            // Cancel the recurring timer and it should not fire any more.
            dt.cancel();
            auto const count = cb.count.load();
            sleep_for (50ms);
            BEAST_EXPECT (count == cb.count.load());
        }
    }

    void run()
    {
        testExpiration();
    }
};

BEAST_DEFINE_TESTSUITE(DeadlineTimer, core, ripple);

}