#pragma once

#include <xrpl/nodestore/DatabaseRotating.h>

#include <mutex>

namespace xrpl::NodeStore {

/** Concrete two-backend node store that enables online deletion of old ledger data.
 *
 *  Maintains a _writable_ backend (receives all new stores) and an _archive_
 *  backend (holds older data). The `SHAMapStore` sweep thread drives rotations:
 *  when the configured deletion horizon is reached it calls `rotate()`, which
 *  atomically promotes the current writable to archive, installs a fresh backend
 *  as the new writable, and schedules the old archive for deletion.
 *
 *  All public methods follow a capture-under-lock / use-outside-lock pattern:
 *  the mutex protects only the `shared_ptr` swap, not the backend I/O.  This
 *  keeps unrelated readers and writers concurrent during disk operations.
 *
 *  **Thread safety**: all public methods are safe to call from any thread
 *  concurrently.  `stop()` must be called in the derived destructor before
 *  the base `Database` destructor tears down the async read pool.
 *
 *  @see DatabaseRotating, Database, SHAMapStoreImp
 */
class DatabaseRotatingImp : public DatabaseRotating
{
public:
    DatabaseRotatingImp() = delete;
    DatabaseRotatingImp(DatabaseRotatingImp const&) = delete;
    DatabaseRotatingImp&
    operator=(DatabaseRotatingImp const&) = delete;

    /** Construct the rotating database and initialise the async read pool.
     *
     *  Both backends must already be open.  Their `fdRequired()` values are
     *  accumulated into `fdRequired_` so the application can pre-validate the
     *  process file-descriptor limit before any I/O begins.
     *
     *  @param scheduler        Task scheduler for async dispatch and telemetry;
     *      must outlive this object.
     *  @param readThreads      Number of async read worker threads to spawn.
     *  @param writableBackend  The backend that receives all new stores.
     *  @param archiveBackend   The backend holding older (pre-rotation) data.
     *  @param config           `[node_db]` config section forwarded to `Database`.
     *  @param j                Logging sink.
     */
    DatabaseRotatingImp(
        Scheduler& scheduler,
        int readThreads,
        std::shared_ptr<Backend> writableBackend,
        std::shared_ptr<Backend> archiveBackend,
        Section const& config,
        beast::Journal j);

    /** Destroy the rotating database.
     *
     *  Calls `stop()` before the base destructor so that async worker threads
     *  stop invoking the virtual `fetchNodeObject()` while derived data members
     *  are still valid.
     */
    ~DatabaseRotatingImp() override
    {
        stop();
    }

    /** Atomically swap in a new writable backend, demoting the current one.
     *
     *  The rotation sequence under the mutex is:
     *  1. Mark the existing archive backend for on-disk deletion, move it into
     *     a local to extend its lifetime past the callback.
     *  2. Promote the current writable backend to become the new archive.
     *  3. Install @p newBackend as the writable backend.
     *
     *  The lock is released before @p f is called.  This ordering is critical:
     *  the callback (in production, `SHAMapStoreImp`) persists the new backend
     *  names to a SQLite state database.  The old archive `shared_ptr` remains
     *  alive on the stack until after @p f returns, so the archive directory is
     *  deleted only after the persistent state has been updated — making the
     *  rotation crash-safe.
     *
     *  @param newBackend  Freshly prepared backend to install as the new writable.
     *      Ownership is transferred; the caller's pointer is null on return.
     *  @param f           Callback invoked after the swap, outside the mutex.
     *      Receives the new writable name and the new archive name (the former
     *      writable).  Must persist these names to durable storage before
     *      returning so the node can recover the correct layout after a crash.
     *  @note The callback is invoked outside the mutex, so other methods
     *      (including `getName()` and even `rotate()`) may be called from within
     *      @p f without deadlocking.  Re-entering `rotate()` from @p f is
     *      technically safe but should never occur in production code.
     */
    void
    rotate(
        std::unique_ptr<NodeStore::Backend>&& newBackend,
        std::function<void(std::string const& writableName, std::string const& archiveName)> const&
            f) override;

    /** Return the name of the current writable backend.
     *
     *  Acquires the mutex to take a consistent snapshot of `writableBackend_`.
     *
     *  @return A human-readable identifier for the writable backend.
     */
    std::string
    getName() const override;

