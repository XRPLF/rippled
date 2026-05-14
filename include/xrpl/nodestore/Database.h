/** @file
 *  Abstract base class for the NodeStore persistence layer.
 *
 *  Defines the full public contract for node object storage: async and
 *  synchronous fetch, store, import, and diagnostics. Concrete subclasses
 *  (`DatabaseNodeImp`, `DatabaseRotatingImp`) implement the private virtual
 *  `fetchNodeObject()` and `forEach()` hooks; all instrumentation (timing,
 *  counters, scheduler callbacks) is applied in this base class and cannot
 *  be bypassed.
 */

#pragma once

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/TaggedCache.ipp>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/protocol/SystemParameters.h>

#include <condition_variable>

namespace xrpl::NodeStore {

/** Persistence layer for NodeObject records.
 *
 *  Every ledger datum — account states, transactions, ledger headers — is
 *  stored as a `NodeObject` keyed by the 256-bit hash of its payload. Because
 *  the total object set typically exceeds available memory, any hash absent
 *  from the in-memory cache must be fetched from disk through this class.
 *
 *  `Database` owns the async read thread pool and all performance counters.
 *  The public non-virtual `fetchNodeObject()` wraps the private pure-virtual
 *  one, applying timing, hit/miss accounting, and `Scheduler::onFetch()`
 *  callbacks — so no subclass can escape the instrumentation.
 *
 *  **Shutdown ordering**: Derived classes **must** call `stop()` in their own
 *  destructors before the base destructor runs. Worker threads invoke the
 *  virtual `fetchNodeObject()` through a subclass vtable; if the derived
 *  object is destroyed before all threads have exited, a waking thread will
 *  call through a dangling vtable entry (undefined behaviour). The base
 *  destructor calls `stop()` only as a last-resort safety net.
 *
 *  @see NodeObject, Backend, Scheduler, DatabaseNodeImp, DatabaseRotatingImp
 */
class Database
{
public:
    Database() = delete;

    /** Construct the node store and start the async read thread pool.
     *
     *  Validates configuration parameters, then spawns `readThreads` detached
     *  worker threads. Threads are controlled by `readStopping_`; `stop()`
     *  spin-waits (≤ 30 s) until `readThreads_` reaches zero.
     *
     *  @param scheduler Task scheduler for async I/O dispatch and telemetry
     *      callbacks; must outlive this object.
     *  @param readThreads Number of prefetch worker threads to create; clamped
     *      to at least 1.
     *  @param config `[node_db]` config section; reads `earliest_seq` (default
     *      `kXRP_LEDGER_EARLIEST_SEQ`, must be ≥ 1) and `rq_bundle` (default 4,
     *      clamped [1, 64]).
     *  @param j Logging sink.
     *  @throws std::runtime_error if `earliest_seq` < 1 or `rq_bundle` is
     *      outside [1, 64].
     */
    Database(Scheduler& scheduler, int readThreads, Section const& config, beast::Journal j);

    /** Destroy the node store.
     *
     *  Calls `stop()` as a safety net to drain the read queue and wait for all
     *  worker threads to exit. Derived classes **must** call `stop()` in their
     *  own destructors first — worker threads invoke the pure-virtual
     *  `fetchNodeObject()` through the subclass vtable, which is already gone
     *  by the time this base destructor runs.
     */
    virtual ~Database();

    /** Return the name of the underlying backend for diagnostics.
     *
     *  The returned string may not reflect the actual on-disk path when
     *  multiple backends are in use (e.g. `DatabaseRotatingImp`).
     *
     *  @return A human-readable backend identifier.
     */
    virtual std::string
    getName() const = 0;

    /** Bulk-import all objects from another database into this one.
     *
     *  Iterates every `NodeObject` in @p source and writes it to this
     *  database's backend. Implementations typically delegate to
     *  `importInternal()`. Large databases may take significant time.
     *
     *  @param source The source database to read from; must remain valid
     *      and quiescent (no concurrent writes) for the duration of the call.
     */
    virtual void
    importDatabase(Database& source) = 0;

    /** Return the estimated number of pending write operations.
     *
     *  Used for backpressure diagnostics; the value is approximate and may
     *  change immediately after it is read.
     *
     *  @return Pending write count, or 0 if the backend does not batch writes.
     */
    virtual std::int32_t
    getWriteLoad() const = 0;

