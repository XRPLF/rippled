# SQL Database

SQLite via SOCI for ledger/transaction history. Only SQLite is supported; the backend name is validated and any non-`sqlite` value throws at config parse time.

## Key Invariants

- Two main databases: `lgrdb_` (ledger) and `txdb_` (transactions, optional via `useTxTables` config)
- Transaction tables are optional; disabling them means no transaction history or account_tx queries
- WAL checkpointing offloads to `JobQueue` (jtWAL); at most one checkpoint job in flight per `DatabaseCon` (guarded by `running_` mutex)
- Database init failure is fatal (throws exception, prevents construction)
- Free disk space < 512MB triggers fatal error on write operations
- File extension inconsistency: `validators` and `peerfinder` use `.sqlite`; all other DBs use `.db`. This is historical and enforced in `detail::getSociInit`

## Schema

- `Ledgers` table: seq, hash, parent hash, total coins, close time, etc. Indexed by `LedgerSeq`
- `Transactions` table: TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn, TxnMeta. Indexed by `LedgerSeq`
- `AccountTransactions` table: TransID, Account, LedgerSeq, TxnSeq. Triple-indexed for account_tx queries
- Secondary DBs: Wallet (node identity, manifests), PeerFinder (bootstrap cache), State (deletion tracking)

## Common Bug Patterns

- No schema migration system; `CREATE TABLE IF NOT EXISTS` means old schemas silently persist with missing columns
- PeerFinder DB is the exception — it has schema versioning via `SchemaVersion` table
- `safety_level` config affects journal_mode and synchronous; "low" can lose data on crash
- `page_size` must be power of 2 between 512-65536; invalid values cause init failure
- Online deletion coordinates between NodeStore rotation and SQL table pruning; race conditions here lose history
- Empty database name passed to `detail::getSociSqliteInit` throws — silent fallback paths are not provided
- A `WALCheckpointer` registered with `sqlite3_wal_hook` outlives its `DatabaseCon` if a checkpoint job is in flight; teardown must wait for the job to drain (see Lifecycle below)

## Configuration

| Option | Section | Values | Default |
|--------|---------|--------|---------|
| `backend` | `[sqdb]` / `[relational_db]` | `sqlite` only | sqlite |
| `page_size` | `[sqlite]` | 512-65536, power of 2 | 4096 |
| `safety_level` | `[sqlite]` | high, medium, low | high |
| `journal_size_limit` | `[sqlite]` | integer >= 0 | 1582080 |

## WAL Checkpointer Lifecycle

The checkpointer subsystem is the trickiest part of this module. SQLite's WAL hook is a C callback registered on the native `sqlite3*` connection, but the work runs on a `JobQueue` thread that may still be executing when the owning `DatabaseCon` is destroyed.

### ID-based hook indirection
- `WALCheckpointer` (in `SociDB.cpp`) is registered with `sqlite3_wal_hook` using a `std::uintptr_t id_` cast to `void*`, **not** a raw `this` pointer.
- The C hook calls `checkpointerFromId()` which looks up the ID in a process-wide `CheckpointersCollection` (in `DatabaseCon.cpp`). If the lookup returns null, the hook deregisters itself via `sqlite3_wal_hook(conn, nullptr, nullptr)`.
- This protects against the hook firing on a writer thread between the `DatabaseCon` being torn down and the hook being unwired.

### Session ownership split
- `DatabaseCon` holds `std::shared_ptr<soci::session>`.
- `WALCheckpointer` holds only `std::weak_ptr<soci::session>`. Intentional: if the checkpointer held a `shared_ptr`, an in-flight job would keep the WAL lock alive and a freshly-opened replacement `DatabaseCon` would fail to acquire it.
- `WALCheckpointer::checkpoint()` calls `session_.lock()` and bails silently if expired.

### Destructor wait
`DatabaseCon::~DatabaseCon` sequence (order matters):
1. `checkpointers.erase(checkpointer_->id())` — future hook invocations now no-op.
2. Take a `weak_ptr` to the checkpointer, then `checkpointer_.reset()`.
3. Busy-poll `wk.use_count() != 0` with 100 ms sleeps until all in-flight job lambdas release their `shared_ptr<Checkpointer>`.

