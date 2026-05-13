# NodeStore

Persistent key-value store for `NodeObject`s (ledger entries). Every piece of ledger state — account states, transactions, ledger headers, SHAMap nodes — is serialized as a `NodeObject` keyed by its 256-bit hash and persisted here. Backends are pluggable (NuDB, RocksDB, in-memory, null) and selected via the `[node_db]` config section. The layer above (`Database`) adds an async read thread pool, batching, and statistics; `Backend` is the narrow storage interface.

## Architecture

Four layers, each with a narrow contract:

1. **`NodeObject`** (`include/xrpl/nodestore/NodeObject.h`) — immutable (type, hash, blob). Constructed only via `createObject()` factory; the `PrivateAccess` tag struct makes the public constructor effectively private while remaining compatible with `std::make_shared`. Hash is *not* verified against data — trust the caller. Inherits `CountedObject<NodeObject>` for live-instance diagnostics.
2. **`Backend`** (`include/xrpl/nodestore/Backend.h`) — pure abstract key/value interface. `fetch`/`store` are concurrent; `storeBatch`/`for_each` are not. Two-phase init: construct, then `open()`.
3. **`Database`** (`include/xrpl/nodestore/Database.h`) — owns the async read pool and stats; defines pure-virtual `fetchNodeObject(hash, seq, FetchReport&, duplicate)`. Public non-virtual `fetchNodeObject` instruments the private virtual one (timing, hit/miss, scheduler callback) — subclasses cannot bypass metrics.
4. **`Manager`** (`include/xrpl/nodestore/Manager.h`) — singleton registry mapping config `type=` strings to `Factory` instances. `make_Backend()` and `make_Database()` are the construction entry points.

Two concrete `Database` subclasses: `DatabaseNodeImp` (single backend) and `DatabaseRotatingImp` (two backends for online deletion).

## Key Invariants

- `NodeObjectType` values: `hotUNKNOWN=0`, `hotLEDGER=1`, `hotACCOUNT_NODE=3`, `hotTRANSACTION_NODE=4`, `hotDUMMY=512`. Value 2 is a historical gap. `hotDUMMY` is deliberately outside the contiguous range so it cannot collide with valid types — used as a cache sentinel meaning "confirmed missing."
- `NodeObject` lives in the `xrpl` namespace (not `xrpl::NodeStore`) because it is consumed broadly by SHAMap, ledger, and serialization layers.
- Preferred backends: NuDB (append-mostly, default) and RocksDB; Memory and Null are for tests / configured ephemerality.
- `Database` instrumentation is structural: the public `fetchNodeObject()` measures elapsed time, increments atomic counters, and calls `scheduler_.onFetch()` around every private virtual call.
- `Backend::fetch` and `Backend::store` are called concurrently from many threads; `storeBatch` and `for_each` are not. Implementations must reflect this.
- `DatabaseRotatingImp::isSameDB()` and `DatabaseNodeImp::isSameDB()` both return `true` unconditionally — the rotating store is one logical namespace despite physical split.
- Batch writes accumulate up to `batchWriteLimitSize = 65536` objects; peak in-flight memory can be ~2× this because a new batch accumulates while the previous one is being flushed (`Types.h`).
- `EncodedBlob` / `DecodedBlob` define the on-disk format: bytes 0–7 zero-padded (legacy ledger-index field), byte 8 = `NodeObjectType`, bytes 9+ = payload. Both must change together.
- `NuDBBackend::fdRequired()` returns 3 (data, key, log files). `RocksDBBackend::fdRequired()` returns `max_open_files + 128`. `DatabaseRotatingImp` sums both backends' values.
- `storeStats()` asserts `count <= sz` — byte total must be ≥ item count, guards against accounting bugs in subclasses.

## On-Disk Format

Defined jointly by `EncodedBlob` (write) and `DecodedBlob` (read) in `include/xrpl/nodestore/detail/`:

