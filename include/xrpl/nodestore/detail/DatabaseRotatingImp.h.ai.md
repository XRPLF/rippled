# `DatabaseRotatingImp` — Concrete Rotating NodeStore Backend

## Role in the System

The XRPL node store persists every ledger object (SHAMap nodes, transactions, account states) as keyed binary blobs identified by their 256-bit hash. Over time, old ledger data accumulates and must be pruned without downtime — the "online deletion" feature. `DatabaseRotatingImp` is the concrete class that implements this rotation strategy by maintaining two simultaneously live storage backends: a **writable** backend that accepts new writes and a read-only **archive** backend holding older data scheduled for eventual deletion. When online deletion fires, the old archive is discarded, the writable backend is demoted to archive, and a freshly created backend becomes the new writable target. The header declares the class interface; the implementation lives in `src/libxrpl/nodestore/DatabaseRotatingImp.cpp`.

The class sits at the bottom of a three-level inheritance chain: `Database` → `DatabaseRotating` → `DatabaseRotatingImp`. `Database` provides the threading infrastructure (async read threads, statistics counters, `fetchNodeObject()` public façade), `DatabaseRotating` declares the pure-virtual `rotate()` contract, and `DatabaseRotatingImp` supplies every concrete method.

## Backend Lifecycle and the `rotate()` Operation

The heart of the class is `rotate()`. When called, it must atomically swap out the old archive, promote the current writable to archive, and install the new writable — all while concurrent reads and writes may be in flight.

```cpp
void rotate(
    std::unique_ptr<Backend>&& newBackend,
    std::function<void(std::string const&, std::string const&)> const& f) override;
```

The implementation captures the old archive in a local `shared_ptr` (`oldArchiveBackend`), performs the pointer swap under `mutex_`, then calls the callback `f` outside the lock. The critical design choice is that `oldArchiveBackend` remains alive until *after* `f` returns. This is intentional: the callback is used by the caller (online deletion logic) to update external metadata — for example, marking which backend directories are canonical in a configuration record. Only once `f` completes and `oldArchiveBackend` falls out of scope does the backend destructor run, physically deleting the old storage directory via the previously set `setDeletePath()` flag. This sequencing prevents the caller's metadata from pointing to a path that has already been removed.

The three-step pointer shuffle under the lock is:
1. Mark old archive for deletion.
2. Move old archive into `oldArchiveBackend` (local RAII guard).
3. Demote `writableBackend_` → `archiveBackend_`.
4. Install `newBackend` → `writableBackend_`.

## Locking Strategy: Snapshot-and-Release

`DatabaseRotatingImp` uses `mutex_` only to snapshot `shared_ptr` references, never while doing actual I/O. In every read/write path the pattern is:

```cpp
auto const backend = [&] {
    std::lock_guard lock(mutex_);
    return writableBackend_;   // copy the shared_ptr
}();
// I/O happens here, no lock held
backend->store(nObj);
```

This is the right design because backend I/O (especially to RocksDB or NuDB) can take milliseconds, and holding `mutex_` during I/O would serialize all access including `rotate()` calls. Instead, the `shared_ptr` copy keeps the backend alive even if a rotation fires concurrently.

`fetchNodeObject()` takes this a step further: it snapshots *both* pointers together in a single lock acquisition, then attempts the writable backend first and the archive backend second — all without holding the lock. If the object is found only in the archive and the caller sets `duplicate = true`, the method re-acquires the lock to refresh the writable pointer (in case a rotation occurred during the archive lookup), then promotes the object back into the current writable backend. This forward-migration of archive data is how the system ensures hot data is not accidentally swept away by the next rotation cycle.

## Fetch Fallthrough and Data Promotion

The fetch path is a deliberate two-tier lookup:

1. Check the writable backend (recent data, faster).
2. On miss, check the archive backend (older data, possibly on its way out).
3. If found in the archive and `duplicate == true`, write a copy back into the writable backend.

The `duplicate` flag is supplied by the upper-layer cache logic in `Database::fetchNodeObject()` and enables background data migration: objects retrieved from the archive are refreshed into the writable tier so that when the archive is eventually rotated out, all accessed objects are already safe in the new backend. Objects that have not been accessed since the last rotation simply disappear with the old archive.

## Logical Identity and Ignored Parameters

`isSameDB()` unconditionally returns `true`. The base `Database` contract uses this to determine whether two ledger sequence numbers would be answered by the same physical store; for the rotating database both backends together form one logical namespace, so the answer is always yes.

Similarly, the `std::uint32_t ledgerSeq` parameters throughout `store()` and `fetchNodeObject()` are deliberately unnamed and unused — the rotating store does not partition data by ledger sequence the way sharded databases do.

## Resource Management

The constructor aggregates `fdRequired_` from both backends:

```cpp
if (writableBackend_)
    fdRequired_ += writableBackend_->fdRequired();
if (archiveBackend_)
    fdRequired_ += archiveBackend_->fdRequired();
```

This propagates the sum to the base class so the process can correctly reserve file descriptors at startup before both backends are open.

The destructor calls `stop()` (inherited from `Database`), which drains async read threads before the backends are released. This ordering is essential: the async threads hold `shared_ptr` references to backends and would dereference freed memory if allowed to outlive the destructor.

`sync()` flushes only the writable backend. The archive backend is read-only at rotation time and needs no explicit flush.