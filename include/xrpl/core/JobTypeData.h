#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobTypeInfo.h>
#include <xrpl/core/LoadMonitor.h>

#include <utility>

namespace xrpl {

struct JobTypeData
{
private:
    LoadMonitor load_;

    /* Support for insight */
    beast::insight::Collector::ptr collector_;

public:
    /**
     * Metric-name suffixes appended to `JobTypeInfo::name()`.
     *
     * The constructor builds the instrument names from these. Public so
     * tests can assert on the exported names without repeating the
     * literals. The collector is the `"jobq"` group, so
     * `GroupImp::makeName()` prefixes `jobq.` and `OTelCollector`
     * lowercases and turns `.` into `_`: `ledgerRequest` +
     * `kSuffixDeferred` exports as `jobq_ledgerrequest_deferred`.
     */
    /** @{ */
    static constexpr char kSuffixWaiting[] = "_waiting";
    static constexpr char kSuffixRunning[] = "_running";
    static constexpr char kSuffixDeferred[] = "_deferred";
    static constexpr char kSuffixQueued[] = "_q";
    /** @} */

    /* The job category which we represent */
    JobTypeInfo const& info;

    /* The number of jobs waiting */
    int waiting{0};

    /* The number presently running */
    int running{0};

    /* And the number we deferred executing because of job limits */
    int deferred{0};

    /* Notification callbacks */
    beast::insight::Event dequeue;
    beast::insight::Event execute;

    /**
     * Saturation gauges, published by `JobQueue::collect()`.
     *
     * Each mirrors the same-named counter above so per-job-type queue
     * pressure is visible in metrics. Without them the only exported queue
     * signal is the process-wide `jobq_job_count`, which cannot attribute
     * pressure to a job type.
     *
     * `waitingGauge` is the backlog not yet started, `runningGauge` is the
     * in-flight count, and `deferredGauge` is the count held back by this
     * type's concurrency limit. `deferredGauge` is the leading indicator:
     * `JobQueue::addJob()` never rejects, so a capped type under pressure
     * shows up as latency only after the fact, whereas a non-zero deferred
     * reading precedes it.
     *
     * Created only for non-special types (see the constructor). A default
     * constructed `beast::insight::Gauge` holds a null impl and every
     * mutator is a no-op, so a special type's gauge is safe to assign to
     * but publishes nothing.
     */
    /** @{ */
    beast::insight::Gauge waitingGauge;
    beast::insight::Gauge runningGauge;
    beast::insight::Gauge deferredGauge;
    /** @} */

    JobTypeData(
        JobTypeInfo const& info,
        beast::insight::Collector::ptr collector,
        Logs& logs) noexcept
        : load_(logs.journal("LoadMonitor")), collector_(std::move(collector)), info(info)

    {
        load_.setTargetLatency(info.getAverageLatency(), info.getPeakLatency());

        // Special types have limit_ == 0 and bypass the limit logic
        // entirely, so their `deferred` is always 0. Excluding them here
        // matches the existing dequeue/execute rule.
        if (!info.special())
        {
            dequeue = collector_->makeEvent(info.name() + kSuffixQueued);
            execute = collector_->makeEvent(info.name());

            waitingGauge = collector_->makeGauge(info.name() + kSuffixWaiting);
            runningGauge = collector_->makeGauge(info.name() + kSuffixRunning);
            deferredGauge = collector_->makeGauge(info.name() + kSuffixDeferred);
        }
    }

    /* Not copy-constructible or assignable */
    JobTypeData(JobTypeData const& other) = delete;
    JobTypeData&
    operator=(JobTypeData const& other) = delete;

    [[nodiscard]] std::string
    name() const
    {
        return info.name();
    }

    [[nodiscard]] JobType
    type() const
    {
        return info.type();
    }

    LoadMonitor&
    load()
    {
        return load_;
    }

    LoadMonitor::Stats
    stats()
    {
        return load_.getStats();
    }
};

}  // namespace xrpl
