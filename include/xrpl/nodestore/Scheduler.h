#pragma once

#include <xrpl/nodestore/Task.h>

#include <chrono>

namespace xrpl::NodeStore {

/** Distinguishes how a node-object fetch was initiated.
 *
 *  Used by `FetchReport` to let the `Scheduler` route telemetry to the
 *  correct load-tracking bucket (`jtNS_SYNC_READ` vs `jtNS_ASYNC_READ`
 *  in production).
 */
enum class FetchType {
    Synchronous, /**< Fetch was issued on the caller's thread and awaited inline. */
    Async        /**< Fetch was queued and completed on a background read thread. */
};

/** Performance telemetry for a single completed node-object fetch.
 *
 *  Created on the stack immediately before a fetch and passed to
 *  `Scheduler::onFetch()` once the fetch returns. `fetchType` is fixed at
 *  construction; `elapsed` and `wasFound` are filled in afterwards.
 *
 *  @see Scheduler::onFetch
 */
struct FetchReport
{
    /** Construct a report for a fetch of the given type.
     *
     *  @param fetchType Whether the fetch was synchronous or asynchronous;
     *      stored as a `const` member and cannot be changed after construction.
     */
    explicit FetchReport(FetchType fetchType) : fetchType(fetchType)
    {
    }

    std::chrono::milliseconds elapsed{}; /**< Wall-clock duration of the fetch; zero-initialized. */
    FetchType const fetchType;           /**< Sync or async; set at construction. */
    bool wasFound = false;               /**< True if the object was present in the backend. */
};

/** Performance telemetry for a single completed batch write.
 *
 *  Constructed by `BatchWriter` after each flush and passed to
 *  `Scheduler::onBatchWrite()`. Both fields must be filled in by the caller
 *  before the report is forwarded.
 *
 *  @see Scheduler::onBatchWrite
 */
struct BatchWriteReport
{
    explicit BatchWriteReport() = default;

    std::chrono::milliseconds elapsed; /**< Wall-clock duration of the batch flush. */
    int writeCount;                    /**< Number of `NodeObject`s written in this batch. */
};

/** Scheduling and telemetry interface for NodeStore backend activity.
 *
 *  Decouples backend write batching and I/O instrumentation from any
 *  particular threading strategy. A `Scheduler` implementation may run a
 *  submitted task synchronously on the calling thread (as `DummyScheduler`
 *  does) or post it to a thread pool (as `NodeStoreScheduler` does via the
 *  application `JobQueue`). The same backend code is correct under either
 *  policy.
 *
 *  The interface serves two orthogonal purposes that share one injection
 *  point: *work dispatch* (`scheduleTask`) and *telemetry ingestion*
 *  (`onFetch`, `onBatchWrite`). Concrete implementations may ignore the
 *  telemetry hooks entirely or forward them to a load-balancing subsystem.
 *
 *  @note `scheduleTask` takes `task` by non-const reference rather than by
 *      value or smart pointer. `BatchWriter` implements `Task` privately and
 *      manages its own lifetime, so no heap allocation is required for the
 *      common write-batching case. Callers must ensure the task object
 *      remains valid until `performScheduledTask()` returns.
 *
 *  @see BatchWriter
 *  @see DummyScheduler
 */
class Scheduler
{
public:
    virtual ~Scheduler() = default;

    /** Dispatch a task for execution.
     *
     *  The scheduler may call `task.performScheduledTask()` on the current
     *  thread before returning, or post the task to an unspecified foreign
     *  thread. Both behaviours are valid; callers must not assume which will
     *  occur. The task object must remain valid until `performScheduledTask()`
     *  returns.
     *
     *  @param task The deferred work to execute; typically a `BatchWriter`
     *      flush. Passed by reference — ownership is not transferred.
     */
    virtual void
    scheduleTask(Task& task) = 0;

    /** Telemetry hook called after each node-object fetch completes.
     *
     *  Allows the scheduler to record I/O latency and hit/miss statistics.
     *  This is a pure reporting path with no effect on control flow; backends
     *  call it unconditionally after every fetch, whether or not the object
     *  was found.
     *
     *  @param report Timing, fetch type, and hit/miss outcome for the
     *      completed fetch.
     */
    virtual void
    onFetch(FetchReport const& report) = 0;

    /** Telemetry hook called after each batch write completes.
     *
     *  Allows the scheduler to record write throughput. Called by
     *  `BatchWriter` after each flush, with `report.writeCount` reflecting
     *  the number of objects flushed in that batch.
     *
     *  @param report Elapsed time and object count for the completed batch.
     */
    virtual void
    onBatchWrite(BatchWriteReport const& report) = 0;
};

}  // namespace xrpl::NodeStore
