#include <xrpl/nodestore/DummyScheduler.h>

namespace ripple {
namespace NodeStore {

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

}  // namespace NodeStore
}  // namespace ripple
