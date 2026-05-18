# `DatabaseRotatingImp.cpp` — Concrete Rotating Node-Store Backend

## Role in the System

`DatabaseRotatingImp` is the concrete implementation that powers XRPL's **online deletion** feature. The broader mechanism is built around the insight that a running node cannot compact or delete ledger data from a single live database, because reads and writes to that database are continuous. The solution is a two-backend architecture: one backend is _writable_ (receiving all new stores) and one is _archive_ (holding older data). When enough time has elapsed and old ledgers have been validated beyond the configured deletion threshold, the archive backend is discarded and a freshly-created backend becomes the new writable target — the previous writable is demoted to archive in turn. This file contains the implementation of the class that makes that lifecycle work, sitting between the `SHAMapStore` sweep thread and the raw `Backend` storage layer.

The class inherits from `DatabaseRotating`, which itself extends `Database` — the general XRPL node-store abstraction. The two members `writableBackend_` and `archiveBackend_` are `std::shared_ptr<Backend>`, allowing them to be atomically replaced while outstanding I/O operations on the old handle remain valid for the lifetime of the shared reference.

## The Rotation Protocol

`rotate()` is the heart of the class and the subtlest method. It takes a `std::unique_ptr<Backend>&&` (the freshly prepared replacement) and a callback that will receive the resulting backend names. The sequence inside the mutex is:

1. Mark the existing archive backend for deletion on destruction (`setDeletePath()`), then move it out into a local `oldArchiveBackend`.
2. Promote the current writable backend to become the new archive.
3. Install `newBackend` as the writable backend.

The lock is then **released before the callback runs**. This is intentional: the callback (in production, `SHAMapStoreImp`) persists the new writable/archive names to a SQLite state database. That disk write must happen while `oldArchiveBackend` is still alive — held by the local variable on the stack — so the old archive directory is not deleted until after the state database is updated. The `oldArchiveBackend` shared_ptr falls out of scope only after `f()` returns, at which point its destructor fires and cleans up the on-disk data. This sequencing makes the rotation crash-safe: if the process dies between the atomic swap and the state persistence, the node can recover by reading back the previous state from SQLite.

A naming subtlety: `newWritableBackendName` is captured _before_ acquiring the lock (by calling `getName()` on the new backend), and `newArchiveBackendName` is captured _inside_ the lock (by reading the demoted former writable). Both values are passed to the callback so it can update persistent state without needing to re-query under lock.

## Thread-Safety Pattern: Capture Under Lock, Use Outside Lock

Every operation that touches either backend follows the same pattern: acquire the mutex, copy the `shared_ptr` into a local variable, release the lock, then call the backend through the local. For example in `store()`:

```cpp
auto const backend = [&] {
    std::lock_guard const lock(mutex_);
    return writableBackend_;
}();
backend->store(nObj);
```

This is not an oversight — it is deliberate. The lock only needs to protect the pointer swap, not the entire backend I/O operation, which may block. Holding the mutex across a disk write would serialize all readers and writers, eliminating concurrency. By capturing the `shared_ptr` under the lock and releasing before I/O, the code achieves safe pointer visibility without blocking unrelated threads.

The exception is `sync()`, which does hold the lock for the entire backend sync call. This is acceptable because `sync()` is a maintenance operation, not a latency-sensitive read/write path.

## Fetch Promotion

`fetchNodeObject()` implements a two-tier lookup with optional write-back. It first tries the writable backend, then falls back to the archive backend. If the object is found only in the archive _and_ the `duplicate` flag is true, the object is copied back into the writable backend:

```cpp
if (duplicate)
    writable->store(nodeObject);
```

This promotion matters because recently-fetched archive objects are likely to be accessed again, and having them in the writable tier reduces future archive lookups. After the archive fetch succeeds, the code re-acquires the mutex to refresh the `writable` local pointer. This handles the race where a rotation occurs between when the archive lookup started and when the write-back is about to happen — without the refresh, the write-back would land in the now-demoted (and soon-to-be-deleted) former writable backend rather than the current one.

The `fetchNodeObject` inner `fetch` lambda catches `std::exception`, logs it at fatal severity, and rethrows via `Rethrow()`. Backend corruption (`dataCorrupt` status) is logged fatally but does not throw — the null `nodeObject` propagates up as a cache miss. Unknown statuses are logged at warning level. This conservative approach avoids crashing on correctable backend errors while surfacing data corruption prominently.

## File Descriptor Accounting

The constructor accumulates `fdRequired_` by querying each backend for its file descriptor needs, guarded by null checks. This aggregate is surfaced to the system-level resource validator so the process can request enough file descriptors from the OS before opening any backend.

## Relationship to `SHAMapStoreImp`

The real orchestration of when to rotate, how to build `newBackend`, and how to clear in-memory caches lives in `SHAMapStoreImp`. `DatabaseRotatingImp` is deliberately narrow: it manages the thread-safe pointer swap and the deferred deletion lifecycle, but delegates policy entirely to its caller through the callback interface. This separation means the rotation logic can be tested in isolation — the tests in `SHAMapStore_test.cpp` create a `DatabaseRotatingImp` directly and call `rotate()` with custom callbacks, without needing a full `SHAMapStoreImp`.