    /** Persist a node object to the backend.
     *
     *  Takes ownership of @p data (the caller's `Blob` is consumed). The object
     *  is keyed by @p hash; backends are content-addressed, so storing an object
     *  whose hash already exists is a no-op (same key → same data).
     *
     *  @param type   The semantic type of the object (ledger, account node, etc.).
     *  @param data   Serialized payload; moved into the backend — caller's variable
     *      is left in a valid but unspecified state.
     *  @param hash   256-bit hash of @p data. The caller is responsible for
     *      correctness; the hash is not re-verified by the store.
     *  @param ledgerSeq The ledger sequence this object belongs to; used by
     *      rotating backends to route writes to the correct physical file.
     */
    virtual void
    store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t ledgerSeq) = 0;

    /** Return whether two ledger sequence numbers resolve to the same backend.
     *
     *  When this returns `true`, a fetch with either sequence number will
     *  reach the same physical storage and yield identical results. The async
     *  thread pool uses this to avoid redundant backend reads when multiple
     *  callbacks for the same hash were registered with different sequence
     *  numbers.
     *
     *  `DatabaseNodeImp` always returns `true` (single backend).
     *  `DatabaseRotatingImp` returns `false` when the sequences straddle a
     *  rotation boundary.
     *
     *  @param s1 First ledger sequence number.
     *  @param s2 Second ledger sequence number.
     *  @return `true` if both sequences map to the same physical backend.
     */
    virtual bool
    isSameDB(std::uint32_t s1, std::uint32_t s2) = 0;

    /** Flush any buffered writes to durable storage.
     *
     *  Called by maintenance paths (e.g. ledger close) to ensure consistency.
     *  Not latency-sensitive; implementations may hold locks for the full call.
     */
    virtual void
    sync() = 0;

    /** Fetch a node object by hash, recording timing and hit/miss metrics.
     *
     *  This is the public entry point for all node lookups. It wraps the
     *  private pure-virtual `fetchNodeObject(hash, seq, FetchReport&, duplicate)`
     *  using the Template Method pattern: timing, atomic counters, and
     *  `Scheduler::onFetch()` are applied here and cannot be bypassed by
     *  subclasses.
     *
     *  Returns `nullptr` if the object is absent, could not be decoded, or the
     *  backend encountered an error.
     *
     *  @note Thread-safe; may be called concurrently from any thread.
     *  @param hash       256-bit content hash of the desired object.
     *  @param ledgerSeq  Ledger sequence that owns this object; used by rotating
     *      backends to select the correct physical file. Defaults to 0.
     *  @param fetchType  `FetchType::Synchronous` (default) or
     *      `FetchType::Async` when called from the async worker pool.
     *  @param duplicate  When `true`, the object is also written into the
     *      writable backend after being found in the archive backend
     *      (`DatabaseRotatingImp` promotion path). Defaults to `false`.
     *  @return The requested `NodeObject`, or `nullptr` on miss or error.
     */
    std::shared_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t ledgerSeq = 0,
        FetchType fetchType = FetchType::Synchronous,
        bool duplicate = false);

    /** Schedule a non-blocking background fetch for a node object.
     *
     *  Enqueues a `(hash, ledgerSeq, callback)` entry in the async read map.
     *  Multiple calls for the same hash are coalesced: a single backend read
     *  satisfies all registered callbacks. If `isStopping()` is `true` at the
     *  time of the call, the request is silently discarded and the callback
     *  will never fire.
     *
     *  @note Thread-safe; may be called concurrently from any thread.
     *  @param hash      256-bit content hash of the desired object.
     *  @param ledgerSeq Ledger sequence that owns this object; passed through
     *      to `isSameDB()` for multi-sequence coalescing.
     *  @param callback  Invoked on a worker thread with the fetched
     *      `NodeObject`, or `nullptr` on miss or error.
     */
    virtual void
    asyncFetch(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        std::function<void(std::shared_ptr<NodeObject> const&)>&& callback);

    // --- Performance counters (all lock-free atomic reads) ---

    /** Return the total number of objects written since construction. */
    std::uint64_t
    getStoreCount() const
    {
        return storeCount_;
    }

    /** Return the total number of fetch attempts (hits + misses). */
    std::uint32_t
    getFetchTotalCount() const
    {
        return fetchTotalCount_;
    }

    /** Return the number of fetch attempts that found the requested object. */
    std::uint32_t
    getFetchHitCount() const
    {
        return fetchHitCount_;
    }

    /** Return the cumulative byte count of all stored objects. */
    std::uint64_t
    getStoreSize() const
    {
        return storeSz_;
    }

    /** Return the cumulative byte count of all successfully fetched objects. */
    std::uint32_t
    getFetchSize() const
    {
        return fetchSz_;
    }

    /** Populate a JSON object with read/write diagnostics for `get_counts` RPC.
     *
     *  Snapshots the async read queue depth (under `readLock_`) and then reads
     *  thread counts, request bundle size, and all atomic counters without
     *  holding any lock. The resulting fields include: `read_queue`,
     *  `read_threads_total`, `read_threads_running`, `read_request_bundle`,
     *  `node_writes`, `node_reads_total`, `node_reads_hit`,
     *  `node_written_bytes`, `node_read_bytes`, `node_reads_duration_us`.
     *
     *  @param obj A JSON object to populate; must satisfy `obj.isObject()`.
     */
    void
    getCountsJson(json::Value& obj);

    /** Return the number of file descriptors this database expects to hold open.
     *
     *  Aggregated from the underlying backend(s). Used by the application to
     *  check that the process file-descriptor limit is sufficient before
     *  opening backends. Inaccurate values cause silent failures when the
     *  limit is exceeded.
     *
     *  @return File descriptor count, or 0 if not set by the subclass.
     */
    int
    fdRequired() const
    {
        return fdRequired_;
    }

    /** Begin orderly shutdown of the async read thread pool.
     *
     *  Sets `readStopping_`, clears the pending `read_` queue, broadcasts on
     *  `readCondVar_`, then spin-yields until `readThreads_` reaches zero.
     *  An assertion fires if shutdown takes longer than 30 seconds.
     *
     *  Idempotent: a second call after shutdown has already completed is a
     *  no-op. Derived classes must call this in their own destructors before
     *  their data members are torn down.
     */
    virtual void
    stop();

    /** Return whether `stop()` has been called.
     *
     *  Uses a relaxed atomic load — only the flag value is observed; no
     *  ordering is imposed on surrounding operations.
     *
     *  @return `true` once `stop()` has been invoked.
     */
    bool
    isStopping() const;

    /** Return the earliest ledger sequence this database will serve.
     *
     *  Configured via `earliest_seq` in `[node_db]`; defaults to
     *  `kXRP_LEDGER_EARLIEST_SEQ` (32570 on the main network). The value is
     *  constant after construction. Only unit tests or alternate networks
     *  should set this below the default.
     *
     *  @return The minimum valid ledger sequence number, always ≥ 1.
     */
    [[nodiscard]] std::uint32_t
    earliestLedgerSeq() const noexcept
    {
        return earliestLedgerSeq_;
    }