    /** Return the estimated pending write count from the writable backend.
     *
     *  Acquires the mutex to snapshot `writableBackend_`, then queries it
     *  outside the lock.
     *
     *  @return Pending write count; 0 if the backend does not batch writes.
     */
    std::int32_t
    getWriteLoad() const override;

    /** Bulk-import all objects from @p source into the current writable backend.
     *
     *  Snapshots `writableBackend_` under the mutex, then delegates to
     *  `importInternal()`.  A rotation that occurs concurrently will not affect
     *  the import — it continues writing to the backend that was writable when
     *  it started.
     *
     *  @param source  Source database to read from; must remain valid and
     *      quiescent (no concurrent writes) for the duration of the call.
     */
    void
    importDatabase(Database& source) override;

    /** Return `true`, since both backends form a single logical namespace.
     *
     *  The async read pool calls this to decide whether two in-flight fetches
     *  for the same hash (with different ledger sequence numbers) can share a
     *  single backend read.  Because the rotating store presents one logical
     *  keyspace across both tiers, this always returns `true`.
     *
     *  @return Always `true`.
     */
    bool
    isSameDB(std::uint32_t, std::uint32_t) override
    {
        return true;
    }

    /** Store a node object in the current writable backend.
     *
     *  Snapshots `writableBackend_` under the mutex, constructs a `NodeObject`
     *  from the supplied data, then writes it outside the lock.  The ledger
     *  sequence parameter is accepted for interface compatibility but ignored —
     *  all writes always go to the current writable backend regardless of age.
     *
     *  @param type       Semantic type of the object.
     *  @param data       Serialized payload; moved into the backend.
     *  @param hash       256-bit content hash; not re-verified.
     *  @param ledgerSeq  Ignored; present for `Database` interface compatibility.
     */
    void
    store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t) override;

    /** Flush the writable backend to durable storage.
     *
     *  Holds the mutex for the entire sync call.  Acceptable because this is a
     *  maintenance path, not a latency-sensitive read/write path.
     */
    void
    sync() override;

private:
    /** Active backend; receives all new `store()` calls. */
    std::shared_ptr<Backend> writableBackend_;

    /** Read-only backend holding data from before the last rotation. */
    std::shared_ptr<Backend> archiveBackend_;

    /** Guards swaps of `writableBackend_` and `archiveBackend_`.
     *  Held only for pointer capture or swap — never across I/O.
     */
    mutable std::mutex mutex_;

    /** Two-tier fetch with optional archive-to-writable promotion.
     *
     *  Snapshots both backend pointers under the mutex, then tries the writable
     *  backend first.  On a miss, tries the archive backend.  If the object is
     *  found in the archive and @p duplicate is `true`, the writable pointer is
     *  refreshed under the mutex (to handle a concurrent rotation) and the
     *  object is written back into the current writable tier.
     *
     *  Backend errors are handled conservatively: `DataCorrupt` is logged at
     *  fatal severity and returns `nullptr` (cache miss); unknown status codes
     *  are logged at warning level; exceptions are logged and rethrown via
     *  `Rethrow()`.
     *
     *  @param hash         256-bit content hash of the desired object.
     *  @param ledgerSeq    Ignored; accepted for `Database` virtual interface.
     *  @param fetchReport  Out-param; `wasFound` is set to `true` on a hit.
     *  @param duplicate    When `true`, a hit in the archive is promoted to
     *      the writable backend.
     *  @return The found `NodeObject`, or `nullptr` on miss or error.
     */
    std::shared_ptr<NodeObject>
    fetchNodeObject(uint256 const& hash, std::uint32_t, FetchReport& fetchReport, bool duplicate)
        override;

    /** Visit every object in both backends sequentially.
     *
     *  Snapshots both backend pointers under the mutex, then calls
     *  `writable->forEach(f)` followed by `archive->forEach(f)` outside the
     *  lock.  Used by `importInternal()` during bulk import.
     *
     *  @param f  Callable invoked with each `NodeObject`; must not call any
     *      method that acquires `mutex_` to avoid deadlock.
     *  @note Not safe to call concurrently with `rotate()` or other writes if
     *      the backend's `for_each` re-opens the database (e.g. NuDB).
     */
    void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) override;
};

}  // namespace xrpl::NodeStore
