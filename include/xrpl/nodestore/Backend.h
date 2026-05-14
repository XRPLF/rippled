#pragma once

#include <xrpl/nodestore/Types.h>

#include <cstdint>

namespace xrpl::NodeStore {

/** Pure abstract storage interface for the NodeStore persistence layer.
 *
 *  Every ledger object (account states, transactions, ledger headers) is a
 *  `NodeObject` keyed by its 256-bit hash. `Backend` defines the narrow
 *  interface that lets the `Database` layer remain independent of the
 *  underlying engine — NuDB, RocksDB, or an in-memory store for tests all
 *  satisfy this contract identically.
 *
 *  A backend instance is fixed to a particular key size (always 32 bytes in
 *  practice, matching `NodeObject::keyBytes`) at construction.
 *
 *  **Concurrency contract**: `fetch()` and `store()` will be called
 *  concurrently by multiple threads; implementations must be internally
 *  thread-safe for these two operations. `storeBatch()` and `forEach()` are
 *  never called concurrently with each other or with other writes.
 *
 *  **Lifecycle**: Construction is separated from initialization via `open()`.
 *  Backends are never constructed directly — use `Factory::createInstance()`
 *  dispatched through `Manager`.
 *
 *  @see Factory, Manager, Database
 */
class Backend
{
public:
    /** Destroy the backend.
     *
     *  All open files are closed and flushed. Any batched writes or scheduled
     *  tasks complete before this returns, so dropping a `unique_ptr<Backend>`
     *  cannot silently discard data.
     */
    virtual ~Backend() = default;

    /** Return the human-readable name of this backend, used in diagnostics. */
    virtual std::string
    getName() = 0;

    /** Return the storage block size, if the backend has a meaningful one.
     *
     *  NuDB organizes data into fixed-size blocks; callers that care about
     *  I/O alignment or prefetch granularity can query this without
     *  downcasting. Backends with no block concept return `std::nullopt`.
     *
     *  @return Block size in bytes, or `std::nullopt` if not applicable.
     */
    [[nodiscard]] virtual std::optional<std::size_t>
    getBlockSize() const
    {
        return std::nullopt;
    }

    /** Open the backend, optionally creating the database if absent.
     *
     *  Separating `open()` from the constructor allows I/O errors to be
     *  caught without wrapping constructors in try/catch.
     *
     *  @param createIfMissing If `true`, create the database files when they
     *      do not exist. Pass `false` to fail fast on a missing database.
     *  @throws implementation-defined exception on I/O or database errors.
     */
    virtual void
    open(bool createIfMissing = true) = 0;

    /** Return `true` if the backend is currently open. */
    virtual bool
    isOpen() = 0;

    /** Open the backend with deterministic NuDB header parameters.
     *
     *  This overload exists exclusively to support NuDB's header-level
     *  application identification (appnum, uid, salt). It enables shard
     *  databases to be created with reproducible identifiers.
     *
     *  @param createIfMissing Create the database files if they do not exist.
     *  @param appType Application-defined type tag embedded in the NuDB header.
     *  @param uid Deterministic unique identifier for this database instance.
     *  @param salt Deterministic salt value used during NuDB database creation.
     *  @throws std::runtime_error for every backend except NuDB, as this
     *      capability is not part of the general interface.
     *  @note Non-NuDB backends inherit a default implementation that always
     *      throws, clearly advertising that the capability is unavailable.
     */
    virtual void
    open(bool createIfMissing, uint64_t appType, uint64_t uid, uint64_t salt)
    {
        Throw<std::runtime_error>(
            "Deterministic appType/uid/salt not supported by backend " + getName());
    }

    /** Close the backend, flushing any pending writes.
     *
     *  Separating `close()` from the destructor allows the caller to catch
     *  and handle I/O exceptions explicitly.
     */
    virtual void
    close() = 0;

    /** Fetch a single object by its 256-bit hash.
     *
     *  On success, `*pObject` is set to the retrieved `NodeObject`. On any
     *  non-`Ok` outcome, `*pObject` is left unchanged (or reset).
     *
     *  @note Called concurrently by multiple threads; implementations must
     *      be thread-safe for this operation.
     *  @param hash The 256-bit hash key identifying the object.
     *  @param pObject Output parameter; receives the fetched object on success.
     *  @return `Status::Ok` on success, `Status::NotFound` if the key is
     *      absent, `Status::DataCorrupt` if the stored blob fails validation,
     *      or another `Status` value on backend or unknown errors.
     */
    virtual Status
    fetch(uint256 const& hash, std::shared_ptr<NodeObject>* pObject) = 0;