```
Bytes 0–7   Zero (historically ledger index; ignored on read)
Byte  8     NodeObjectType (one byte)
Bytes 9+    Raw serialized payload
```

The 32-byte hash is the storage key, kept separate from the value.

- `EncodedBlob` embeds a 1033-byte stack buffer (`payload_`) sized for header + 1024-byte payload (most objects). Only blobs exceeding 1024 bytes heap-allocate. `ptr_` is `uint8_t* const` set at construction; destructor frees iff `ptr_ != payload_.data()`. ~94% of real objects fit the stack buffer. The destructor `XRPL_ASSERT` verifies pointer/size coherence to catch any drift.
- `DecodedBlob` is a non-owning view into the raw buffer. Validation is by `wasOk()` flag, not exceptions. `createObject()` asserts `m_success` and copies the payload into an owning `Blob` for the returned `NodeObject`. `hotDUMMY` (512) falls through the type `switch` and leaves `m_success = false`.

NuDB additionally runs the encoded blob through `nodeobject_compress` (see Compression below).

## Compression (`include/xrpl/nodestore/detail/codec.h`)

NuDB-only. Four type tags prefix every stored blob (as a varint):

| Type | Format |
|---|---|
| 0 | Uncompressed (legacy; never written, still read) |
| 1 | LZ4-compressed |
| 2 | Sparse inner-node (bitmask + present hashes) |
| 3 | Full inner-node (all 16 hashes, no bitmask) |

**Inner-node fast path**: SHAMap inner nodes are exactly 525 bytes with `HashPrefix::innerNode` at offset 9. The compressor recognizes this and either packs only non-zero child hashes with a 16-bit presence bitmask (type 2) or stores all 16 hashes contiguously (type 3). Reconstruction zeros the `index`, `unused`, and `kind` fields — so a round-trip is *lossy* on those fields. `filter_inner()` pre-zeros them on the source side to make import-time `memcmp` verification succeed.

**LZ4 path**: `lz4_compress` stores a varint decompressed-size prefix then LZ4 payload. `lz4_decompress` validates the varint (overflow checked before `static_cast<int>`) then pre-allocates the exact output buffer before calling `LZ4_decompress_safe`. All size mismatches throw `std::runtime_error`.

**Varint** (`varint.h`): base-127 (not base-128) LEB-style encoding; bit 7 is continuation. Used for the type tag and the LZ4 decompressed-size prefix. The base-127 choice means `0x7F` never appears as a payload byte. Functions are function templates (not plain functions) to satisfy ODR across TUs. `read_varint` returns 0 on empty buffer, overrun, or overflow.

**BufferFactory pattern**: All codec functions take a callable `void*(size_t)` so allocation policy stays with the caller (NuDB passes its scratch buffer). Codecs never free memory; the factory object's lifetime governs cleanup.

## Async Read Pool (`Database`)

The base class spawns `readThreads` detached threads at construction. Each loops on `readCondVar_`, dequeues from `read_` (a `std::map<uint256, vector<pair<seq, callback>>>`), and calls the subclass's private `fetchNodeObject`.

**Hash coalescing**: Multiple concurrent `asyncFetch()` calls for the same hash collapse into a single map entry; one backend read fires all callbacks. For different `seq` values on the same hash, `isSameDB(seq1, seq2)` decides whether to reuse the fetched object or issue a second fetch.

**Batched dequeue**: Each worker extracts up to `requestBundle_` entries per lock acquisition (default 4, clamped 1–64 via `rq_bundle` config) to amortize mutex cost. Uses `read_.extract()` (C++17 node-handle) to move entries without copying.

**Threads are detached** (not joined), controlled by `readStopping_` atomic + `readThreads_` counter. `stop()` clears `read_`, broadcasts the condvar, and spin-yields until `readThreads_` reaches zero (asserted within 30s).

**Shutdown ordering — critical**: Derived classes *must* call `stop()` in their own destructors. The base destructor calls `stop()` as a safety net, but by then the derived vtable is already gone — a worker thread blocked in `fetchNodeObject` would invoke a destroyed vtable entry. See Common Bug Patterns.

