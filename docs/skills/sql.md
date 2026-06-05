# SQL Database

SQLite via SOCI for ledger/transaction history. Only SQLite is supported; any non-`sqlite` backend value in config throws at parse time (`detail::getSociInit` in `SociDB.cpp`).

## Key Invariants

- Two main databases: `lgrdb_` (ledger) and `txdb_` (transactions, optional via `useTxTables` config)
- Transaction tables are optional; disabling them disables transaction history and `account_tx` queries
- WAL checkpointing offloads to `JobQueue` (`jtWAL`); at most one checkpoint job in flight per `DatabaseCon` (guarded by `running_` mutex in `WALCheckpointer`)
- Database init failure is fatal (throws exception, prevents construction)
- Free disk space < 512 MB triggers fatal error on write operations
- File extension inconsistency: `validators` and `peerfinder` use `.sqlite`; all other DBs use `.db`. Historical artifact enforced in `detail::getSociInit`

## Schema

- `Ledgers`: seq, hash, parent hash, total coins, close time, etc. Indexed by `LedgerSeq`
- `Transactions`: TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn, TxnMeta. Indexed by `LedgerSeq`
- `AccountTransactions`: TransID, Account, LedgerSeq, TxnSeq. Triple-indexed for `account_tx` queries
- Secondary DBs: Wallet (node identity, manifests), PeerFinder (bootstrap cache), State (deletion tracking)
- Schema defined in `src/xrpld/app/main/DBInit.h`
- No schema migration system; `CREATE TABLE IF NOT EXISTS` silently preserves old schemas with missing columns. **Exception**: PeerFinder has schema versioning via a `SchemaVersion` table.

## Configuration

| Option | Section | Values | Default |
|--------|---------|--------|---------|
| `backend` | `[sqdb]` / `[relational_db]` | `sqlite` only | sqlite |
| `page_size` | `[sqlite]` | 512–65536, power of 2 | 4096 |
| `safety_level` | `[sqlite]` | high, medium, low | high |
| `journal_size_limit` | `[sqlite]` | integer >= 0 | 1582080 |

`safety_level: low` changes `journal_mode` and `synchronous` settings — can lose data on crash.

## WAL Checkpointing Architecture

The checkpointer subsystem is the trickiest part of this module. SQLite's WAL hook is a C callback registered on the native `sqlite3*` connection, but the work runs on a `JobQueue` thread that may still be executing when the owning `DatabaseCon` is destroyed.

### Two-file split

- **`SociDB.cpp`**: `WALCheckpointer` class (anonymous namespace) — installs the hook, implements `schedule()` and `checkpoint()`, holds the `weak_ptr<soci::session>`.
- **`DatabaseCon.cpp`**: `CheckpointersCollection` class — process-wide singleton registry (`checkpointers`, namespace-scope variable) mapping monotonically-incrementing integer IDs to `shared_ptr<Checkpointer>`; exposes `create`, `fromId`, `erase`. All `DatabaseCon` instances share this one registry.

`DatabaseCon.cpp` has no direct SQLite dependency; it only manages the `Checkpointer` abstract interface.

### ID-based hook indirection

- `WALCheckpointer` is registered with `sqlite3_wal_hook` using its `std::uintptr_t id_` cast to `void*`, **not** a raw `this` pointer.
- The C hook calls `checkpointerFromId()` → `CheckpointersCollection::fromId()` (process-wide singleton). If lookup returns null (connection torn down), the hook deregisters itself via `sqlite3_wal_hook(conn, nullptr, nullptr)`.
- Prevents use-after-free: the hook may fire on a writer thread after `DatabaseCon` begins destruction.

### Session ownership split

- `DatabaseCon` holds `std::shared_ptr<soci::session>`; `WALCheckpointer` holds only `std::weak_ptr<soci::session>`.
- If the checkpointer held a `shared_ptr`, an in-flight job would keep the WAL lock alive, blocking a freshly-opened replacement `DatabaseCon` on the same file.
- `WALCheckpointer::checkpoint()` calls `session_.lock()` and bails silently if expired.

### Destructor sequence (`DatabaseCon::~DatabaseCon`)

Order matters:
1. `checkpointers.erase(checkpointer_->id())` — future hook invocations now no-op and self-deregister.
2. Take `weak_ptr<Checkpointer> wk(checkpointer_)`, then `checkpointer_.reset()`.
3. Busy-poll `wk.use_count() != 0` with 100 ms sleeps until all in-flight job lambdas release their `shared_ptr<Checkpointer>`.

The 100 ms poll is deliberate (rare event; simpler than a condvar). Without this wait, reopening the same SQLite file immediately after destruction can fail because the old checkpoint job may still hold the WAL lock.

### `setupCheckpointing()` — deferred wiring

- Separated from constructors so checkpointing is opt-in.
- Constructors accepting `CheckpointerSetup` open the DB first, then call `setupCheckpointing(JobQueue*, ServiceRegistry&)`.
- Null `JobQueue*` throws `std::logic_error` (programming error, not runtime).
- The checkpointer must be inserted into `CheckpointersCollection` **before** `setupCheckpointing` returns, because the WAL hook is armed inside the `WALCheckpointer` constructor and writes can fire it immediately.

### Checkpoint job behavior

