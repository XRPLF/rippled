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
#include <ripple/core/JobQueue.h>
#include <test/jtx/Env.h>

namespace ripple {
namespace test {

//------------------------------------------------------------------------------

class JobQueue_test : public beast::unit_test::suite
{
    void
    testAddJob()
    {
        jtx::Env env{*this};

        JobQueue& jQueue = env.app().getJobQueue();
        {
            // addJob() should run the Job (and return true).
            std::atomic<bool> jobRan{false};
            BEAST_EXPECT(
                jQueue.addJob(jtCLIENT, "JobAddTest1", [&jobRan](Job&) {
                    jobRan = true;
                }) == true);

            // Wait for the Job to run.
            while (jobRan == false)
                ;
        }
        {
            // If the JobQueue is stopped, we should no
            // longer be able to add Jobs (and calling addJob() should
            // return false).
            using namespace std::chrono_literals;
            jQueue.stop();

            // The Job should never run, so having the Job access this
            // unprotected variable on the stack should be completely safe.
            // Not recommended for the faint of heart...
            bool unprotected;
            BEAST_EXPECT(
                jQueue.addJob(jtCLIENT, "JobAddTest2", [&unprotected](Job&) {
                    unprotected = false;
                }) == false);
        }
    }

    void
    testPostCoro()
    {
        jtx::Env env{*this};

        JobQueue& jQueue = env.app().getJobQueue();
        {
            // Test repeated post()s until the Coro completes.
            std::atomic<int> yieldCount{0};
            auto const coro = jQueue.postCoro(
                jtCLIENT,
                "PostCoroTest1",
                [&yieldCount](std::shared_ptr<JobQueue::Coro> const& coroCopy) {
                    while (++yieldCount < 4)
                        coroCopy->yield();
                });
            BEAST_EXPECT(coro != nullptr);

            // Wait for the Job to run and yield.
            while (yieldCount == 0)
                ;

            // Now re-post until the Coro says it is done.
            int old = yieldCount;
            while (coro->runnable())
            {
                BEAST_EXPECT(coro->post());
                while (old == yieldCount)
                {
                }
                coro->join();
                BEAST_EXPECT(++old == yieldCount);
            }
            BEAST_EXPECT(yieldCount == 4);
        }
        {
            // Test repeated resume()s until the Coro completes.
            int yieldCount{0};
            auto const coro = jQueue.postCoro(
                jtCLIENT,
                "PostCoroTest2",
                [&yieldCount](std::shared_ptr<JobQueue::Coro> const& coroCopy) {
                    while (++yieldCount < 4)
                        coroCopy->yield();
                });
            if (!coro)
            {
                // There's no good reason we should not get a Coro, but we
                // can't continue without one.
                BEAST_EXPECT(false);
                return;
            }

            // Wait for the Job to run and yield.
            coro->join();

            // Now resume until the Coro says it is done.
            int old = yieldCount;
            while (coro->runnable())
            {
                coro->resume();  // Resume runs synchronously on this thread.
                BEAST_EXPECT(++old == yieldCount);
            }
            BEAST_EXPECT(yieldCount == 4);
        }
        {
            // If the JobQueue is stopped, we should no
            // longer be able to add a Coro (and calling postCoro() should
            // return false).
            using namespace std::chrono_literals;
            jQueue.stop();

            // The Coro should never run, so having the Coro access this
            // unprotected variable on the stack should be completely safe.
            // Not recommended for the faint of heart...
            bool unprotected;
            auto const coro = jQueue.postCoro(
                jtCLIENT,
                "PostCoroTest3",
                [&unprotected](std::shared_ptr<JobQueue::Coro> const&) {
                    unprotected = false;
                });
            BEAST_EXPECT(coro == nullptr);
        }
    }

public:
    void
    run() override
    {
        testAddJob();
        testPostCoro();
    }
};

BEAST_DEFINE_TESTSUITE(JobQueue, core, ripple);

}  // namespace test
}  // namespace ripple