    /** Fetch a batch of objects by their 256-bit hashes.
     *
     *  Amortizes round-trip or I/O overhead when prefetching sets of related
     *  objects. The returned vector is parallel to `hashes`: a null
     *  `shared_ptr` at position `i` indicates the object at `hashes[i]` was
     *  not found or could not be retrieved.
     *
     *  @param hashes Ordered list of 256-bit hash keys to fetch.
     *  @return A pair of (results vector, aggregate Status). Each element in
     *      the results vector is the fetched object, or an empty
     *      `shared_ptr` if the corresponding hash was not found.
     */
    virtual std::pair<std::vector<std::shared_ptr<NodeObject>>, Status>
    fetchBatch(std::vector<uint256> const& hashes) = 0;

    /** Store a single object.
     *
     *  Depending on the implementation, the write may be synchronous or
     *  deferred to a scheduled task (e.g., via `BatchWriter`). Either way,
     *  the object is guaranteed to be durable before the backend is destroyed.
     *
     *  @note Called concurrently by multiple threads; implementations must
     *      be thread-safe for this operation.
     *  @param object The `NodeObject` to persist.
     */
    virtual void
    store(std::shared_ptr<NodeObject> const& object) = 0;

    /** Store a group of objects as a batch.
     *
     *  More efficient than repeated `store()` calls for backends that
     *  support atomic or coalesced multi-key writes (e.g., RocksDB
     *  `WriteBatch`). The entire batch is treated as a single unit.
     *
     *  @note Never called concurrently with itself or with `store()`.
     *  @param batch The collection of `NodeObject`s to persist.
     */
    virtual void
    storeBatch(Batch const& batch) = 0;

    /** Flush all previously submitted stores to durable storage.
     *
     *  Provides an explicit durability barrier: after `sync()` returns,
     *  all objects passed to `store()` or `storeBatch()` before the call
     *  are guaranteed to be on disk. Backends backed by a write-ahead log
     *  (e.g., RocksDB) may implement this as a no-op.
     */
    virtual void
    sync() = 0;

    /** Invoke a callback for every object stored in the backend.
     *
     *  Typically used during database import or migration. Because it closes
     *  and reopens the underlying database (NuDB), it must not be called
     *  while concurrent reads or writes are in flight.
     *
     *  @note Never called concurrently with itself or with any other method.
     *  @param f Callback invoked once per stored object; receives a
     *      `shared_ptr<NodeObject>` for each entry in the database.
     *  @see importInternal
     */
    virtual void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) = 0;

    /** Return an estimate of the number of pending write operations.
     *
     *  Used by the `Database` layer for back-pressure and diagnostic
     *  reporting. The value is advisory; implementations may return 0 if
     *  writes are always synchronous (e.g., NuDB).
     *
     *  @return Approximate count of writes not yet flushed to storage.
     */
    virtual int
    getWriteLoad() = 0;

    /** Schedule the backend's on-disk files for deletion on destruction.
     *
     *  After this call, the next `close()` (including the one in the
     *  destructor) removes all database files from the filesystem. Used by
     *  temporary databases — unit tests and ephemeral shard stores — that
     *  require automatic cleanup without external management.
     */
    virtual void
    setDeletePath() = 0;

    /** Perform an offline consistency check of the stored data.
     *
     *  Closes and reopens the database around the check, so it must not be
     *  called while I/O is in progress. Currently implemented only by
     *  `NuDBBackend`; all other backends inherit a no-op.
     *
     *  @note Not yet called at startup, but could one day be invoked at
     *      launch to detect on-disk corruption before it causes a crash.
     */
    virtual void
    verify()
    {
    }

    /** Return the number of file descriptors this backend expects to consume.
     *
     *  The `Database` base class aggregates these values across all open
     *  backends and exposes the total so the process can pre-check against
     *  the OS file descriptor limit before opening any databases.
     *
     *  @return Expected file descriptor count (e.g., 3 for NuDB, 0 for Null).
     */
    [[nodiscard]] virtual int
    fdRequired() const = 0;
};

}  // namespace xrpl::NodeStore
