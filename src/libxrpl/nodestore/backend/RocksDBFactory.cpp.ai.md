# `RocksDBFactory.cpp` — RocksDB Backend for the XRPL NodeStore

## Role in the System

The XRPL NodeStore is a pluggable key-value storage layer that holds serialized ledger objects (transactions, account states, ledger headers) keyed by their 256-bit hash. `RocksDBFactory.cpp` provides one concrete storage backend — RocksDB — beneath that abstraction. The entire file is guarded by `#if XRPL_ROCKSDB_AVAILABLE`, making RocksDB an optional dependency that can be compiled out entirely.

Three classes collaborate to deliver this backend: `RocksDBEnv` integrates RocksDB's threading into XRPL's naming conventions, `RocksDBBackend` implements the full `Backend` contract, and `RocksDBFactory` advertises the backend to the NodeStore's plugin registry.

## `RocksDBEnv` — Thread Naming Shim

`RocksDBEnv` derives from `rocksdb::EnvWrapper`, which is RocksDB's mechanism for intercepting OS-level operations. The only method it overrides is `StartThread`. When RocksDB creates an internal thread (compaction workers, flush threads, etc.), the overridden `StartThread` wraps the original function pointer and argument in a heap-allocated `ThreadParams` struct, then invokes `thread_entry` instead. Inside `thread_entry`, the struct is deleted, and a monotonically incrementing `std::atomic<std::size_t>` counter assigns each thread a unique name in the form `"rocksdb #N"` via `beast::setCurrentThreadName`. This makes RocksDB's background threads visible and identifiable in profilers and crash dumps — a purely operational concern, not a correctness one.

## `RocksDBBackend` — Storage Implementation

`RocksDBBackend` implements both `Backend` (the NodeStore interface) and `BatchWriter::Callback` (the async-write protocol). The dual inheritance is deliberate: the backend exposes `writeBatch()` to `BatchWriter` while hiding it from the broader `Backend` consumers. The concrete `writeBatch()` just calls `storeBatch()`, making the async path a thin wrapper around the same synchronous write logic.

### Configuration

The constructor accepts a `Section` of key-value config pairs and translates them into `rocksdb::Options` and `rocksdb::BlockBasedTableOptions`. Several settings exhibit a "legacy default escalation" pattern controlled by the `hard_set` flag. For example, a configured `cache_mb = 256` is silently promoted to 1024 MB, and `open_files = 2000` becomes 8000, unless the config also sets `hard_set = true`. This reflects accumulated operational knowledge that the original documented defaults were too conservative for production; `hard_set` lets operators freeze the values at their literal specified amounts when they truly mean them.

When `open_files` is set, `fdRequired_` is calculated as `max_open_files + 128` to give the process enough headroom in its file-descriptor table. The `fdRequired()` method exposes this count upward so the process can pre-check system limits before opening databases.

Two escape hatches accept raw RocksDB option strings: `bbt_options` feeds `rocksdb::GetBlockBasedTableOptionsFromString`, and `options` feeds `rocksdb::GetOptionsFromString`. Both call `Throw<std::runtime_error>` on parse failure. After the options object is fully assembled, the constructor logs the resolved `DBOptions` and `ColumnFamilyOptions` at debug level — useful for verifying that escalated defaults or raw string overrides took effect as intended.

### Open/Close Lifecycle

`open()` asserts the database is not already open (`UNREACHABLE` guard with `LCOV_EXCL_START/STOP` to exclude from coverage), sets `create_if_missing` from the caller's flag, and calls `rocksdb::DB::Open`, which returns a raw `rocksdb::DB*`. The pointer is immediately adopted into `m_db` (a `std::unique_ptr<rocksdb::DB>`). If the status indicates failure or the pointer is null, an exception is thrown. The destructor calls `close()`, which resets `m_db` (triggering RocksDB's own cleanup), then conditionally removes the database directory if `m_deletePath` was set. The flag is an `std::atomic<bool>` because `setDeletePath()` is part of the public `Backend` interface and may be called from threads different from the one that destroys the object.

### Write Path

Individual writes go through `store()` → `m_batch.store()`. `BatchWriter` accumulates objects and schedules a `Task` on the node's `Scheduler`. When the scheduler fires the task, `BatchWriter` calls back into `writeBatch()`, which calls `storeBatch()`. That method encodes each `NodeObject` via `EncodedBlob`, packs all encoded key/value pairs into a single `rocksdb::WriteBatch`, and commits with `m_db->Write()`. Writing in a single `WriteBatch` is atomically durable in RocksDB's WAL — either all objects in the group are recoverable or none are. Failure throws a `std::runtime_error`.

The `sync()` method is deliberately empty. RocksDB's write-ahead log provides crash durability automatically; there is no need for an explicit fsync barrier at the NodeStore level.

### Read Path

`fetch()` constructs a `rocksdb::Slice` directly over the `uint256` hash bytes via `std::bit_cast<char const*>`, avoiding any copy of the key. The returned value string is passed to `DecodedBlob`, which reconstructs the `NodeObject`. Three failure modes are distinguished: `dataCorrupt` for both a corrupt RocksDB status and a `DecodedBlob` that fails to parse, `notFound` for a clean miss, and a catch-all `customCode + status.code()` for other RocksDB errors — each mapped to the `Status` enum so callers can decide whether to fall through to another cache tier or surface the error.

`fetchBatch()` is not atomic — it serially calls `fetch()` for each hash and inserts a null `shared_ptr` for any miss or error, always returning an overall status of `ok`. This contrasts with `storeBatch()`, where a single `WriteBatch` provides atomic group semantics. The asymmetry is intentional: reads are inherently independent, and RocksDB offers no multi-get API that would change the semantics meaningfully here.

### Iteration

`for_each()` creates a plain `rocksdb::Iterator` without snapshot pinning. Per the `Backend` contract, it is only called during database import and never concurrently with other operations. Entries with unexpected key sizes are logged at fatal level (a defensive guard left from early development) rather than throwing, allowing the iterator to continue past potential corruption.

## `RocksDBFactory` — Plugin Registration

`RocksDBFactory` holds a single `RocksDBEnv` instance, shared across all backends it creates. Its constructor calls `manager_.insert(*this)` to register with the NodeStore's `Manager` registry. The public entry point is the free function `registerRocksDBFactory(Manager&)`, which uses a `static` local variable to guarantee singleton initialization and exactly one registration per process. `createInstance()` ignores the `burstSize` parameter (second-to-last argument unnamed), as RocksDB manages its own internal buffering independently of any externally imposed burst limit.