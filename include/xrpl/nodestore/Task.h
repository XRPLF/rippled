/** @file
 *  Defines the `Task` abstract interface for NodeStore scheduled work units.
 *
 *  Any piece of deferred backend work (e.g., a `BatchWriter` flush) inherits
 *  from `Task` and implements `performScheduledTask()`. The `Scheduler`
 *  interface accepts a `Task&` and decides *where* and *when* to invoke it,
 *  decoupling the work unit from any knowledge of threads or job queues.
 */

#pragma once

namespace xrpl::NodeStore {

/** Pure command-pattern base for NodeStore deferred backend work.
 *
 *  A `Task` is the minimal callable token the scheduling system needs: a single
 *  `performScheduledTask()` entry point and a virtual destructor. Concrete work
 *  units inherit from this struct (typically privately, as `BatchWriter` does)
 *  and are submitted to `Scheduler::scheduleTask()`.
 *
 *  The scheduling contract is intentionally loose: `Scheduler::scheduleTask()`
 *  may invoke the task synchronously on the calling thread (as `DummyScheduler`
 *  does for tests) or post it to an unspecified foreign thread (as
 *  `NodeStoreScheduler` does via the application `JobQueue`). Concrete `Task`
 *  implementations must be safe under either policy.
 *
 *  The interface is deliberately as small as possible. A richer alternative
 *  such as `std::function` or `std::unique_ptr<Task>` would impose a heap
 *  allocation on every scheduled operation and couple the interface to a
 *  specific ownership model. With this design, `BatchWriter` can implement
 *  `Task` privately and pass `*this` to `scheduleTask()` — no extra allocation
 *  needed, and lifetime management stays entirely within `BatchWriter`.
 *
 *  @see Scheduler
 *  @see BatchWriter
 *  @see DummyScheduler
 */
struct Task
{
    virtual ~Task() = default;

    /** Execute the deferred work represented by this task.
     *
     *  Called by the `Scheduler` either synchronously on the submitting thread
     *  or asynchronously on a foreign thread, depending on the scheduler
     *  implementation. Implementations must tolerate either calling context.
     *
     *  The object must remain valid and unmodified from the time it is passed
     *  to `Scheduler::scheduleTask()` until this method returns.
     */
    virtual void
    performScheduledTask() = 0;
};

}  // namespace xrpl::NodeStore
