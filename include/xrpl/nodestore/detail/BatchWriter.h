#pragma once

#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Task.h>
#include <xrpl/nodestore/Types.h>

#include <condition_variable>
#include <mutex>

namespace xrpl::NodeStore {

/** Coalesces individual NodeObject writes into batches for NodeStore backends.
 *
 *  Individual key-value store writes carry per-operation overhead (system
 *  call, WAL append, compaction pressure). `BatchWriter` amortises that cost
 *  by accumulating objects in an internal buffer and flushing them as a single
 *  batch via a `Scheduler`-dispatched task.  Use of this class is optional —
 *  a backend may implement its own batching strategy or skip batching entirely.
 *
 *  The class privately inherits `Task`, turning itself into a schedulable unit
 *  of work with no additional heap allocation. The actual write is delegated
 *  to a `Callback` (typically the owning backend), keeping storage-engine
 *  specifics out of the batching logic.
 *
 *  **Thread safety**: `store()` and `getWriteLoad()` are safe to call
 *  concurrently from multiple threads.  The flush task may run on the calling
 *  thread (synchronous scheduler) or a background thread (async scheduler);
 *  the recursive mutex design is safe under both policies.
 *
 *  **Backpressure**: `store()` blocks when the pending buffer reaches
 *  `kBATCH_WRITE_LIMIT_SIZE` (65,536 objects), preventing unbounded memory
 *  growth when disk I/O cannot keep pace with producers.  Peak in-flight
 *  memory can reach approximately twice this limit due to the double-buffer
 *  swap pattern (one batch being written while the next accumulates).
 *
 *  @see Scheduler
 *  @see Backend
 */
class BatchWriter : private Task
{
public:
    /** Pure interface through which `BatchWriter` delivers a completed batch.
     *
     *  The concrete backend (e.g., `RocksDBBackend`) inherits both `Backend`
     *  and `BatchWriter::Callback`, implementing `writeBatch` to forward the
     *  batch to the underlying storage engine.  This indirection keeps batching
     *  logic storage-agnostic.
     *
     *  `writeBatch` is invoked outside the internal mutex, so implementations
     *  may perform blocking I/O without serialising concurrent `store()` calls.
     */
    struct Callback
    {
        virtual ~Callback() = default;
        Callback() = default;
        Callback(Callback const&) = delete;
        Callback&
        operator=(Callback const&) = delete;

        /** Flush a completed batch to the storage engine.
         *
         *  Called by `BatchWriter` once per scheduled flush, with the lock
         *  already released.  The implementation must persist every object in
         *  `batch` before returning.
         *
         *  @param batch The collection of `NodeObject`s to write.  Objects in
         *      the batch may be concurrently referenced by in-memory caches.
         */
        virtual void
        writeBatch(Batch const& batch) = 0;
    };

    /** Construct a `BatchWriter` tied to the given sink and scheduler.
     *
     *  Pre-allocates the internal write buffer to avoid repeated small
     *  reallocations during normal operation.
     *
     *  @param callback  The sink that receives each flushed `Batch` via
     *      `Callback::writeBatch()`.  Typically the owning backend.  Must
     *      outlive this `BatchWriter`.
     *  @param scheduler The scheduler used to dispatch the flush task.  May be
     *      a synchronous `DummyScheduler` (tests and bulk import) or the
     *      production async scheduler; both are supported.
     */
    BatchWriter(Callback& callback, Scheduler& scheduler);

    /** Destroy the `BatchWriter`, draining any pending writes first.
     *
     *  Blocks until all accumulated objects have been flushed to the
     *  `Callback`.  No objects passed to `store()` are silently abandoned.
     */
    ~BatchWriter() override;

    /** Enqueue a `NodeObject` for the next scheduled batch flush.
     *
     *  Appends `object` to the internal accumulation buffer and, if no flush
     *  task is already outstanding, schedules one via the `Scheduler`.
     *  Subsequent `store()` calls before the flush fires piggyback on the
     *  single in-flight task.
     *
     *  @param object The `NodeObject` to persist.
     *  @note Blocks the caller when the buffer reaches `kBATCH_WRITE_LIMIT_SIZE`
     *      (65,536 objects) until the in-flight batch is fully written.  This
     *      backpressure prevents unbounded memory growth when disk I/O falls
     *      behind producers.
     */
    void
    store(std::shared_ptr<NodeObject> const& object);

    /** Return a conservative estimate of pending write I/O.
     *
     *  Returns the larger of the item count currently being written to the
     *  backend and the item count waiting for the next scheduled flush.
     *  Taking the maximum reflects pressure in both the in-flight and
     *  accumulating phases, giving callers a meaningful load signal for
     *  scheduling decisions.
     *
     *  @return Estimated number of `NodeObject`s awaiting or undergoing write.
     */
    int
    getWriteLoad();

private:
    /** `Task` entry-point; delegates to the internal `writeBatch()`. */
    void
    performScheduledTask() override;

    /** Drain accumulated objects to the backend using the double-buffer swap.
     *
     *  Holds the lock only long enough to swap the internal buffer with a
     *  local vector (O(1)), then releases the lock before calling
     *  `Callback::writeBatch()`.  Loops until no objects remain after a swap,
     *  then clears `writePending_` and notifies any blocked `store()` callers.
     */
    void
    writeBatch();

    /** Block until any in-flight flush has completed.
     *
     *  Waits on the condition variable until `writePending_` is false.
     *  Called by the destructor to guarantee no pending objects are abandoned
     *  on teardown.
     */
    void
    waitForWriting();

private:
    /** Recursive to allow synchronous schedulers that invoke `writeBatch()`
     *  on the same thread as `store()` or `waitForWriting()`. */
    using LockType = std::recursive_mutex;

    /** Required by `LockType`; `std::condition_variable` only works with
     *  `std::mutex`. */
    using CondvarType = std::condition_variable_any;

    Callback& callback_;
    Scheduler& scheduler_;
    LockType writeMutex_;
    CondvarType writeCondition_;
    /** Item count of the batch currently being written; used by `getWriteLoad()`. */
    int writeLoad_{0};
    /** True when a flush task has been scheduled but not yet completed. */
    bool writePending_{false};
    /** Accumulation buffer; swapped out atomically inside `writeBatch()`. */
    Batch writeSet_;
};

}  // namespace xrpl::NodeStore
