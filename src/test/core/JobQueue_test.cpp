#include <test/jtx/Env.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Counter.h>
#include <xrpl/beast/insight/CounterImpl.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/insight/EventImpl.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/GaugeImpl.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/HookImpl.h>
#include <xrpl/beast/insight/Meter.h>
#include <xrpl/beast/insight/MeterImpl.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/JobTypeData.h>
#include <xrpl/core/JobTypes.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/json/json_value.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xrpl::test {

namespace {

/**
 * A beast::insight::Collector that remembers the last value set on every
 * gauge it created, keyed by gauge name.
 *
 * Needed because the production collectors are write-only sinks: StatsD
 * sends over UDP and NullCollector discards. `JobQueue::collect()` assigns
 * to the per-job-type gauges, so the only way to assert on the published
 * values in-process is to supply a Collector that keeps them.
 *
 *   RecordingCollector  (Collector)
 *     |  makeGauge(name)
 *     v
 *   RecordingGaugeImpl  (GaugeImpl) --writes--> Values (shared, mutex-guarded)
 *     ^                                            ^
 *     |                                            |
 *   JobQueue::collect() assigns          gaugeValue(name) reads
 *
 * Only gauges are recorded; counters, events, meters and hooks get inert
 * implementations, because no assertion here needs them. The hook handler is
 * kept so the test can invoke the collection pass on demand rather than
 * waiting for a collector's own timer -- that makes the reads deterministic.
 *
 * @note Thread-safe: the value map is guarded by its own mutex, because
 * `JobQueue::collect()` may run on a different thread from the assertions.
 */
class RecordingCollector : public beast::insight::Collector
{
public:
    /**
     * Shared storage so gauge handles outliving the collector stay valid.
     */
    struct Values
    {
        std::mutex mutex;
        std::map<std::string, beast::insight::GaugeImpl::value_type> gauges;
    };

private:
    /**
     * A gauge that writes each assigned value into the shared map.
     */
    class RecordingGaugeImpl : public beast::insight::GaugeImpl
    {
        std::shared_ptr<Values> values_;
        std::string name_;

    public:
        RecordingGaugeImpl(std::shared_ptr<Values> values, std::string name)
            : values_(std::move(values)), name_(std::move(name))
        {
        }

        void
        set(value_type value) override
        {
            std::scoped_lock const lock(values_->mutex);
            values_->gauges[name_] = value;
        }

        void
        increment(difference_type amount) override
        {
            std::scoped_lock const lock(values_->mutex);
            values_->gauges[name_] += static_cast<value_type>(amount);
        }
    };

    /** @{ */
    /**
     * Inert implementations for the metric kinds no assertion reads.
     */
    class InertCounterImpl : public beast::insight::CounterImpl
    {
        void
        increment(value_type) override
        {
        }
    };

    class InertEventImpl : public beast::insight::EventImpl
    {
        void
        notify(value_type const&) override
        {
        }
    };

    class InertMeterImpl : public beast::insight::MeterImpl
    {
        void
        increment(value_type) override
        {
        }
    };

    /**
     * A hook that owns its handler, so releasing the Hook releases the
     * handler with it.
     *
     * The collector holds only a weak reference (see `hooks_`). That
     * reproduces the documented beast::insight lifetime rule -- "when the
     * last reference goes away, the metric is no longer collected" -- and
     * matters here because `JobQueue::~JobQueue()` unhooks by assigning an
     * empty Hook. A collector holding the handler strongly would keep
     * calling back into a destroyed JobQueue.
     */
    class InertHookImpl : public beast::insight::HookImpl
    {
    public:
        explicit InertHookImpl(HandlerType handler) : handler_(std::move(handler))
        {
        }

        void
        invoke() const
        {
            if (handler_)
                handler_();
        }

    private:
        HandlerType handler_;
    };
    /** @} */

    std::shared_ptr<Values> values_{std::make_shared<Values>()};
    std::vector<std::weak_ptr<InertHookImpl>> hooks_;

public:
    beast::insight::Hook
    makeHook(beast::insight::HookImpl::HandlerType const& handler) override
    {
        auto impl = std::make_shared<InertHookImpl>(handler);
        hooks_.push_back(impl);
        return beast::insight::Hook(std::move(impl));
    }

    beast::insight::Counter
    makeCounter(std::string const&) override
    {
        return beast::insight::Counter(std::make_shared<InertCounterImpl>());
    }

    beast::insight::Event
    makeEvent(std::string const&) override
    {
        return beast::insight::Event(std::make_shared<InertEventImpl>());
    }

    beast::insight::Gauge
    makeGauge(std::string const& name) override
    {
        return beast::insight::Gauge(std::make_shared<RecordingGaugeImpl>(values_, name));
    }

