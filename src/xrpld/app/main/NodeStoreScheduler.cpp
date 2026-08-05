#include <xrpld/app/main/NodeStoreScheduler.h>

#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Task.h>

namespace xrpl {

NodeStoreScheduler::NodeStoreScheduler(JobQueue& jobQueue) : jobQueue_(jobQueue)
{
}

void
NodeStoreScheduler::scheduleTask(NodeStore::Task& task)
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
NodeStoreScheduler::onFetch(NodeStore::FetchReport const& report)
{
    if (jobQueue_.isStopped())
        return;

    jobQueue_.addLoadEvents(
        report.fetchType == NodeStore::FetchType::Async ? JtNsAsyncRead : JtNsSyncRead,
        1,
        report.elapsed);
}

void
NodeStoreScheduler::onBatchWrite(NodeStore::BatchWriteReport const& report)
{
    if (jobQueue_.isStopped())
        return;

    jobQueue_.addLoadEvents(JtNsWrite, report.writeCount, report.elapsed);
}

}  // namespace xrpl
