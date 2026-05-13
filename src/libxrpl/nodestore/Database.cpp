#include <xrpl/nodestore/Database.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace xrpl::NodeStore {

/** Construct the node store and start the async read thread pool.
 *
 *  Validates three configuration parameters, then spawns and detaches
 *  `readThreads` worker threads:
 *  - `earliest_seq`: minimum ledger sequence the store will serve;
 *      defaults to `kXRP_LEDGER_EARLIEST_SEQ` (32570). Must be >= 1.
 *  - `rq_bundle`: batch dequeue size per lock acquisition; clamped [1, 64],
 *      defaults to 4. Amortises mutex overhead under load.
 *
 *  Threads are detached rather than joined; their lifetime is controlled by
 *  `readStopping_`. `stop()` spin-waits until `readThreads_` reaches zero,
 *  providing a bounded shutdown guarantee (asserted within 30 s).
 *
 *  @param scheduler Task scheduler for async I/O dispatch and telemetry callbacks.
 *  @param readThreads Number of prefetch worker threads to create. Must be > 0.
 *  @param config Section from the node configuration, read for `earliest_seq`
 *      and `rq_bundle` keys.
 *  @param journal Logging sink.
 *  @throws std::runtime_error if `earliest_seq` < 1 or `rq_bundle` is outside [1, 64].
 */
Database::Database(
    Scheduler& scheduler,
    int readThreads,
    Section const& config,
    beast::Journal journal)
    : j_(journal)
    , scheduler_(scheduler)
    , earliestLedgerSeq_(get<std::uint32_t>(config, "earliest_seq", kXRP_LEDGER_EARLIEST_SEQ))
    , requestBundle_(get<int>(config, "rq_bundle", 4))
    , readThreads_(std::max(1, readThreads))
{
    XRPL_ASSERT(readThreads, "xrpl::NodeStore::Database::Database : nonzero threads input");

    if (earliestLedgerSeq_ < 1)
        Throw<std::runtime_error>("Invalid earliest_seq");

    if (requestBundle_ < 1 || requestBundle_ > 64)
        Throw<std::runtime_error>("Invalid rq_bundle");

    for (int i = readThreads_.load(); i != 0; --i)
    {
        std::thread t(
            [this](int i) {
                runningThreads_++;

                beast::setCurrentThreadName("db prefetch #" + std::to_string(i));

                decltype(read_) read;

                while (true)
                {
                    {
                        std::unique_lock<std::mutex> lock(readLock_);

                        if (isStopping())
                            break;

                        if (read_.empty())
                        {
                            runningThreads_--;
                            readCondVar_.wait(lock);
                            runningThreads_++;
                        }

                        if (isStopping())
                            break;

                        // Extract up to requestBundle_ entries per lock acquisition
                        // to amortise mutex overhead without starving other threads.
                        for (int cnt = 0; !read_.empty() && cnt != requestBundle_; ++cnt)
                            read.insert(read_.extract(read_.begin()));
                    }

                    for (auto it = read.begin(); it != read.end(); ++it)
                    {
                        XRPL_ASSERT(
                            !it->second.empty(),
                            "xrpl::NodeStore::Database::Database : non-empty "
                            "data");

                        auto const& hash = it->first;
                        auto const& data = it->second;
                        auto const seqn = data[0].first;

                        auto obj = fetchNodeObject(hash, seqn, FetchType::Async);

                        // This could be further optimized: if there are
                        // multiple requests for sequence numbers mapping to
                        // multiple databases by sorting requests such that all
                        // indices mapping to the same database are grouped
                        // together and serviced by a single read.
                        for (auto const& req : data)
                        {
                            req.second(
                                (seqn == req.first) || isSameDB(req.first, seqn)
                                    ? obj
                                    : fetchNodeObject(hash, req.first, FetchType::Async));
                        }
                    }

                    read.clear();
                }

                --runningThreads_;
                --readThreads_;
            },
            i);
        t.detach();
    }
}

/** Destroy the node store.
 *
 *  Calls `stop()` as a safety net to drain the read queue and wait for worker
 *  threads to exit. However, derived classes **must** call `stop()` in their
 *  own destructors before this base destructor runs. Worker threads hold a raw
 *  `this` pointer and invoke the virtual `fetchNodeObject()`; if the derived
 *  vtable has already been dismantled when a thread wakes, the call resolves
 *  against a destroyed object, causing undefined behaviour.
 */
Database::~Database()
{
    stop();
}

