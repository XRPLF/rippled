#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/JobTypes.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <numeric>
#include <thread>

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

    void
    testPostCoro()
    {
        jtx::Env env{*this};

        JobQueue& jQueue = env.app().getJobQueue();
        {
            // Test repeated post()s until the Coro completes.
            std::atomic<int> yieldCount{0};
            auto const coro = jQueue.postCoro(
                JtClient,
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
                JtClient,
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
            bool unprotected = false;
            auto const coro = jQueue.postCoro(
                JtClient, "PostCoroTest3", [&unprotected](std::shared_ptr<JobQueue::Coro> const&) {
                    unprotected = false;
                });
            BEAST_EXPECT(coro == nullptr);
        }
    }

    /**
     * The telemetry accessors added for the job-queue saturation gauges.
     *
     * These feed `jobq_backlog{metric,job_type}` and `jobq_saturation{metric}`,
     * which are polled from an xrpld observable-gauge callback. The values are
     * asserted exactly, because the whole point of the signals is that a
     * specific count (especially `deferred`) is correct -- a plausible-looking
     * number would misreport starvation as health.
     */
    void
    testTelemetryAccessors()
    {
        testcase("telemetry occupancy accessors");

        jtx::Env env{*this};
        JobQueue& jQueue = env.app().getJobQueue();

        // --- Every registered type is present, and an idle one reads zero ---
        // Absence and zero must be distinguishable: the gauge observes every
        // type on every tick, so a missing type would be an exporter bug, not
        // an idle queue.
        auto const counts = jQueue.getJobTypeCounts();

        // One entry per registered JobType. JobTypes is the registry the
        // JobQueue constructor populates jobData_ from, so the sizes must
        // agree exactly -- a mismatch means a type is silently unreported.
        BEAST_EXPECT(counts.size() == JobTypes::instance().size());

        auto findType = [&counts](JobType t) {
            return std::ranges::find_if(counts, [t](auto const& c) { return c.type == t; });
        };

        // A sync-critical type is present even though nothing has been
        // enqueued for it, and reads exactly zero on all three fields.
        auto const ledgerData = findType(JtLedgerData);
        BEAST_EXPECT(ledgerData != counts.end());
        if (ledgerData != counts.end())
        {
            BEAST_EXPECT(ledgerData->waiting == 0);
            BEAST_EXPECT(ledgerData->running == 0);
            BEAST_EXPECT(ledgerData->deferred == 0);
        }

        // JtInvalid is NOT a registered type: jobData_ is built from
        // JobTypes, whose map excludes it. So the snapshot must not carry it.
        BEAST_EXPECT(findType(JtInvalid) == counts.end());

        // --- The per-type snapshot agrees with the existing accessor ---
        // getJobTypeCounts() must not re-implement counting: `waiting` is the
        // same field getJobCount() returns, and `waiting + running` is what
        // getJobCountTotal() returns. Asserted on every type so a divergence
        // anywhere fails, not just on the one type a test happens to poke.
        for (auto const& count : counts)
        {
            BEAST_EXPECT(count.waiting == jQueue.getJobCount(count.type));
            BEAST_EXPECT(count.waiting + count.running == jQueue.getJobCountTotal(count.type));
        }

        // --- Saturation: the pool reports its own capacity ---
        auto const saturation = jQueue.getWorkerSaturation();

        // jtx::Env runs standalone, which the JobQueue ctor maps to exactly
        // one worker thread. This is the dashboard ratio's denominator, so it
        // must be the real configured count and never zero (a zero would make
        // the ratio undefined).
        BEAST_EXPECT(saturation.workerThreads == 1);

        // totalWaiting is the sum of the per-type waiting counts from the same
        // fields, so the two accessors must agree.
        int const summedWaiting =
            std::accumulate(counts.begin(), counts.end(), 0, [](int acc, auto const& c) {
                return acc + c.waiting;
            });
        BEAST_EXPECT(saturation.totalWaiting == summedWaiting);

        // An in-flight task count can never exceed the configured pool size.
        BEAST_EXPECT(saturation.runningTasks >= 0);
        BEAST_EXPECT(saturation.runningTasks <= saturation.workerThreads);

        // --- A queued job is actually observed ---
        // Block one job inside its handler so the queue provably holds work
        // while it is sampled: without this the sample could race the job to
        // completion and read zeros, which would pass vacuously.
        std::atomic<bool> release{false};
        std::atomic<bool> started{false};
        BEAST_EXPECT(jQueue.addJob(JtClient, "OccupancyBlocker", [&release, &started]() {
            started = true;
            while (!release)
                std::this_thread::yield();
        }));

        while (!started)
            std::this_thread::yield();

        // With the single standalone worker occupied, the type reports exactly
        // one job running.
        auto const busy = jQueue.getJobTypeCounts();
        auto const busyClient =
            std::ranges::find_if(busy, [](auto const& c) { return c.type == JtClient; });
        BEAST_EXPECT(busyClient != busy.end());
        if (busyClient != busy.end())
            BEAST_EXPECT(busyClient->running == 1);

        // And the pool reports exactly one task in flight out of one thread:
        // a fully saturated pool, which is the reading the gauge exists for.
        auto const busySaturation = jQueue.getWorkerSaturation();
        BEAST_EXPECT(busySaturation.runningTasks == 1);
        BEAST_EXPECT(busySaturation.workerThreads == 1);

        release = true;
        jQueue.rendezvous();

        // After draining, the same type reads zero again -- the counters are
        // live readings, not a high-water mark. rendezvous() returns with the
        // queue mutex having seen finishJob(), so the running count is settled
        // by here. (Workers::runningTaskCount_ is decremented only after
        // processTask returns, which rendezvous does not wait for, so it is
        // deliberately not asserted at this point.)
        auto const drained = jQueue.getJobTypeCounts();
        auto const drainedClient =
            std::ranges::find_if(drained, [](auto const& c) { return c.type == JtClient; });
        BEAST_EXPECT(drainedClient != drained.end());
        if (drainedClient != drained.end())
        {
            BEAST_EXPECT(drainedClient->waiting == 0);
            BEAST_EXPECT(drainedClient->running == 0);
        }
    }

public:
    void
    run() override
    {
        testAddJob();
        testPostCoro();
        testTelemetryAccessors();
    }
};

BEAST_DEFINE_TESTSUITE(JobQueue, core, xrpl);

}  // namespace xrpl::test