protected:
    beast::Journal const j_;   ///< Logging sink; set at construction.
    Scheduler& scheduler_;     ///< Task scheduler for async dispatch and telemetry.

    /** Number of file descriptors consumed by the underlying backend(s).
     *  Subclasses set this in their constructors; read by `fdRequired()`.
     */
    int fdRequired_{0};

    std::atomic<std::uint32_t> fetchHitCount_{0};  ///< Fetches that returned a non-null object.
    std::atomic<std::uint32_t> fetchSz_{0};        ///< Cumulative bytes returned by successful fetches.

    /** Minimum ledger sequence this store will serve; constant after construction.
     *  Defaults to `kXRP_LEDGER_EARLIEST_SEQ` (32570). Must be ≥ 1.
     */
    std::uint32_t const earliestLedgerSeq_;

    /** Maximum number of read-queue entries extracted per mutex acquisition.
     *  Amortises lock overhead under load. Configured via `rq_bundle` in
     *  `[node_db]`; clamped to [1, 64]; defaults to 4.
     */
    int const requestBundle_;

    /** Update store counters after a successful batch write.
     *
     *  @param count Number of objects written.
     *  @param sz    Total byte size of those objects.
     *  @note Asserts `count <= sz` — byte total must be ≥ item count.
     */
    void
    storeStats(std::uint64_t count, std::uint64_t sz)
    {
        XRPL_ASSERT(count <= sz, "xrpl::NodeStore::Database::storeStats : valid inputs");
        storeCount_ += count;
        storeSz_ += sz;
    }

    /** Bulk-import all objects from @p srcDB into @p dstBackend.
     *
     *  Iterates @p srcDB via `forEach()`, accumulates objects into batches of
     *  `kBATCH_WRITE_PREALLOCATION_SIZE`, and flushes each batch with
     *  `dstBackend.storeBatch()`. Byte statistics are recorded via
     *  `storeStats()` after each flush. On exception, logs the error and
     *  returns early without aborting the overall import.
     *
     *  Called by subclass `importDatabase()` implementations.
     *
     *  @param dstBackend Destination backend; must be open and writable.
     *  @param srcDB      Source database; iterated sequentially — no concurrent
     *      writes to @p srcDB should occur during the call.
     */
    void
    importInternal(Backend& dstBackend, Database& srcDB);

    /** Merge externally-collected fetch metrics into the atomic counters.
     *
     *  Used by subclasses that perform their own batched reads (e.g. import
     *  paths) and need to credit the counters in bulk rather than per-object.
     *
     *  @param fetches   Number of fetch attempts to add to `fetchTotalCount_`.
     *  @param hits      Number of successful fetches to add to `fetchHitCount_`.
     *  @param duration  Elapsed microseconds to add to `fetchDurationUs_`.
     */
    void
    updateFetchMetrics(uint64_t fetches, uint64_t hits, uint64_t duration)
    {
        fetchTotalCount_ += fetches;
        fetchHitCount_ += hits;
        fetchDurationUs_ += duration;
    }

