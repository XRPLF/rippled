#pragma once

#include <xrpl/nodestore/Scheduler.h>

namespace xrpl::NodeStore {

/** Null-object implementation of @ref Scheduler for tests and offline import.
 *
 *  Satisfies the full `Scheduler` interface contract while doing the minimum
 *  possible work: every task is executed immediately on the calling thread, and
 *  the two performance-reporting hooks are no-ops. There is no thread pool, no
 *  queue, and no statistics collection.
 *
 *  The `Scheduler` contract explicitly permits running a task on the calling
 *  thread, so `DummyScheduler` is always correct — it differs from a
 *  production scheduler only in latency and throughput characteristics.
 *
 *  **Effect on `BatchWriter`**: Because `scheduleTask` flushes the batch
 *  inline before returning, batching is effectively disabled. This is
 *  acceptable for import and test workloads but would degrade performance
 *  under normal ledger-processing load.
 *
 *  **Typical call sites**:
 *  - `Application.cpp` — transient scheduler for the source database during
 *    node-startup `doImport`; sequential offline migration makes async
 *    scheduling unnecessary.
 *  - `Backend_test.cpp`, `Database_test.cpp`, `Timing_test.cpp`,
 *    `NuDBFactory_test.cpp`, `shamap/common.h` — test fixtures use this to
 *    obtain deterministic, single-threaded execution without the teardown
 *    complexity of a real async scheduler.
 *
 *  @see Scheduler
 *  @see BatchWriter
 */
class DummyScheduler : public Scheduler
{
public:
    DummyScheduler() = default;
    ~DummyScheduler() override = default;

    /** Execute @p task synchronously on the calling thread.
     *
     *  Calls `task.performScheduledTask()` directly and returns only after
     *  the task completes. With `BatchWriter` as the consumer, this causes
     *  the pending write batch to be flushed inline, disabling asynchronous
     *  batching.
     *
     *  @param task The task to execute; must remain valid for the duration
     *      of the call.
     */
    void
    scheduleTask(Task& task) override;

    /** No-op performance hook — fetch telemetry is not collected.
     *
     *  @param report Ignored.
     */
    void
    onFetch(FetchReport const& report) override;

    /** No-op performance hook — batch-write telemetry is not collected.
     *
     *  @param report Ignored.
     */
    void
    onBatchWrite(BatchWriteReport const& report) override;
};

}  // namespace xrpl::NodeStore
