#include <xrpl/nodestore/DummyScheduler.h>

namespace xrpl {
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
}  // namespace xrpl
