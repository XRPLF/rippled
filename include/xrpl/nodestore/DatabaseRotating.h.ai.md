# `include/xrpl/nodestore/DatabaseRotating.h`

## Role and Purpose

`DatabaseRotating` is a minimal abstract interface that extends `Database` with exactly one additional operation: `rotate()`. Its sole purpose is to define the contract for the two-backend rotation scheme that enables **online deletion of ledger history** without taking the node offline. The class carries no state of its own; all the mechanism lives in its concrete subclass `DatabaseRotatingImp`.

## Design Context: Two-Backend Storage

The XRPL node store must keep ledger data beyond what fits in RAM, but running nodes indefinitely accumulates unbounded disk usage. The solution is an **online deletion** strategy: rather than deleting individual records (which would be expensive in key-value stores), the system maintains two physical backends in parallel — a *writable* backend that receives all new writes and a *read-only archive* backend holding older data. When enough new ledger history has accumulated in the writable backend, a rotation event swaps the backends and discards the old archive.

`DatabaseRotating` is the abstract seam between this two-backend logic and the rest of the application. Components like `SHAMapStoreImp` hold only a `DatabaseRotating*` pointer, making the rotation mechanism pluggable and testable independently of the storage format.

## The `rotate()` Method

```cpp
virtual void rotate(
    std::unique_ptr<NodeStore::Backend>&& newBackend,
    std::function<void(std::string const& writableName,
                       std::string const& archiveName)> const& f) = 0;
```

This is the entire API extension over `Database`. The caller supplies a freshly created backend that becomes the new writable store, and a callback `f` that fires after the in-memory rotation is complete but before the old archive directory is physically deleted.

The callback design is deliberate and load-bearing. In `DatabaseRotatingImp::rotate()`, the pointer swap happens inside a `mutex_` lock — the old archive is marked for deletion and kept alive in a local `shared_ptr`, the current writable backend becomes the new archive, and the new backend becomes the new writable. After releasing the lock, `f` is called with the new names. Only after `f` returns does `oldArchiveBackend` go out of scope and the old files are removed.

This sequencing lets `SHAMapStoreImp` durably persist the new backend names and `lastRotated` ledger sequence to a SQL state database inside `f`, creating an atomic checkpoint: if the process crashes between the pointer swap and state persistence, the on-disk backend directories still exist and can be recovered on restart. The old archive directory is never unlinked until the new state is committed.

## Fetch Strategy

The concrete `fetchNodeObject` implementation in `DatabaseRotatingImp` first attempts a fetch from the writable backend, then falls back to the archive backend if not found. When a node is retrieved from the archive, and the `duplicate` flag is set, the object is promoted — written back into the current writable backend — so that future fetches find it without traversing the archive. This matters because after the *next* rotation, the archive backend will be discarded; data that hasn't been promoted would be permanently lost unless it's been written to the new writable store.

The `mutex_` in `DatabaseRotatingImp` guards the backend shared-pointer references. The fetch implementation takes a snapshot of both `writableBackend_` and `archiveBackend_` under the lock, then releases it before performing I/O. This is a deliberate choice: holding a lock across disk I/O would serialize concurrent fetches. The snapshot approach lets multiple threads read concurrently, with the tradeoff that a rotation can occur between snapshot and fetch — handled safely because `shared_ptr` reference counting keeps the old backend alive until all threads are done with it.

## `isSameDB()` Returns True Unconditionally

`DatabaseRotatingImp::isSameDB()` always returns `true`, ignoring both sequence number arguments. This reflects a semantic choice: from the application layer's perspective, the rotating store is one logical database, regardless of which physical backend holds a given ledger. Callers use `isSameDB` to decide whether two ledger sequences are co-located and can be fetched with identical results; the rotating store always satisfies this predicate because `fetchNodeObject` transparently spans both backends.

## Relationship to `SHAMapStoreImp`

The rotation is driven by `SHAMapStoreImp`, which runs a background thread that monitors validated ledger advancement. When enough ledgers have accumulated, it copies the minimum required ledger data into a freshly created backend via `makeBackendRotating()`, freshens in-memory caches, clears stale entries, and then calls `dbRotating_->rotate()`. Inside `rotate()`'s callback, `SHAMapStoreImp` writes the new `SavedState` (writable name, archive name, last-rotated sequence) to a soci/SQLite state database, providing crash-safe restart semantics.

The minimum rotation interval enforced by `SHAMapStoreImp` (256 ledgers in networked mode, 8 in standalone) ensures at least one full epoch of history is always retained, protecting network health and preventing data loss during brief unavailability of peers.