private:
    // --- Write-side atomic counters ---
    std::atomic<std::uint64_t> storeCount_{0};      ///< Total objects stored.
    std::atomic<std::uint64_t> storeSz_{0};          ///< Total bytes stored.
    std::atomic<std::uint64_t> storeDurationUs_{0};  ///< Cumulative store duration (µs); reserved.

    // --- Fetch-side atomic counters (incremented by the public fetchNodeObject wrapper) ---
    std::atomic<std::uint64_t> fetchTotalCount_{0};  ///< Total fetch attempts.
    std::atomic<std::uint64_t> fetchDurationUs_{0};  ///< Cumulative fetch duration (µs).

    // --- Async read-queue state (all guarded by readLock_ except atomic members) ---
    mutable std::mutex readLock_;           ///< Guards `read_` and `readCondVar_`.
    std::condition_variable readCondVar_;   ///< Wakes worker threads when `read_` is non-empty or stopping.

    /** Pending async read requests, keyed by hash.
     *
     *  Each map entry holds all `(ledgerSeq, callback)` pairs registered for a
     *  given hash. Multiple calls to `asyncFetch()` with the same hash are
     *  coalesced here so that a single backend read services all callbacks.
     */
    std::map<
        uint256,
        std::vector<
            std::pair<std::uint32_t, std::function<void(std::shared_ptr<NodeObject> const&)>>>>
        read_;

    std::atomic<bool> readStopping_ = false;  ///< Set by `stop()`; workers exit when observed.
    std::atomic<int> readThreads_ = 0;        ///< Count of live worker threads; reaches 0 on full stop.
    std::atomic<int> runningThreads_ = 0;     ///< Threads currently active (not blocked on condvar).

    /** Backend fetch hook — the Template Method target.
     *
     *  Called exclusively by the public non-virtual `fetchNodeObject()` wrapper,
     *  which applies timing and metrics around this call. Subclasses must
     *  implement this and may not call the public wrapper from within it.
     *
     *  @param hash        256-bit content hash to look up.
     *  @param ledgerSeq   Ledger sequence, used by rotating backends to select
     *      the correct physical file.
     *  @param fetchReport Mutable report populated by the implementation;
     *      the public wrapper reads `fetchReport.wasFound` and `elapsed`.
     *  @param duplicate   When `true`, if the object is found in the archive
     *      backend it should also be written back to the writable backend
     *      (promotion path for `DatabaseRotatingImp`).
     *  @return The fetched `NodeObject`, or `nullptr` on miss or error.
     */
    virtual std::shared_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        FetchReport& fetchReport,
        bool duplicate) = 0;

    /** Iterate every object in the database and invoke @p f for each one.
     *
     *  Used exclusively by `importInternal()`. Implementations may close and
     *  reopen the underlying store (e.g. NuDB) and are not safe for concurrent
     *  access; the caller must ensure no other reads or writes occur during
     *  iteration.
     *
     *  @note Never called concurrently with itself or other methods.
     *  @param f Callback invoked with each `NodeObject`; must not be null.
     */
    virtual void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) = 0;

    /** Worker thread body for the async read pool.
     *
     *  Loops waiting on `readCondVar_`, extracts up to `requestBundle_` entries
     *  from `read_` per lock acquisition, and dispatches each to the private
     *  `fetchNodeObject()`. Handles multi-sequence coalescing via `isSameDB()`.
     *  Exits when `readStopping_` is observed, then decrements `readThreads_`.
     */
    void
    threadEntry();
};

}  // namespace xrpl::NodeStore
