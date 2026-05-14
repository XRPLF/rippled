/** @file
 *  Single-backend concrete implementation of the NodeStore `Database` interface.
 *
 *  `DatabaseNodeImp` is the standard node-store path for deployments that keep
 *  all ledger objects in one persistent key/value backend (NuDB, RocksDB, etc.).
 *  It adapts the thin `Backend` interface onto the richer `Database` contract
 *  — async read pool, telemetry, and scheduler callbacks — all of which live in
 *  the base class and cannot be bypassed.
 */

#pragma once

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/nodestore/Database.h>

namespace xrpl::NodeStore {

/** Single-backend implementation of the NodeStore `Database` interface.
 *
 *  Wraps exactly one `Backend` (NuDB, RocksDB, Memory, Null) and serves all
 *  ledger objects regardless of their ledger sequence number. This is the
 *  standard deployment path; the two-backend rotation variant is
 *  `DatabaseRotatingImp`.
 *
 *  Every public method is a thin delegation: to `backend_` for storage
 *  operations and to base-class helpers for async dispatch, telemetry, and
 *  bulk import. No business logic lives here.
 *
 *  **Shutdown ordering**: The destructor calls `stop()` to drain all pending
 *  async reads and wait for worker threads to exit before releasing `backend_`.
 *  Worker threads invoke the virtual `fetchNodeObject()` hook; if `backend_`
 *  were released while a thread was active, it would dereference a dangling
 *  pointer.
 *
 *  @see Database, DatabaseRotatingImp, Backend
 */
class DatabaseNodeImp : public Database
{
public:
    DatabaseNodeImp() = delete;
    DatabaseNodeImp(DatabaseNodeImp const&) = delete;
    DatabaseNodeImp&
    operator=(DatabaseNodeImp const&) = delete;

    /** Construct the database and start the async read thread pool.
     *
     *  Asserts that @p backend is non-null, then delegates to the `Database`
     *  base constructor which spawns `readThreads` detached worker threads.
     *
     *  @param scheduler   Task scheduler for async I/O dispatch and telemetry;
     *      must outlive this object.
     *  @param readThreads Number of async prefetch threads; clamped to at least 1
     *      by the base constructor.
     *  @param backend     Open, non-null backend to use for all storage; shared
     *      ownership is assumed.
     *  @param config      `[node_db]` config section; forwarded to `Database`
     *      for `earliest_seq` and `rq_bundle` parsing.
     *  @param j           Logging sink.
     */
    DatabaseNodeImp(
        Scheduler& scheduler,
        int readThreads,
        std::shared_ptr<Backend> backend,
        Section const& config,
        beast::Journal j)
        : Database(scheduler, readThreads, config, j), backend_(std::move(backend))
    {
        XRPL_ASSERT(
            backend_,
            "xrpl::NodeStore::DatabaseNodeImp::DatabaseNodeImp : non-null "
            "backend");
    }

    /** Drain pending I/O and release the backend.
     *
     *  Calls `stop()` to wait for all async read worker threads to exit before
     *  `backend_` is destroyed. This must happen in the derived destructor
     *  because worker threads call the virtual `fetchNodeObject()` hook, which
     *  dereferences `backend_`.
     */
    ~DatabaseNodeImp() override
    {
        stop();
    }

    /** Return the name of the underlying backend for diagnostics.
     *
     *  @return The backend's human-readable identifier (e.g. the on-disk path).
     */
    std::string
    getName() const override
    {
        return backend_->getName();
    }

    /** Return the estimated number of pending write operations in the backend.
     *
     *  Approximate; the value may change immediately after it is read.
     *
     *  @return Pending write count, or 0 if the backend does not batch writes.
     */
    std::int32_t
    getWriteLoad() const override
    {
        return backend_->getWriteLoad();
    }

    /** Bulk-import all objects from @p source into this database's backend.
     *
     *  Delegates to `importInternal()`, which iterates @p source via `forEach()`
     *  and stores objects in batches. Large source databases may take significant
     *  time; no concurrent writes to @p source should occur during the call.
     *
     *  @param source The database to read from; must remain open and quiescent.
     */
    void
    importDatabase(Database& source) override
    {
        importInternal(*backend_.get(), source);
    }

