#include <xrpld/app/main/NodeStoreScheduler.h>

namespace ripple {

NodeStoreScheduler::NodeStoreScheduler(JobQueue& jobQueue) : jobQueue_(jobQueue)
{
}

void
NodeStoreScheduler::scheduleTask(NodeStore::Task& task)
{
    if (jobQueue_.isStopped())
        return;

    if (!jobQueue_.addJob(jtWRITE, "NodeObject::store", [&task]() {
            task.performScheduledTask();
        }))
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
        report.fetchType == NodeStore::FetchType::async ? jtNS_ASYNC_READ
                                                        : jtNS_SYNC_READ,
        1,
        report.elapsed);
}

void
NodeStoreScheduler::onBatchWrite(NodeStore::BatchWriteReport const& report)
{
    if (jobQueue_.isStopped())
        return;

    jobQueue_.addLoadEvents(jtNS_WRITE, report.writeCount, report.elapsed);
}

}  // namespace ripple