- Triggered by `sqlite3_wal_hook` after every WAL write; `static checkpointPageCount = 1000` mirrors SQLite's auto-checkpoint threshold.
- `schedule()` uses `running_` bool under mutex to enforce single in-flight job; if `JobQueue` rejects the job, `running_` is reset.
- Enqueued lambda captures `std::weak_ptr<Checkpointer>`; destroyed `DatabaseCon` causes the job to exit without touching the session.
- `checkpoint()` calls `sqlite3_wal_checkpoint_v2` with `SQLITE_CHECKPOINT_PASSIVE`. `SQLITE_LOCKED` logged at trace (expected under reader contention); other errors logged as warnings. `running_` reset under mutex after each attempt.
- Net effect: routes checkpoint work off the writer thread onto `jtWAL`. Without this, SQLite does it synchronously on whichever thread crosses the page threshold.

## SOCI Adapter Notes (`SociDB.cpp`)

- `DBConfig` is two-phase: parse params, open later. `detail::getSociInit` and `detail::getSociSqliteInit` resolve backend + path; the `.sqlite` vs `.db` extension fork lives in `getSociInit`. `getSociSqliteInit` throws `std::runtime_error` if the database name is empty.
- Two free-function `open()` overloads: config-based (delegates through `DBConfig`) and explicit-string (enforces same "sqlite only" constraint). Both paths call `s.open(soci::sqlite3, connectionString)`.
- `getConnection(session&)` recovers the raw `sqlite3*` via `dynamic_cast<soci::sqlite3_session_backend*>` — the only intentional break in the SOCI abstraction. Throws `std::logic_error` if the cast fails. Required for WAL hooks and `sqlite3_db_status`.
- `getKBUsedAll()` → `sqlite3_memory_used()` (process-global). `getKBUsedDB()` → `SQLITE_DBSTATUS_CACHE_USED` (per-connection).
- Four `convert()` overloads bridge `soci::blob` ↔ `std::vector<uint8_t>` / `std::string`. Empty blobs require `blob.trim(0)` rather than `blob.write(nullptr, 0)`.
- `SociDB.cpp` opens with `#pragma clang diagnostic ignored "-Wdeprecated"` because SOCI headers use deprecated constructs; scoped to this TU only.

## Common Bug Patterns

- No schema migration system; `CREATE TABLE IF NOT EXISTS` silently preserves old schemas with missing columns. New columns on existing deployments require manual `ALTER TABLE` or explicit documentation that the column may be absent.
- `page_size` must be power of 2 between 512–65536; invalid values cause init failure.
- Online deletion coordinates between NodeStore rotation and SQL table pruning; race conditions here lose history.
- Empty database name passed to `detail::getSociSqliteInit` throws — no silent fallback.
- A `WALCheckpointer` registered with `sqlite3_wal_hook` can outlive its `DatabaseCon` if a checkpoint job is in flight; teardown must wait for the job to drain (see Destructor sequence above).
- Opening a new `DatabaseCon` to the same file immediately after destroying the old one can fail if the destructor busy-poll is skipped or shortened — the old checkpoint job may still hold the WAL lock.

## Key Patterns

### Schema Evolution Caveat
```cpp
// No migration system — old databases keep old schemas.
// CREATE TABLE IF NOT EXISTS silently skips if table exists with old columns.
// New columns require manual ALTER TABLE or must be treated as optional/absent.
// PeerFinder is the exception: it has a SchemaVersion table.
```

### Disk Space Guard
```cpp
// Required on all write paths.
if (freeDiskSpace < minDiskFree)
    Throw<std::runtime_error>("Not enough disk space for database write");
```

### WAL Hook Cookie
```cpp
// Always pass an integer ID, never `this`.
// DatabaseCon may be destroyed while a hook invocation is mid-flight on a writer thread.
sqlite3_wal_hook(conn, &walHookCallback,
                 reinterpret_cast<void*>(checkpointer->id()));
```

### Penetrating the SOCI Abstraction
```cpp
// getConnection() is the only intentional SOCI abstraction break.
// Required for sqlite3_wal_hook and sqlite3_db_status APIs.
auto* be = dynamic_cast<soci::sqlite3_session_backend*>(s.get_backend());
if (!be || !be->conn_) throw std::logic_error("Not a sqlite3 session");
sqlite3* conn = be->conn_;
```

## Key Files

| File | Purpose |
|------|---------|
| `src/libxrpl/rdb/SociDB.cpp` | SOCI/SQLite adapter, `WALCheckpointer`, blob conversion, memory stats |
| `src/libxrpl/rdb/DatabaseCon.cpp` | Connection lifecycle, `CheckpointersCollection`, destructor drain |
| `src/xrpld/app/main/DBInit.h` | Schema definitions (CREATE TABLE statements) |
| `src/xrpld/app/rdb/backend/detail/SQLiteDatabase.cpp` | Main `SQLiteDatabase` implementation |
| `src/xrpld/app/rdb/backend/detail/Node.cpp` | Ledger/tx read-write operations |
| `src/xrpld/app/rdb/detail/State.cpp` | Deletion state tracking |
| `src/xrpld/core/detail/DatabaseCon.cpp` | Legacy reference; lifecycle now in `libxrpl` |
