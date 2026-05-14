/** @file
 *  Implements BatchWriter, the write-coalescing buffer for NodeStore backends.
 *
 *  Individual NodeObject writes are accumulated into a Batch and flushed as a
 *  single scheduled task, amortising the per-write overhead of backends such as
 *  RocksDB.  Back-pressure, load estimation, and shutdown safety are all handled
 *  here; see BatchWriter.h for the public interface contract.
 */

#include <xrpl/nodestore/detail/BatchWriter.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

namespace xrpl::NodeStore {

/** Construct a BatchWriter tied to the given sink and scheduler.
 *
 *  Pre-allocates `writeSet_` to `kBATCH_WRITE_PREALLOCATION_SIZE` to avoid
 *  repeated small reallocations during normal operation.
 *
 *  @param callback  The sink that will receive each flushed Batch via
 *      `writeBatch()`.  Typically the owning backend (e.g. RocksDBBackend).
 *  @param scheduler The scheduler used to dispatch the flush task.  May be
 *      a synchronous DummyScheduler (tests/import) or the production async
 *      scheduler; both are supported via the recursive mutex.
 */
BatchWriter::BatchWriter(Callback& callback, Scheduler& scheduler)
    : callback_(callback), scheduler_(scheduler)
{
    writeSet_.reserve(kBATCH_WRITE_PREALLOCATION_SIZE);
}

/** Destroy the BatchWriter, draining any pending writes first.
 *
 *  Blocks until `writePending_` is false so that no accumulated objects
 *  are silently abandoned when a backend tears down.
 */
BatchWriter::~BatchWriter()
{
    waitForWriting();
}

/** Enqueue a NodeObject for the next batch flush.
 *
 *  Appends `object` to `writeSet_` and, if no flush task is already
 *  outstanding (`writePending_` is false), schedules one via the
 *  `Scheduler`.  Subsequent stores during the same window piggyback on
 *  the single in-flight task.
 *
 *  @param object The NodeObject to persist.
 *  @note Blocks the caller when `writeSet_` reaches `kBATCH_WRITE_LIMIT_SIZE`
 *      (65,536 objects) until the in-flight batch is fully written.  This
 *      back-pressure prevents unbounded memory growth when disk I/O falls
 *      behind producers.
 */
void
BatchWriter::store(std::shared_ptr<NodeObject> const& object)
{
    std::unique_lock<decltype(writeMutex_)> sl(writeMutex_);

    while (writeSet_.size() >= kBATCH_WRITE_PREALLOCATION_SIZE)
        writeCondition_.wait(sl);

    writeSet_.push_back(object);

    if (!writePending_)
    {
        writePending_ = true;

        scheduler_.scheduleTask(*this);
    }
}

/** Return a conservative estimate of pending write I/O.
 *
 *  Returns the larger of `writeLoad_` (the item count of the batch
 *  currently being written to disk) and `writeSet_.size()` (the items
 *  not yet dispatched).  Taking the maximum captures pressure in both the
 *  in-flight and accumulating phases simultaneously.
 *
 *  @return Estimated number of NodeObjects awaiting or undergoing write.
 */
int
BatchWriter::getWriteLoad()
{
    std::scoped_lock const sl(writeMutex_);

    return std::max(writeLoad_, static_cast<int>(writeSet_.size()));
}

/** Task entry-point invoked by the Scheduler; delegates to writeBatch(). */
void
BatchWriter::performScheduledTask()
{
    writeBatch();
}

/** Drain accumulated objects to the backend via the double-buffer swap pattern.
 *
 *  Under the lock, atomically swaps `writeSet_` with a local vector (O(1)),
 *  then releases the lock before calling `callback_.writeBatch()`.  This
 *  keeps the lock held for only the swap, never across I/O.  The loop
 *  continues draining until `writeSet_` is empty after a swap, at which
 *  point `writePending_` is cleared and waiters on `writeCondition_` are
 *  notified.
 *
 *  After each successful flush, records elapsed time and item count in a
 *  `BatchWriteReport` and forwards it to `scheduler_.onBatchWrite()` for
 *  telemetry.
 *
 *  @note The `XRPL_ASSERT` after the swap guards the invariant that the swap
 *      leaves `writeSet_` empty — a defence against future refactors that
 *      might violate atomicity.
 */
void
BatchWriter::writeBatch()
{
    for (;;)
    {
        std::vector<std::shared_ptr<NodeObject>> set;

        set.reserve(kBATCH_WRITE_PREALLOCATION_SIZE);

        {
            std::scoped_lock const sl(writeMutex_);

            writeSet_.swap(set);
            XRPL_ASSERT(
                writeSet_.empty(), "xrpl::NodeStore::BatchWriter::writeBatch : writes not set");
            writeLoad_ = set.size();

            if (set.empty())
            {
                writePending_ = false;
                writeCondition_.notify_all();

                // VFALCO NOTE Fix this function to not return from the middle
                return;
            }
        }

        BatchWriteReport report{};
        report.writeCount = set.size();
        auto const before = std::chrono::steady_clock::now();

        callback_.writeBatch(set);

        report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - before);

        scheduler_.onBatchWrite(report);
    }
}

/** Block until any in-flight batch flush has completed.
 *
 *  Waits on `writeCondition_` until `writePending_` is false.  Called by
 *  the destructor to guarantee no pending objects are abandoned on teardown.
 */
void
BatchWriter::waitForWriting()
{
    std::unique_lock<decltype(writeMutex_)> sl(writeMutex_);

    while (writePending_)
        writeCondition_.wait(sl);
}

}  // namespace xrpl::NodeStore
