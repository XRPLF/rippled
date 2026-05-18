# NuDBFactory.cpp — NuDB Storage Backend for the XRPL NodeStore

This file provides the concrete NuDB storage backend for the XRPL NodeStore subsystem. The NodeStore is the layer beneath the ledger that persists all `NodeObject` instances — SHAMap nodes, transactions, ledger headers — as an immutable content-addressed key-value store. `NuDBFactory.cpp` defines two classes: `NuDBBackend`, which wraps a NuDB database and implements the `Backend` interface, and `NuDBFactory`, which instantiates backends and registers them with the global `Manager`.

## Why NuDB?

NuDB is a hash-based, append-mostly key-value store designed specifically for workloads where keys are cryptographic hashes and values are small to medium-sized blobs. It keeps a separate data file (`nudb.dat`), a key-index file (`nudb.key`), and a write-ahead log (`nudb.log`). This layout matches the XRPL access pattern almost exactly: random-read-heavy, write-append-only, no deletes. Compared to RocksDB, NuDB trades away compaction, range iteration, and general-purpose flexibility in exchange for simpler I/O behavior and more predictable write amplification — which is why it remains the default backend for `rippled`.

## Registration and Lifetime

The `registerNuDBFactory()` free function creates a `static NuDBFactory` instance and passes a reference to it into the global `Manager` via `Manager::insert()`. The static local ensures registration happens exactly once regardless of how many translation units call it, and the factory outlives any backend instance it creates. This one-line idiom — `static NuDBFactory const instance{manager}` — is the standard pattern used by all backends in this directory.

`NuDBFactory::createInstance()` has two overloads: one that constructs a `NuDBBackend` without a `nudb::context`, and one that accepts an existing `nudb::context`. A `nudb::context` owns background I/O threads that NuDB uses for asynchronous buffering; providing one enables shared I/O across multiple backends (relevant when multiple shards are open simultaneously).

## Database Lifecycle

`NuDBBackend` construction is lightweight — it parses configuration from a `Section` key-value map and validates the required `path` field, but does not touch the filesystem until `open()` is called. This two-phase initialization lets the caller catch I/O exceptions after construction.

The full `open(bool createIfMissing, uint64_t appType, uint64_t uid, uint64_t salt)` overload supports deterministic database creation: the uid and salt are embedded in the NuDB file headers and must be reproduced consistently when reopening a database. The simplified `open(bool)` overload generates random uid/salt via `nudb::make_uid()` and `nudb::make_salt()`, which is appropriate for the main node store where only one instance is ever created for a given path. When `createIfMissing` is true, `nudb::create<nudb::xxhasher>()` initializes the three files; if they already exist (`nudb::errc::file_exists`), the error is silently cleared and `db_.open()` proceeds normally.

The `appnum` constant (`1`) is stored in the NuDB header at creation time and checked on every `open()`. Its only purpose now is a sanity check that the files were written by xrpld; historical shard-database differentiation code has been removed. After opening, `db_.set_burst(burstSize_)` configures NuDB's in-memory write buffer, which is a critical performance parameter: it determines how many bytes NuDB will accumulate before flushing to disk.

`close()` logs at `fatal` level and throws on NuDB errors rather than silently swallowing them — a closed-with-error database is a serious condition. If `deletePath_` was set (via `setDeletePath()`, called for temporary databases), `boost::filesystem::remove_all()` deletes the entire database directory after the close succeeds. The `std::atomic<bool>` for `deletePath_` avoids a data race if the flag is set from a different thread than the one closing the backend. The destructor catches `nudb::system_error` from `close()` because destructors must not propagate exceptions; the error is already logged as `fatal` before being swallowed.

## Compression Pipeline

Every value passing through this backend is compressed before storage and decompressed on retrieval. The codec layer (`detail/codec.h`) uses LZ4 as the default compression algorithm, but has special-case handling for SHAMap inner nodes. When the input blob is exactly 525 bytes with a matching `HashPrefix::innerNode` prefix, `nodeobject_compress()` recognizes it as a SHAMap inner node and applies a sparse-hash encoding (type 2 or type 3) rather than LZ4: only non-zero 32-byte child hashes are stored along with a 16-bit presence bitmask, which is far more compact for partially-filled nodes.

The full write path for a single object is: `store()` → `do_insert()` → `EncodedBlob(no)` (serializes `NodeObject` to raw bytes) → `nodeobject_compress()` (LZ4 or inner-node encoding) → `db_.insert()`. The read path inverts this: `fetch()` calls `db_.fetch()` with a callback lambda that receives a raw pointer into NuDB's internal buffer (no copy), decompresses inline with `nodeobject_decompress()`, then `DecodedBlob::createObject()` reconstructs the `NodeObject`. This zero-copy callback pattern is central to NuDB's design — the buffer is only valid within the callback, so decompression must happen there.

Duplicate inserts are silently ignored: when `db_.insert()` returns `nudb::error::key_exists`, `do_insert()` discards the error. This is correct because the NodeStore is content-addressed — the same hash always maps to the same data.

## Iteration and Verification

`for_each()` and `verify()` share an unusual requirement: both must close the live database before operating and reopen it afterward. NuDB's `nudb::visit()` (used by `for_each`) reads the data file sequentially — a pattern incompatible with normal concurrent access through `nudb::store`. Similarly, `nudb::verify<nudb::xxhasher>()` performs a consistency check by independently re-hashing every key and confirming it matches the stored key-file index. The `xxhasher` template parameter must match the one used at database creation time; the NuDB key file is organized around this hash function. `Backend.h` notes that `verify()` is not currently called at startup, though it would be valuable to do so.

`fetchBatch()` is a sequential loop over individual `fetch()` calls — NuDB provides no native batch-read operation, so this offers no I/O parallelism. Missing or corrupt entries produce empty slots (`{}`) in the result vector rather than aborting the entire batch.

## Configuration and Resource Accounting

`parseBlockSize()` reads an optional `nudb_block_size` configuration key and validates that it is a power of 2 between 4096 and 32768 bytes. This value governs the page size of the NuDB key file; misaligned block sizes cause I/O amplification. The default comes from `nudb::block_size()`, which queries the filesystem for the native block size — typically 4096 bytes. `getBlockSize()` exposes this to higher-level callers so they can make storage-layout decisions.

`fdRequired()` returns 3, directly reflecting the three physical files NuDB keeps open: data, key, and log. This lets the `NodeStore::Manager` pre-check that the process has enough file descriptors before attempting to open the database. `getWriteLoad()` returns 0 because NuDB's writes go through `do_insert()` synchronously — there is no internal write queue to measure, unlike the `BatchWriter` pattern used by RocksDB.