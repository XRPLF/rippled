#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/CoroTask.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>

#include <atomic>

namespace xrpl::test {

//------------------------------------------------------------------------------

class JobQueue_test : public beast::unit_test::Suite
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
                jQueue.addJob(JtClient, "JobAddTest1", [&jobRan]() { jobRan = true; }) == true);

            // Wait for the Job to run.
            while (!jobRan)
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
            bool unprotected = false;
            BEAST_EXPECT(jQueue.addJob(JtClient, "JobAddTest2", [&unprotected]() {
                unprotected = false;
            }) == false);
        }
    }

    // NOTE: All coroutine lambdas passed to postCoroTask use explicit
    // pointer-by-value captures instead of [&] to work around a GCC 14
    // bug where reference captures in coroutine lambdas are corrupted
    // in the coroutine frame.

    void
    testPostCoroTask()
    {
        jtx::Env env{*this};

        JobQueue& jQueue = env.app().getJobQueue();
        {
            // Test repeated post()s until the coroutine completes.
            std::atomic<int> yieldCount{0};
            // Safe capture: the test blocks below until the coroutine
            // completes, so the captured pointer outlives the coroutine.
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
            auto const runner = jQueue.postCoroTask(
                JtClient, "PostCoroTest1", [ycp = &yieldCount](auto runner) -> CoroTask<void> {
                    while (++(*ycp) < 4)
                        co_await runner->suspend();
                    co_return;
                });
            BEAST_EXPECT(runner != nullptr);

            // Wait for the Job to run and yield.
            while (yieldCount == 0)
                ;

            // Now re-post until the CoroTaskRunner says it is done.
            int old = yieldCount;
            while (runner->runnable())
            {
                BEAST_EXPECT(runner->post());
                while (old == yieldCount)
                {
                }
                runner->join();
                BEAST_EXPECT(++old == yieldCount);
            }
            BEAST_EXPECT(yieldCount == 4);
        }
        {
            // Test repeated post()+join()s until the coroutine completes.
            int yieldCount{0};
            // Safe capture: the test blocks below until the coroutine
            // completes, so the captured pointer outlives the coroutine.
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
            auto const runner = jQueue.postCoroTask(
                JtClient, "PostCoroTest2", [ycp = &yieldCount](auto runner) -> CoroTask<void> {
                    while (++(*ycp) < 4)
                        co_await runner->suspend();
                    co_return;
                });
            if (!runner)
            {
                // There's no good reason we should not get a runner, but we
                // can't continue without one.
                BEAST_EXPECT(false);
                return;
            }

            // Wait for the Job to run and yield.
            runner->join();

            // Now post()+join() until the CoroTaskRunner says it is done.
            // resume() requires a prior post() (see the precondition on
            // CoroTaskRunner::resume()), so the posted job performs the
            // resume and join() blocks until it completes. yieldCount is
            // deliberately not atomic: the mutexRun_ handoff inside join()
            // must provide the happens-before edge that makes the
            // increment visible to this thread.
            int old = yieldCount;
            while (runner->runnable())
            {
                BEAST_EXPECT(runner->post());
                runner->join();
                BEAST_EXPECT(++old == yieldCount);
            }
            BEAST_EXPECT(yieldCount == 4);
        }
        {
            // If the JobQueue is stopped, we should no
            // longer be able to post a coroutine (and calling postCoroTask()
            // should return nullptr).
            using namespace std::chrono_literals;
            jQueue.stop();

            // The coroutine should never run, so having it access this
            // unprotected variable on the stack should be completely safe.
            // Not recommended for the faint of heart...
            bool unprotected = false;
            // Safe capture: the JobQueue is stopped, so the coroutine is
            // never started and the captured pointer is never dereferenced.
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
            auto const runner = jQueue.postCoroTask(
                JtClient, "PostCoroTest3", [up = &unprotected](auto) -> CoroTask<void> {
                    *up = true;
                    co_return;
                });
            BEAST_EXPECT(runner == nullptr);
            BEAST_EXPECT(unprotected == false);
        }
    }

public:
    void
    run() override
    {
        testAddJob();
        testPostCoroTask();
    }
};

BEAST_DEFINE_TESTSUITE(JobQueue, core, xrpl);

}  // namespace xrpl::test