The 100 ms poll is deliberate (rare event, simpler than a condvar). Without this wait, reopening the same SQLite file immediately after destruction can fail because the old checkpoint job still holds the WAL lock.

### Checkpoint job behavior
- Triggered by `sqlite3_wal_hook` after every WAL write; module-level `checkpointPageCount = 1000` mirrors SQLite's auto-checkpoint threshold.
- `schedule()` uses a `running_` bool under a mutex to ensure single in-flight job; if `JobQueue` rejects the job, `running_` is reset.
- The enqueued lambda captures `std::weak_ptr<Checkpointer>` so a destroyed `DatabaseCon` causes the job to exit without touching the session.
- `checkpoint()` calls `sqlite3_wal_checkpoint_v2` with `SQLITE_CHECKPOINT_PASSIVE`. `SQLITE_LOCKED` is logged at trace (expected under reader contention); other errors are warnings.
- Net effect: routes checkpoint work off the writer thread onto `jtWAL`. SQLite would otherwise do this synchronously on whichever thread crossed the page threshold.

### setupCheckpointing
- Separated from `DatabaseCon` constructors so checkpointing is opt-in.
- Constructors taking a `CheckpointerSetup` open the DB first, then call `setupCheckpointing(JobQueue*, ServiceRegistry&)`.
- Null `JobQueue*` throws `std::logic_error` (programming error, not runtime).
- The checkpointer must be inserted into `CheckpointersCollection` **before** returning from setup, because the WAL hook is armed inside the `WALCheckpointer` constructor and writes can fire it immediately.

## SOCI Adapter Notes

- `getConnection(session&)` (`SociDB.cpp`) recovers the raw `sqlite3*` via `dynamic_cast<soci::sqlite3_session_backend*>`. This is the only intentional break in the SOCI abstraction; needed for WAL hooks and `sqlite3_db_status`.
- `getKBUsedAll()` → `sqlite3_memory_used()` (process-global). `getKBUsedDB()` → `SQLITE_DBSTATUS_CACHE_USED` (per-connection).
- Four `convert()` overloads bridge `soci::blob` and `std::vector<uint8_t>` / `std::string`. Empty blobs require `blob.trim(0)` rather than `blob.write(nullptr, 0)`.
- `SociDB.cpp` opens with `#pragma clang diagnostic ignored "-Wdeprecated"` because SOCI headers use deprecated constructs; scoped to this TU only.
- `DBConfig` is two-phase: parse params, open later. `detail::getSociInit` and `detail::getSociSqliteInit` resolve backend + path; the `.sqlite` vs `.db` extension fork lives in `getSociInit`.

## Key Patterns

### Schema Evolution Caveat
```cpp
// WARNING: no migration system — old databases keep old schemas
// CREATE TABLE IF NOT EXISTS silently skips if table exists with old columns
// New columns on existing tables require manual ALTER TABLE or
// documentation that the column is optional and may be absent
```

### Disk Space Guard
```cpp
// REQUIRED on write paths: < 512MB triggers fatal to prevent corruption
if (freeDiskSpace < minDiskFree)
    Throw<std::runtime_error>("Not enough disk space for database write");
```

### WAL Hook Cookie
```cpp
// Always pass an integer ID, never `this`. The DatabaseCon may be
// destroyed while a hook invocation is mid-flight on a writer thread.
sqlite3_wal_hook(conn, &walHookCallback,
                 reinterpret_cast<void*>(checkpointer->id()));
```

## Key Files

- `src/xrpld/app/rdb/backend/detail/SQLiteDatabase.cpp` — main implementation
- `src/xrpld/app/main/DBInit.h` — schema definitions
- `src/xrpld/core/detail/DatabaseCon.cpp` — kept for historical reference; lifecycle now in `libxrpl`
- `src/libxrpl/rdb/DatabaseCon.cpp` — connection lifecycle, `CheckpointersCollection`, destructor drain
- `src/libxrpl/rdb/SociDB.cpp` — SOCI/SQLite adapter, `WALCheckpointer`, blob conversion, memory stats
- `src/xrpld/app/rdb/backend/detail/Node.cpp` — ledger/tx operations
- `src/xrpld/app/rdb/detail/State.cpp` — deletion state tracking
