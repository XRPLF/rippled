/** @file
 *  Synchronous no-op implementation of the NodeStore `Scheduler` interface.
 *
 *  All three virtual methods are trivial: `scheduleTask` runs the task
 *  inline on the calling thread; the two telemetry hooks are empty. See
 *  `DummyScheduler.h` for the full behavioral contract and usage guidance.
 */

#include <xrpl/nodestore/DummyScheduler.h>

#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Task.h>

namespace xrpl::NodeStore {

void
DummyScheduler::scheduleTask(Task& task)
{
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