/** Return whether a stop has been requested.
 *
 *  Uses a relaxed load — only the `readStopping_` flag itself need be
 *  observed; surrounding operations are not required to be sequentially
 *  consistent with this check.
 *
 *  @return `true` once `stop()` has been called.
 */
bool
Database::isStopping() const
{
    return readStopping_.load(std::memory_order_relaxed);
}

/** Signal all worker threads to stop and block until they have fully exited.
 *
 *  On the first call, sets `readStopping_`, clears the pending read queue,
 *  and broadcasts on `readCondVar_` so all blocked threads wake immediately.
 *  Then spin-waits (yielding the CPU) until `readThreads_` drops to zero,
 *  confirming every thread has fully exited.
 *
 *  Idempotent: subsequent calls are no-ops (the `exchange` guards against
 *  double-clear).
 *
 *  @note Safe to call from any thread. The 30-second assertion in the
 *      spin-wait is a hard upper bound; exceeding it indicates a deadlock
 *      in `fetchNodeObject()` or the scheduler.
 */
void
Database::stop()
{
    {
        std::scoped_lock const lock(readLock_);

        if (!readStopping_.exchange(true, std::memory_order_relaxed))
        {
            JLOG(j_.debug()) << "Clearing read queue because of stop request";
            read_.clear();
            readCondVar_.notify_all();
        }
    }

    JLOG(j_.debug()) << "Waiting for stop request to complete...";

    using namespace std::chrono;

    auto const start = steady_clock::now();

    while (readThreads_.load() != 0)
    {
        XRPL_ASSERT(
            steady_clock::now() - start < 30s,
            "xrpl::NodeStore::Database::stop : maximum stop duration");
        std::this_thread::yield();
    }

    JLOG(j_.debug())
        << "Stop request completed in "
        << duration_cast<std::chrono::milliseconds>(steady_clock::now() - start).count()
        << " milliseconds";
}

/** Schedule a non-blocking fetch and invoke a callback on completion.
 *
 *  Appends `(ledgerSeq, cb)` to the entry in `read_` keyed by `hash`,
 *  creating the entry if absent. Multiple callers requesting the same hash
 *  coalesce into a single backend read — all their callbacks fire when the
 *  single I/O completes. Wakes exactly one worker thread via
 *  `readCondVar_.notify_one()`.
 *
 *  Silently discards the request (without invoking `cb`) if `isStopping()`
 *  is already true at the point of the lock acquisition.
 *
 *  @param hash The 256-bit hash key of the desired `NodeObject`.
 *  @param ledgerSeq Ledger sequence associated with this request; used by
 *      `isSameDB()` to decide whether a cached fetch result can be reused
 *      across multi-backend configurations.
 *  @param cb Callback invoked on the worker thread when the fetch completes.
 *      Receives the fetched `NodeObject`, or `nullptr` on cache miss.
 */
void
Database::asyncFetch(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    std::function<void(std::shared_ptr<NodeObject> const&)>&& cb)
{
    std::scoped_lock const lock(readLock_);

    if (!isStopping())
    {
        read_[hash].emplace_back(ledgerSeq, std::move(cb));
        readCondVar_.notify_one();
    }
}

/** Copy all objects from `srcDB` into `dstBackend` in batches.
 *
 *  Iterates `srcDB` via `forEach()` and accumulates `NodeObject`s into a
 *  `Batch`. When the batch reaches `kBATCH_WRITE_PREALLOCATION_SIZE`, it is
 *  flushed to `dstBackend.storeBatch()` and byte statistics are recorded via
 *  `storeStats()`. Any remaining objects after iteration are flushed in a
 *  final partial batch.
 *
 *  Exceptions from `storeBatch()` are caught and logged but do not abort the
 *  import — partial progress is preferred over a complete rollback in what may
 *  be a long-running migration.
 *
 *  @param dstBackend Destination backend to write objects into.
 *  @param srcDB Source database to read objects from; `forEach()` is called
 *      on this object and must not be called concurrently.
 */
