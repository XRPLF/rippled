#include <xrpl/nodestore/DummyScheduler.h>

#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Task.h>

namespace xrpl::NodeStore {

void
DummyScheduler::scheduleTask(Task& task)
{
    // Invoke the task synchronously.
    task.performScheduledTask();
}

void
DummyScheduler::onFetch(FetchReport const& report)
{
}

void
DummyScheduler::onBatchWrite(BatchWriteReport const& report)
{
}

}  // namespace xrpl::NodeStore