    beast::insight::Meter
    makeMeter(std::string const&) override
    {
        return beast::insight::Meter(std::make_shared<InertMeterImpl>());
    }

    /**
     * Run every still-live hook, i.e. force one collection pass.
     *
     * Expired hooks are skipped rather than resurrected, so calling this
     * after the JobQueue has been destroyed is a no-op instead of a
     * use-after-free.
     */
    void
    runHooks() const
    {
        for (auto const& weak : hooks_)
        {
            if (auto const hook = weak.lock())
                hook->invoke();
        }
    }

    /**
     * The last value published for @p name.
     *
     * @return The value, or std::nullopt when no gauge of that name has
     * ever been written -- which distinguishes "gauge absent" from
     * "gauge present and reading zero".
     */
    [[nodiscard]] std::optional<beast::insight::GaugeImpl::value_type>
    gaugeValue(std::string const& name) const
    {
        std::scoped_lock const lock(values_->mutex);
        auto const iter = values_->gauges.find(name);
        if (iter == values_->gauges.end())
            return std::nullopt;
        return iter->second;
    }
};

/**
 * A perf::PerfLog that ignores everything.
 *
 * JobQueue requires a PerfLog reference; these tests assert on gauges, not
 * on the perf hooks, so every override is empty.
 */
class SilentPerfLog : public perf::PerfLog
{
    void
    rpcStart(std::string const&, std::uint64_t) override
    {
    }
    void
    rpcFinish(std::string const&, std::uint64_t) override
    {
    }
    void
    rpcError(std::string const&, std::uint64_t) override
    {
    }
    void
    jobQueue(JobType, std::string const&) override
    {
    }
    void
    jobStart(
        JobType,
        std::string const&,
        std::chrono::microseconds,
        std::chrono::time_point<std::chrono::steady_clock>,
        int) override
    {
    }
    void
    jobFinish(JobType, std::string const&, std::chrono::microseconds, int) override
    {
    }
    [[nodiscard]] json::Value
    countersJson() const override
    {
        return json::Value();
    }
    [[nodiscard]] json::Value
    currentJson() const override
    {
        return json::Value();
    }
    void
    resizeJobs(int) override
    {
    }
    void
    rotate() override
    {
    }
};

// Gauge-name suffixes. Aliased from JobTypeData rather than re-spelled, so a
// rename there fails here instead of silently asserting on a stale name.
/** @{ */
constexpr auto& kSuffixWaiting = JobTypeData::kSuffixWaiting;
constexpr auto& kSuffixRunning = JobTypeData::kSuffixRunning;
constexpr auto& kSuffixDeferred = JobTypeData::kSuffixDeferred;
/** @} */

}  // namespace

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

    //--------------------------------------------------------------------------
    // Per-job-type saturation gauges (waiting / running / deferred)
    //--------------------------------------------------------------------------

    /**
     * Owns a JobQueue wired to a RecordingCollector.
     *
     * A standalone JobQueue rather than `env.app().getJobQueue()`, for two
     * reasons: the application's queue uses a write-only collector whose
     * gauge values cannot be read back, and it carries background jobs whose
     * timing would make exact counts unreproducible. Constructed with the
     * given thread count so a concurrency limit can be exceeded on demand.
     */
    struct GaugeFixture
    {
        Logs logs{beast::Severity::Disabled};
        SilentPerfLog perfLog;
        std::shared_ptr<RecordingCollector> collector{std::make_shared<RecordingCollector>()};
        JobQueue queue;

        explicit GaugeFixture(int threadCount)
            : queue(threadCount, collector, logs.journal("JobQueue"), logs, perfLog)
        {
        }

        /**
         * Publish one collection pass, then read a gauge by job type.
         */
        [[nodiscard]] std::optional<beast::insight::GaugeImpl::value_type>
        read(JobType type, char const* suffix) const
        {
            collector->runHooks();
            return collector->gaugeValue(JobTypes::name(type) + suffix);
        }
    };

    /**
     * A gauge exists for a limited job type and not for a special one.
     *
     * Creation is observed through the RecordingCollector: a name no gauge
     * was created for is absent from its value map even after a collection
     * pass, whereas a created one is present. That distinguishes "never
     * created" from "created and reading 0", which a value check alone
     * cannot.
     */
    void
    testGaugeCreation()
    {
        testcase("Saturation gauge creation");

        GaugeFixture fixture(1);

        // JtLedgerReq has limit 3, so it is not special and must be gauged.
        BEAST_EXPECT(!JobTypes::instance().get(JtLedgerReq).special());
        BEAST_EXPECT(JobTypes::instance().get(JtLedgerReq).limit() == 3);
        BEAST_EXPECT(fixture.read(JtLedgerReq, kSuffixWaiting).has_value());
        BEAST_EXPECT(fixture.read(JtLedgerReq, kSuffixRunning).has_value());
        BEAST_EXPECT(fixture.read(JtLedgerReq, kSuffixDeferred).has_value());

        // A quiescent queue publishes exactly zero, not "no value".
        BEAST_EXPECT(fixture.read(JtLedgerReq, kSuffixWaiting) == 0u);
        BEAST_EXPECT(fixture.read(JtLedgerReq, kSuffixRunning) == 0u);
        BEAST_EXPECT(fixture.read(JtLedgerReq, kSuffixDeferred) == 0u);

        // JtPeer is special (limit_ == 0) and must have no gauges at all.
        BEAST_EXPECT(JobTypes::instance().get(JtPeer).special());
        BEAST_EXPECT(JobTypes::instance().get(JtPeer).limit() == 0);
        BEAST_EXPECT(!fixture.read(JtPeer, kSuffixWaiting).has_value());
        BEAST_EXPECT(!fixture.read(JtPeer, kSuffixRunning).has_value());
        BEAST_EXPECT(!fixture.read(JtPeer, kSuffixDeferred).has_value());
    }

    /**
     * Driving a capped type past its limit reports exact running and
     * deferred counts, and deferred returns to exactly 0 once drained.
     *
     * JtPack is used because its limit is 1, which makes both figures
     * unambiguous: with the single slot occupied by a job that blocks until
     * released, every further submission must defer.
     */
    void
    testDeferredGaugeExactValues()
    {
        testcase("Saturation gauge deferred counts");

        int const limit = JobTypes::instance().get(JtPack).limit();
        BEAST_EXPECT(limit == 1);

        // More threads than the type's limit, so `running` is capped by the
        // job type rather than by thread availability.
        GaugeFixture fixture(4);

        // The first job blocks inside doJob() until released, holding the
        // one slot. `entered` proves it really is running before the
        // remaining jobs are submitted.
        std::mutex mutex;
        std::condition_variable cv;
        bool release = false;
        int entered = 0;

        auto blockingJob = [&]() {
            std::unique_lock lock(mutex);
            ++entered;
            cv.notify_all();
            cv.wait(lock, [&release] { return release; });
        };

        BEAST_EXPECT(fixture.queue.addJob(JtPack, "GaugeHold", blockingJob));
        {
            std::unique_lock lock(mutex);
            BEAST_EXPECT(
                cv.wait_for(lock, std::chrono::seconds(10), [&entered] { return entered == 1; }));
        }

        // Every one of these must defer: the limit is already reached.
        int const extra = 4;
        for (int i = 0; i < extra; ++i)
            BEAST_EXPECT(fixture.queue.addJob(JtPack, "GaugeDefer", blockingJob));

        // Exact values, not bounds: `running` equals the type's limit and
        // `deferred` equals the number of submissions beyond it. Both are
        // deterministic here because no JtPack job can complete until
        // `release` is set.
        BEAST_EXPECT(fixture.read(JtPack, kSuffixRunning) == static_cast<std::uint64_t>(limit));
        BEAST_EXPECT(fixture.read(JtPack, kSuffixDeferred) == static_cast<std::uint64_t>(extra));

        // `waiting` counts everything submitted and not yet started, which
        // is the deferred jobs -- the running one was decremented when
        // getNextJob() picked it up.
        BEAST_EXPECT(fixture.read(JtPack, kSuffixWaiting) == static_cast<std::uint64_t>(extra));

        // Cross-check against the public accessors, so a gauge that silently
        // published a stale or unrelated number would be caught.
        BEAST_EXPECT(fixture.queue.getJobCount(JtPack) == extra);
        BEAST_EXPECT(fixture.queue.getJobCountTotal(JtPack) == extra + limit);

        // Release everything and drain.
        {
            std::scoped_lock const lock(mutex);
            release = true;
        }
        cv.notify_all();
        fixture.queue.stop();
        BEAST_EXPECT(fixture.queue.isStopped());

        // All five jobs ran, so the backlog is gone: deferred is exactly 0,
        // and so are waiting and running.
        BEAST_EXPECT(entered == extra + limit);
        BEAST_EXPECT(fixture.read(JtPack, kSuffixDeferred) == 0u);
        BEAST_EXPECT(fixture.read(JtPack, kSuffixWaiting) == 0u);
        BEAST_EXPECT(fixture.read(JtPack, kSuffixRunning) == 0u);
    }

    /**
     * Negative path: an uncapped job type never reports non-zero deferred.
     *
     * JtClient's limit is std::numeric_limits<int>::max(), so
     * `addRefCountedJob()` can never take the `++data.deferred` branch. The
     * gauge must therefore read exactly 0 both while jobs are in flight and
     * after the queue drains -- otherwise a dashboard would attribute
     * backpressure to a type that cannot experience it.
     */
    void
    testUncappedTypeNeverDefers()
    {
        testcase("Saturation gauge uncapped type");

        int const limit = JobTypes::instance().get(JtClient).limit();
        BEAST_EXPECT(limit == std::numeric_limits<int>::max());
        BEAST_EXPECT(!JobTypes::instance().get(JtClient).special());

        // One thread, so submissions greatly outnumber the workers that can
        // service them. Under a capped type this would defer; here it must
        // not, which is what separates "waiting" from "deferred".
        GaugeFixture fixture(1);

        std::mutex mutex;
        std::condition_variable cv;
        bool release = false;
        int entered = 0;

        int const jobs = 6;
        for (int i = 0; i < jobs; ++i)
        {
            BEAST_EXPECT(fixture.queue.addJob(JtClient, "GaugeUncapped", [&]() {
                std::unique_lock lock(mutex);
                ++entered;
                cv.notify_all();
                cv.wait(lock, [&release] { return release; });
            }));
        }

        // At least one job is in flight and the rest are backlogged, yet
        // deferred stays at exactly 0 because the type has no limit.
        {
            std::unique_lock lock(mutex);
            BEAST_EXPECT(
                cv.wait_for(lock, std::chrono::seconds(10), [&entered] { return entered >= 1; }));
        }
        BEAST_EXPECT(fixture.read(JtClient, kSuffixDeferred) == 0u);

        {
            std::scoped_lock const lock(mutex);
            release = true;
        }
        cv.notify_all();
        fixture.queue.stop();

        BEAST_EXPECT(entered == jobs);
        BEAST_EXPECT(fixture.read(JtClient, kSuffixDeferred) == 0u);
        BEAST_EXPECT(fixture.read(JtClient, kSuffixWaiting) == 0u);
        BEAST_EXPECT(fixture.read(JtClient, kSuffixRunning) == 0u);
    }

    /**
     * Exactly the non-special job types are gauged, three gauges each, and
     * every published value is non-negative.
     *
     * The coverage half pins the cardinality the metric family adds: one
     * gauge per non-special type per counter and none for special types, so
     * a job type gaining or losing a limit shows up here.
     *
     * The non-negativity half is the observable consequence of the clamp in
     * `collect()`. The clamp itself cannot be triggered from a test: it
     * guards `waiting` / `running` / `deferred`, which are private to
     * JobQueue and only ever incremented and decremented in matched pairs,
     * so forcing one negative would need the internals hacked. What is
     * assertable is the property the clamp exists to guarantee -- since
     * `Gauge::value_type` is unsigned, an unclamped negative would surface
     * as a value near 2^64 rather than as a small number, which is exactly
     * what the upper bound below rules out.
     */
    void
    testGaugeCoverageAndNonNegative()
    {
        testcase("Saturation gauge coverage");

        GaugeFixture fixture(1);
        fixture.collector->runHooks();

        // Sanity-check the fixture against the job-type table itself, so the
        // expected counts are derived rather than hard-coded.
        int nonSpecial = 0;
        int special = 0;
        int gauges = 0;
        bool allSmall = true;

        // No job has been submitted, so every published value must be 0.
        // The bound is deliberately generous: it is here to catch an
        // unsigned wrap, not to re-assert the exact zero above.
        auto const kWrapGuard = static_cast<std::uint64_t>(1) << 32;

        for (auto const& [type, info] : JobTypes::instance())
        {
            if (type == JtInvalid)
                continue;

            info.special() ? ++special : ++nonSpecial;

            for (char const* suffix : {kSuffixWaiting, kSuffixRunning, kSuffixDeferred})
            {
                auto const value = fixture.collector->gaugeValue(info.name() + suffix);

                // Presence must agree with speciality, in both directions.
                BEAST_EXPECT(value.has_value() == !info.special());
                if (!value)
                    continue;

                ++gauges;
                if (*value >= kWrapGuard)
                    allSmall = false;
                BEAST_EXPECT(*value == 0u);
            }
        }

        BEAST_EXPECT(nonSpecial == 35);
        BEAST_EXPECT(special == 11);
        BEAST_EXPECT(gauges == nonSpecial * 3);
        BEAST_EXPECT(gauges == 105);
        BEAST_EXPECT(allSmall);
    }

public:
    void
    run() override
    {
        testAddJob();
        testPostCoro();
        testGaugeCreation();
        testGaugeCoverageAndNonNegative();
        testDeferredGaugeExactValues();
        testUncappedTypeNeverDefers();
    }
};

BEAST_DEFINE_TESTSUITE(JobQueue, core, xrpl);

}  // namespace xrpl::test
