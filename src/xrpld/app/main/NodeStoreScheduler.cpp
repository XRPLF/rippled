// cspell:ignore ISTOGRAM
// The all-caps macro name XRPL_METRIC_HISTOGRAM_RECORD_LABELED trips cspell's
// compound-word splitter, which emits the subword "ISTOGRAM"; ignore it here.

#include <xrpld/app/main/NodeStoreScheduler.h>

#include <xrpld/telemetry/MetricMacros.h>

#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Task.h>
#include <xrpl/telemetry/NodeStoreMetricNames.h>

#include <chrono>
#include <string>

namespace xrpl {

NodeStoreScheduler::NodeStoreScheduler([[maybe_unused]] ServiceRegistry& app, JobQueue& jobQueue)
#ifdef XRPL_ENABLE_TELEMETRY
    : app_(app), jobQueue_(jobQueue)
#else
    : jobQueue_(jobQueue)
#endif
{
}

void
NodeStoreScheduler::scheduleTask(node_store::Task& task)
{
    if (jobQueue_.isStopped())
        return;

    if (!jobQueue_.addJob(JtWrite, "NObjStore", [&task]() { task.performScheduledTask(); }))
    {
        // Job not added, presumably because we're shutting down.
        // Recover by executing the task synchronously.
        task.performScheduledTask();
    }
}

void
NodeStoreScheduler::onFetch(node_store::FetchReport const& report)
{
    if (jobQueue_.isStopped())
        return;

    auto const isAsync = report.fetchType == node_store::FetchType::Async;

    // The report is in microseconds but addLoadEvents takes milliseconds, so
    // cast explicitly. The load monitor only tracks whole-millisecond load,
    // so the sub-millisecond detail is deliberately dropped here; the
    // histogram below keeps it.
    jobQueue_.addLoadEvents(
        isAsync ? JtNsAsyncRead : JtNsSyncRead,
        1,
        std::chrono::duration_cast<std::chrono::milliseconds>(report.elapsed));

    // Skip a negative elapsed time rather than hand it to the SDK, which
    // rejects it and logs a warning on every single call. The clock is
    // monotonic, so this needs a clock bug to happen -- but a per-fetch log
    // flood would be worse than the missing sample.
    if (!telemetry::shouldRecordFetchLatency(report.elapsed.count()))
        return;

    // Two labels, both already on the report. fetch_type because a slow async
    // read only delays prefetch while a slow sync read blocks a caller;
    // found because a miss can cost a read of every backend, so mixing the
    // two blurs the distribution.
    XRPL_METRIC_HISTOGRAM_RECORD_LABELED(
        app_,
        telemetry::kNodeStoreReadUs,
        telemetry::kNodeStoreReadUsDesc,
        report.elapsed.count(),
        {{telemetry::kFetchTypeLabel, std::string(telemetry::fetchTypeLabelValue(isAsync))},
         {telemetry::kFetchFoundLabel,
          std::string(telemetry::fetchFoundLabelValue(report.wasFound))}});
}

void
NodeStoreScheduler::onBatchWrite(node_store::BatchWriteReport const& report)
{
    if (jobQueue_.isStopped())
        return;

    jobQueue_.addLoadEvents(JtNsWrite, report.writeCount, report.elapsed);
}

}  // namespace xrpl
