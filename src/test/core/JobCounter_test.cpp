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

#include <BeastConfig.h>
#include <ripple/core/JobCounter.h>
#include <ripple/beast/unit_test.h>
#include <test/jtx/Env.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace ripple {
namespace test {

//------------------------------------------------------------------------------

class JobCounter_test : public beast::unit_test::suite
{
    // We're only using Env for its Journal.
    jtx::Env env {*this};
    beast::Journal j {env.app().journal ("JobCounter_test")};

    void testWrap()
    {
        // Verify reference counting.
        JobCounter jobCounter;
        BEAST_EXPECT (jobCounter.count() == 0);
        {
            auto wrapped1 = jobCounter.wrap ([] (Job&) {});
            BEAST_EXPECT (jobCounter.count() == 1);

            // wrapped1 should be callable with a Job.
            {
                Job job;
                (*wrapped1)(job);
            }
            {
                // Copy should increase reference count.
                auto wrapped2 (wrapped1);
                BEAST_EXPECT (jobCounter.count() == 2);
                {
                    // Move should increase reference count.
                    auto wrapped3 (std::move(wrapped2));
                    BEAST_EXPECT (jobCounter.count() == 3);
                    {
                        // An additional Job also increases count.
                        auto wrapped4 = jobCounter.wrap ([] (Job&) {});
                        BEAST_EXPECT (jobCounter.count() == 4);
                    }
                    BEAST_EXPECT (jobCounter.count() == 3);
                }
                BEAST_EXPECT (jobCounter.count() == 2);
            }
            BEAST_EXPECT (jobCounter.count() == 1);
        }
        BEAST_EXPECT (jobCounter.count() == 0);

        // Join with 0 count should not stall.
        using namespace std::chrono_literals;
        jobCounter.join("testWrap", 1ms, j);

        // Wrapping a Job after join() should return boost::none.
        BEAST_EXPECT (jobCounter.wrap ([] (Job&) {}) == boost::none);
    }

    void testWaitOnJoin()
    {
        // Verify reference counting.
        JobCounter jobCounter;
        BEAST_EXPECT (jobCounter.count() == 0);

        auto job = (jobCounter.wrap ([] (Job&) {}));
        BEAST_EXPECT (jobCounter.count() == 1);

        // Calling join() now should stall, so do it on a different thread.
        std::atomic<bool> threadExited {false};
        std::thread localThread ([&jobCounter, &threadExited, this] ()
        {
            // Should stall after calling join.
            using namespace std::chrono_literals;
            jobCounter.join("testWaitOnJoin", 1ms, j);
            threadExited.store (true);
        });

        // Wait for the thread to call jobCounter.join().
        while (! jobCounter.joined());

        // The thread should still be active after waiting 5 milliseconds.
        // This is not a guarantee that join() stalled the thread, but it
        // improves confidence.
        using namespace std::chrono_literals;
        std::this_thread::sleep_for (5ms);
        BEAST_EXPECT (threadExited == false);

        // Destroy the Job and expect the thread to exit (asynchronously).
        job = boost::none;
        BEAST_EXPECT (jobCounter.count() == 0);

        // Wait for the thread to exit.
        while (threadExited == false);
        localThread.join();
    }

public:
    void run()
    {
        testWrap();
        testWaitOnJoin();
    }
};

BEAST_DEFINE_TESTSUITE(JobCounter, core, ripple);

} // test
} // ripple