void
Database::importInternal(Backend& dstBackend, Database& srcDB)
{
    Batch batch;
    batch.reserve(kBATCH_WRITE_PREALLOCATION_SIZE);
    auto storeBatch = [&, fname = __func__]() {
        try
        {
            dstBackend.storeBatch(batch);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.error()) << "Exception caught in function " << fname << ". Error: " << e.what();
            return;
        }

        std::uint64_t sz{0};
        for (auto const& nodeObject : batch)
            sz += nodeObject->getData().size();
        storeStats(batch.size(), sz);
        batch.clear();
    };

    srcDB.forEach([&](std::shared_ptr<NodeObject> nodeObject) {
        XRPL_ASSERT(nodeObject, "xrpl::NodeStore::Database::importInternal : non-null node");
        if (!nodeObject)  // This should never happen
            return;

        batch.emplace_back(std::move(nodeObject));
        if (batch.size() >= kBATCH_WRITE_PREALLOCATION_SIZE)
            storeBatch();
    });

    if (!batch.empty())
        storeBatch();
}

/** Instrumentation shim around the private virtual `fetchNodeObject`.
 *
 *  Delegates to the subclass implementation, then records wall-clock elapsed
 *  time in `fetchDurationUs_`, increments `fetchHitCount_` and `fetchSz_` on
 *  a hit, increments `fetchTotalCount_` unconditionally, and reports the
 *  completed fetch to the `Scheduler` via `scheduler_.onFetch()`. The
 *  scheduler hook allows the task-prioritisation layer to monitor backend
 *  performance dynamically.
 *
 *  @param hash The 256-bit hash key of the desired `NodeObject`.
 *  @param ledgerSeq Ledger sequence passed through to the backend; used by
 *      rotating-backend implementations to select writable vs. archive.
 *  @param fetchType Whether the fetch originates from a synchronous or
 *      asynchronous code path; passed through to `FetchReport`.
 *  @param duplicate When `true`, the caller already has a copy of this object;
 *      the backend may skip promotion to the writable store.
 *  @return The retrieved `NodeObject`, or `nullptr` on cache miss or error.
 */
std::shared_ptr<NodeObject>
Database::fetchNodeObject(
    uint256 const& hash,
    std::uint32_t ledgerSeq,
    FetchType fetchType,
    bool duplicate)
{
    FetchReport fetchReport(fetchType);

    using namespace std::chrono;
    auto const begin{steady_clock::now()};

    auto nodeObject{fetchNodeObject(hash, ledgerSeq, fetchReport, duplicate)};
    auto dur = steady_clock::now() - begin;
    fetchDurationUs_ += duration_cast<microseconds>(dur).count();
    if (nodeObject)
    {
        ++fetchHitCount_;
        fetchSz_ += nodeObject->getData().size();
    }
    ++fetchTotalCount_;

    fetchReport.elapsed = duration_cast<milliseconds>(dur);
    scheduler_.onFetch(fetchReport);
    return nodeObject;
}

/** Populate a JSON object with live operational metrics for this database.
 *
 *  Writes the following keys into `obj`:
 *  - `read_queue`: current depth of the pending async read map.
 *  - `read_threads_total` / `read_threads_running`: total and actively
 *      processing worker thread counts.
 *  - `read_request_bundle`: configured batch dequeue size (`rq_bundle`).
 *  - `node_writes`, `node_written_bytes`: cumulative store count and bytes.
 *  - `node_reads_total`, `node_reads_hit`, `node_read_bytes`: fetch counts
 *      and bytes, distinguishing hits from total attempts.
 *  - `node_reads_duration_us`: cumulative backend read latency in microseconds.
 *
 *  This data surfaces via the `get_counts` RPC command and is valuable for
 *  diagnosing I/O bottlenecks in production nodes.
 *
 *  @param obj A JSON object to populate; must satisfy `obj.isObject()`.
 */
void
Database::getCountsJson(json::Value& obj)
{
    XRPL_ASSERT(obj.isObject(), "xrpl::NodeStore::Database::getCountsJson : valid input type");

    {
        std::unique_lock<std::mutex> const lock(readLock_);
        obj["read_queue"] = static_cast<json::UInt>(read_.size());
    }

    obj["read_threads_total"] = readThreads_.load();
    obj["read_threads_running"] = runningThreads_.load();
    obj["read_request_bundle"] = requestBundle_;

    obj[jss::node_writes] = std::to_string(storeCount_);
    obj[jss::node_reads_total] = std::to_string(fetchTotalCount_);
    obj[jss::node_reads_hit] = std::to_string(fetchHitCount_);
    obj[jss::node_written_bytes] = std::to_string(storeSz_);
    obj[jss::node_read_bytes] = std::to_string(fetchSz_);
    obj[jss::node_reads_duration_us] = std::to_string(fetchDurationUs_);
}

}  // namespace xrpl::NodeStore