    /** Persist a node object to the backend.
     *
     *  Updates store telemetry, wraps the payload in a `NodeObject`, and
     *  forwards to the backend. The ledger sequence parameter is part of the
     *  `Database` contract but is ignored here — a single backend holds objects
     *  from all ledger sequences.
     *
     *  @param type Type tag for the object (ledger, account node, etc.).
     *  @param data Serialized payload; ownership is transferred — the caller's
     *      variable is left in a valid but unspecified state.
     *  @param hash 256-bit content-address key. The caller is responsible for
     *      correctness; the hash is not re-verified.
     */
    void
    store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t) override;

    /** Report whether two ledger sequence numbers map to the same backend.
     *
     *  Always returns `true` for `DatabaseNodeImp` because there is exactly one
     *  backend: every sequence number resolves to the same physical store. This
     *  allows the async read pool to coalesce duplicate hash requests that carry
     *  different sequence numbers without issuing a second backend read.
     *
     *  @return `true` unconditionally.
     */
    bool
    isSameDB(std::uint32_t, std::uint32_t) override
    {
        return true;
    }

    /** Flush any buffered writes to durable storage.
     *
     *  Delegates directly to `backend_->sync()`. Not latency-sensitive;
     *  typically called on ledger close or maintenance paths.
     */
    void
    sync() override
    {
        backend_->sync();
    }

    /** Synchronously fetch a batch of node objects by hash.
     *
     *  Calls `backend_->fetchBatch()` directly, bypassing the async read queue.
     *  Enforces a positional contract: the returned vector is always the same
     *  length as @p hashes, with null entries for objects not found. Missing
     *  objects are logged at `error` level. Wall-clock elapsed time is reported
     *  via `updateFetchMetrics()`; per-slot hit counts are not tracked here and
     *  remain the caller's responsibility.
     *
     *  @note The batch-level `Status` from the backend is discarded; object
     *      availability is inferred entirely from null vs. non-null slots.
     *  @param hashes Ordered list of 256-bit keys to retrieve.
     *  @return Vector of the same length as @p hashes; null entries indicate
     *      objects absent from the backend.
     */
    std::vector<std::shared_ptr<NodeObject>>
    fetchBatch(std::vector<uint256> const& hashes);

    /** Schedule a non-blocking background fetch for a single node object.
     *
     *  Forwards unconditionally to `Database::asyncFetch()`, which coalesces
     *  duplicate hash requests and dispatches callbacks from the worker thread
     *  pool. No per-backend routing is needed for the single-backend case.
     *
     *  @param hash      256-bit key of the object to retrieve.
     *  @param ledgerSeq Ledger sequence the object belongs to; forwarded for
     *      hash-coalescing decisions via `isSameDB()`.
     *  @param callback  Invoked on a worker thread with the fetched `NodeObject`,
     *      or `nullptr` on miss or error.
     */
    void
    asyncFetch(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        std::function<void(std::shared_ptr<NodeObject> const&)>&& callback) override;

private:
    /** The single persistent key/value backend that holds all ledger objects. */
    std::shared_ptr<Backend> backend_;

    /** Template Method hook called by the base-class public `fetchNodeObject()`.
     *
     *  Delegates to `backend_->fetch()` with structured error logging:
     *  `Status::Ok` and `Status::NotFound` are silent; `Status::DataCorrupt`
     *  logs at `fatal`; any other code logs at `warn`. Exceptions from the
     *  backend are logged at `fatal` then re-raised via `Rethrow()`. Sets
     *  `fetchReport.wasFound = true` on a hit to feed the base-class metric.
     *  The ledger sequence parameter is accepted by the signature but unused.
     *
     *  @param hash        256-bit key to look up.
     *  @param fetchReport Mutable report; `wasFound` is set on a hit.
     *  @param duplicate   Whether this fetch was deduplicated from another
     *      in-flight request for the same hash; unused in this implementation.
     *  @return The fetched `NodeObject`, or `nullptr` on miss or error.
     *  @throws Any exception propagated from `backend_->fetch()` after logging.
     */
    std::shared_ptr<NodeObject>
    fetchNodeObject(uint256 const& hash, std::uint32_t, FetchReport& fetchReport, bool duplicate)
        override;

    /** Iterate every object in the backend and invoke @p f for each one.
     *
     *  Used exclusively by `importInternal()` for bulk export. Delegates
     *  directly to `backend_->forEach()`. Not safe for concurrent access with
     *  reads or writes; see `Backend::forEach()` for details.
     *
     *  @param f Callback invoked with each `NodeObject`; must not be null.
     */
    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override
    {
        backend_->forEach(f);
    }
};

}  // namespace xrpl::NodeStore