**`asyncFetch()` during shutdown**: Silently discards the request if `isStopping()` is already true — callers during shutdown get no callback.

**`runningThreads_` vs `readThreads_`**: Workers increment `runningThreads_` on wake and decrement before waiting, so `getCountsJson()` can distinguish actively-processing threads from threads blocked on I/O. `readThreads_` is decremented on thread exit; reaching zero confirms all threads fully exited.

**Diagnostics**: `getCountsJson()` surfaces queue depth, thread counts, `rq_bundle`, write/read counts, bytes, hit counts, and total read duration (µs) for the `get_counts` RPC.

## BatchWriter (`include/xrpl/nodestore/detail/BatchWriter.h`)

Coalesces individual writes into batches for backends that benefit (RocksDB uses it; NuDB does not — NuDB's `do_insert` is synchronous).

- Privately inherits `Task`; the backend inherits `BatchWriter::Callback` and provides `writeBatch(Batch const&)`. Same object plays both roles, no extra allocation.
- **Double-buffer swap**: `store()` pushes into `mWriteSet`. `writeBatch()` holds the lock only long enough to swap `mWriteSet` with a fresh local vector, then releases before doing I/O. New stores accumulate concurrently with the flush. After each flush the loop re-checks the buffer before clearing `mWritePending` so no items are dropped.
- **`std::recursive_mutex` + `std::condition_variable_any`**: A synchronous scheduler (e.g., `DummyScheduler`) calls `performScheduledTask()` inline on the producer thread, which re-enters `writeBatch` on the same thread — plain `std::mutex` would deadlock.
- **Backpressure**: `store()` blocks on `mWriteCondition` when `mWriteSet.size() >= batchWriteLimitSize` (65536). Peak memory ≈ 2× the limit.
- **`mWritePending` flag**: Ensures only one scheduler task outstanding. First `store()` that finds flag clear raises it and calls `scheduleTask()`.
- **`getWriteLoad()`**: Returns `max(mWriteLoad, mWriteSet.size())` — conservative estimate reflecting both in-flight write count and pending accumulation count.
- **Destructor** calls `waitForWriting()` — no data is abandoned on backend teardown.
- **Telemetry**: After each flush, records count + wall-clock duration in `BatchWriteReport` and passes to `m_scheduler.onBatchWrite()`.

## Scheduler

Pure abstract (`include/xrpl/nodestore/Scheduler.h`): `scheduleTask(Task&)`, `onFetch(FetchReport)`, `onBatchWrite(BatchWriteReport)`. Contract: scheduler may invoke task on calling thread *or* a foreign thread — both are valid. Two implementations:

- **`DummyScheduler`**: `scheduleTask` runs `task.performScheduledTask()` synchronously on the calling thread; `onFetch`/`onBatchWrite` are no-ops. Used by every NodeStore test and by the bulk-import path in `Application.cpp`.
- **`NodeStoreScheduler`** (production, in `xrpld/app`): posts a `jtWRITE` job to the `JobQueue` (synchronous fallback if queue is stopped); routes `FetchReport` to `jtNS_SYNC_READ`/`jtNS_ASYNC_READ` load events.

`Task` (`Task.h`) is just a virtual `performScheduledTask()` + virtual destructor. `BatchWriter` inherits it privately so external code cannot treat it as a `Task`. The recursive mutex in `BatchWriter` is required precisely because `DummyScheduler` can call back synchronously.

`FetchReport` has a `const FetchType` member set at construction; `elapsed` is zero-initialized. `BatchWriteReport` carries elapsed + writeCount. Both are plain-old-data stack values passed by value.

## Rotating Backend / Online Deletion

`DatabaseRotatingImp` (`src/libxrpl/nodestore/DatabaseRotatingImp.cpp`) holds two `shared_ptr<Backend>`s: writable + archive. `SHAMapStoreImp` (in `xrpld/app`) drives the rotation policy; `DatabaseRotatingImp` only handles the atomic swap.

**`rotate(newBackend, callback)` sequence** (under `mutex_`):
1. `setDeletePath()` on the old archive, move it into a local `shared_ptr oldArchiveBackend`.
2. Promote `writableBackend_` → `archiveBackend_`.
3. Install `newBackend` → `writableBackend_`.

Then release the lock and invoke `callback(newWritableName, newArchiveName)`. The callback persists names to the SQL state DB. `oldArchiveBackend` falls out of scope *after* the callback returns — so the directory is deleted only after the state DB knows the new layout. Crash between swap and persist → recoverable from state DB on restart.

**Snapshot-and-release locking**: Every read/write copies the relevant `shared_ptr` under `mutex_`, releases the lock, then does I/O via the local. Locking across disk I/O would serialize all readers. Exception: `sync()` holds the lock for the full call (maintenance path, not latency-sensitive).

**Fetch fallthrough + promotion**: `fetchNodeObject` tries writable, then archive. If found in archive and `duplicate=true`, re-snapshots `writableBackend_` (to handle a rotation racing with the archive read) and writes the object into the *current* writable. Objects not promoted before the next rotation are gone forever — promotion is the migration mechanism.

**`newWritableBackendName`** is captured *before* the lock (calling `getName()` on the new backend); `newArchiveBackendName` is captured *inside* the lock from the demoted former writable. Both are passed to the callback.

## Manager / Factory Registration

`ManagerImp` is a Meyers singleton (`static ManagerImp _` in `instance()`). Its constructor calls four free functions (`registerNuDBFactory`, `registerRocksDBFactory`, `registerNullFactory`, `registerMemoryFactory`), each of which creates a function-local `static` factory whose constructor calls `Manager::insert(*this)`.

**Why this pattern**: Factories are never globals. If a global `Factory` destructor called `Manager::instance().erase()` after `ManagerImp` had been destroyed, the result would be UB (no order guarantee across translation units). Function-local statics initialize after `ManagerImp` and destroy before it — safe.

- `Manager::find()` is case-insensitive (`boost::iequals`) so `"NuDB"`, `"nudb"`, `"NUDB"` all match.
- `make_Backend()` throws `std::runtime_error` with operator-facing message on missing or unknown `type` key. Same `missing_backend()` helper covers both the absent-key and unrecognized-name paths.
- `make_Database()` = `make_Backend()` + `backend->open()` + wrap in `DatabaseNodeImp`. The explicit `open()` separation lets I/O errors surface before the full `Database` stack is built.
- Registry mutex protects `list_` (a `vector<Factory*>` of non-owning pointers). `erase()` uses `XRPL_ASSERT` — removing an unknown factory is a programming error.
- `Factory::createInstance` second overload accepts `nudb::context&` for shared I/O threads across NuDB shards. Non-NuDB backends inherit a default that returns `{}` (null), prompting the caller to fall back to the simpler overload.

## Backend-Specific Notes

**NuDB** (`backend/NuDBFactory.cpp`): Three on-disk files (`nudb.dat`, `nudb.key`, `nudb.log`); `fdRequired()` = 3. `appnum = 1` embedded in the header, sanity-checked on every open. `nudb_block_size` config key must be a power of 2 between 4096 and 32768; defaults to `nudb::block_size()` (filesystem-native). `for_each()` and `verify()` *close and reopen* the database — incompatible with concurrent access. `fetch()` uses a zero-copy callback into NuDB's internal buffer; decompression must happen inside the callback (buffer only valid during callback). `db_.insert()` returning `nudb::error::key_exists` is silently ignored (content-addressed: same hash → same data). `burstSize` set via `db_.set_burst()` after open — important performance parameter. Destructor catches `nudb::system_error` from `close()` because destructors must not propagate exceptions.

**RocksDB** (`backend/RocksDBFactory.cpp`): `RocksDBEnv` overrides `StartThread` to name threads `"rocksdb #N"` (using an atomic counter) for profiler visibility. `hard_set` config flag: when false (default), small `cache_mb`/`open_files` values are silently escalated to production-appropriate defaults (1024 MB cache, 8000 FDs). Implements both `Backend` and `BatchWriter::Callback`; uses `BatchWriter` for writes. `storeBatch` is atomic (single `WriteBatch` in WAL); `fetchBatch` is a serial loop with no atomicity. `sync()` is empty — WAL provides durability. Key passed to RocksDB via `std::bit_cast<char const*>` over the `uint256` — no copy. Raw option strings accepted via `bbt_options` and `options` config keys; throw on parse failure.

**Memory** (`backend/MemoryFactory.cpp`): `MemoryFactory` owns named `MemoryDB` instances in a case-insensitive (`boost::beast::iless`) map; multiple `MemoryBackend`s opened with the same path share the same `MemoryDB`. Survives backend close/reopen within a process. The `db.open` guard in `MemoryFactory::open()` is dead code (`open` is never set to `true`). `for_each` reads without holding the mutex — caller must ensure no concurrent writes. Module-level raw pointer `memoryFactory` allows `MemoryBackend::open()` to call back without holding a reference.

**Null** (`backend/NullFactory.cpp`): All operations no-op; `fetch` returns `notFound`. Exists so `type=none` is a valid config value. Doubles as a minimal reference implementation of `Backend`. `isOpen()` always returns `false`.

## Common Bug Patterns

- **Forgetting `stop()` in derived `Database` destructor**: Symptom is a crash during teardown in a worker thread invoking a destroyed vtable. The base `~Database()` calls `stop()` as a fallback but the derived vtable is already gone by then.
- **Holding `DatabaseRotatingImp::mutex_` across I/O**: Will serialize all readers. Always snapshot the `shared_ptr` under lock, release, then I/O.
- **Forgetting `duplicate=true` when archive fallback matters**: Objects fetched from archive are not promoted to writable; next rotation discards them silently.
- **Treating `hotDUMMY` as a real object**: Cache lookups must check the type before dereferencing.
- **Exceeding `batchWriteLimitSize`**: `BatchWriter::store()` blocks the caller; not a silent truncation, but unexpected backpressure can cause deadlock if the same thread is needed to drain.
- **Inaccurate `fdRequired()`**: Causes silent backend failures when the process file descriptor limit is exceeded. Aggregated across all backends by the base `Database`.
- **Changing `EncodedBlob` without `DecodedBlob`**: Breaks on-disk read compatibility silently — both classes define the format jointly.
- **Calling `for_each` / `verify` on NuDB concurrently with reads**: Both close and reopen the database; concurrent access is undefined.
- **Lossy inner-node round-trip**: `nodeobject_compress` zeros `index`/`unused`/`kind` on reconstruction. Code that verifies blobs after compress→decompress must call `filter_inner()` first.
- **Not calling `backend->open()` before wrapping in `DatabaseNodeImp`**: `make_Database()` does this correctly but manual construction may miss it; errors surface much later in I/O paths.
- **Using `MemoryBackend::for_each` concurrently**: No lock held; caller must guarantee no concurrent writes (implicit contract, not enforced).
- **`fetchBatch` atomicity assumption on RocksDB**: `storeBatch` is atomic (one `WriteBatch`); `fetchBatch` is a serial loop — no group atomicity on reads.

## Review Checklist

- Config: `[node_db]` has valid `type`, `path`, and (for NuDB) `nudb_block_size` if present.
- Online deletion: `SHAMapStoreImp` coordinates rotation with application lifecycle; rotation callback must persist new names before old archive can be deleted.
- New backend implementations: full `Backend` interface including `fdRequired()`, both `open()` overloads (default-throws is OK for non-NuDB), accurate concurrency guarantees on `fetch`/`store`, no-op `verify()` if not implementing.
- Derived `Database` subclasses: `stop()` in destructor; override `fetchNodeObject(hash, seq, FetchReport&, duplicate)` not the public non-virtual.
- Any change to on-disk format: update both `EncodedBlob` and `DecodedBlob`; consider backward-compat type tag (codec.h type 0 path is the precedent).
- Inner-node codec changes: update `filter_inner()` alongside compressor/decompressor; verify round-trip with zeroed metadata fields.
- New varint usages: confirm base-127 (not base-128) arithmetic; use `varint_traits<T>::max` for stack buffer sizing.

## Key Files

### Public interfaces
- `include/xrpl/nodestore/NodeObject.h` — immutable object, factory-only construction, `CountedObject` live-count
- `include/xrpl/nodestore/Backend.h` — pluggable storage interface; concurrency contracts annotated inline
- `include/xrpl/nodestore/Database.h` — async read pool, instrumented fetch, Template Method pattern
- `include/xrpl/nodestore/DatabaseRotating.h` — adds `rotate()`
- `include/xrpl/nodestore/Manager.h` — singleton factory registry
- `include/xrpl/nodestore/Factory.h` — abstract factory; two `createInstance` overloads (plain + nudb::context)
- `include/xrpl/nodestore/Scheduler.h` / `Task.h` — async dispatch + telemetry
- `include/xrpl/nodestore/DummyScheduler.h` — synchronous, for tests + import
- `include/xrpl/nodestore/Types.h` — `Status`, `Batch`, batch size constants

### Implementations (detail/)
- `include/xrpl/nodestore/detail/DatabaseNodeImp.h` — single-backend; `isSameDB` always true; ledgerSeq ignored
- `include/xrpl/nodestore/detail/DatabaseRotatingImp.h` — rotation + promotion; snapshot-and-release locking
- `include/xrpl/nodestore/detail/ManagerImp.h` — singleton + registry; `missing_backend()` static helper
- `include/xrpl/nodestore/detail/BatchWriter.h` — write coalescing + backpressure; recursive mutex
- `include/xrpl/nodestore/detail/EncodedBlob.h` — serializer; 1033-byte stack buffer, heap fallback
- `include/xrpl/nodestore/detail/DecodedBlob.h` — parser; non-owning view, `wasOk()` flag
- `include/xrpl/nodestore/detail/codec.h` — LZ4 + inner-node compression; BufferFactory pattern
- `include/xrpl/nodestore/detail/varint.h` — base-127 varint; function templates for ODR safety

### Source (libxrpl/nodestore/)
- `src/libxrpl/nodestore/Database.cpp` — read pool, hash coalescing, shutdown, `importInternal()`
- `src/libxrpl/nodestore/DatabaseNodeImp.cpp` — simple backend wrapper; `fetchBatch` resize guard
- `src/libxrpl/nodestore/DatabaseRotatingImp.cpp` — rotation, promotion, snapshot locking
- `src/libxrpl/nodestore/BatchWriter.cpp` — double-buffer swap, backpressure, load estimation
- `src/libxrpl/nodestore/ManagerImp.cpp` — singleton init + factory registration
- `src/libxrpl/nodestore/DecodedBlob.cpp` — format parsing; legacy 8-byte prefix handling
- `src/libxrpl/nodestore/DummyScheduler.cpp` — synchronous no-op scheduler
- `src/libxrpl/nodestore/NodeObject.cpp` — `PrivateAccess` factory; `CountedObject` wiring
- `src/libxrpl/nodestore/backend/NuDBFactory.cpp` — production default; compression pipeline
- `src/libxrpl/nodestore/backend/RocksDBFactory.cpp` — alternate production; `RocksDBEnv` thread naming
- `src/libxrpl/nodestore/backend/MemoryFactory.cpp` — test/ephemeral; shared `MemoryDB` by path
- `src/libxrpl/nodestore/backend/NullFactory.cpp` — `type=none`; reference `Backend` skeleton

### Lifecycle orchestration
- `src/xrpld/app/misc/SHAMapStoreImp.cpp` — drives `rotate()`, manages state DB, enforces min rotation interval (256 ledgers networked / 8 standalone)
