# `src/libxrpl/rdb/SociDB.cpp`

## Role and Context

This file implements the XRPL ledger's thin adapter layer between the [SOCI](http://soci.sourceforge.net/) database abstraction library and SQLite, the only supported database backend. It covers three distinct concerns: session lifecycle management, diagnostic memory reporting, blob data conversion, and — most significantly — automated Write-Ahead Log (WAL) checkpointing. Everything here exists to insulate the rest of the codebase from SOCI's C++ quirks and SQLite's internal API surface.

The file opens with a Clang `#pragma clang diagnostic ignored "-Wdeprecated"` guard because SOCI's own headers use deprecated constructs. Rather than suppress this globally, the guard wraps just this translation unit.

## Session Configuration and Opening

`DBConfig` provides a two-phase construction idiom: parse the connection parameters once, open the session later. The non-public `DBConfig(std::string const& dbPath)` constructor is called by the public `DBConfig(BasicConfig const&, std::string const&)` form after the `detail::getSociInit` helper resolves the backend name and file path.

`detail::getSociInit` reads the `[sqdb]` section of the node config, looking for a `backend` key (defaulting to `"sqlite"`). If anything other than `"sqlite"` is specified, it throws immediately — the code makes no pretense of supporting other backends. It also handles a legacy naming quirk: the `validators` and `peerfinder` databases use the `.sqlite` extension while all other databases use `.db`. This inconsistency is a historical artifact preserved by the explicit branch in `getSociInit`.

`detail::getSociSqliteInit` constructs the final filesystem path. It throws `std::runtime_error` if the database name is empty, which would otherwise silently produce an unusable path.

Two free-function `open()` overloads provide an eager alternative to `DBConfig` when the session should be opened at the call site. The config-based overload delegates through `DBConfig`; the explicit-string overload accepts a backend name for forward-compatibility but enforces the same "sqlite only" constraint. Both paths ultimately call `s.open(soci::sqlite3, connectionString)`.

## Penetrating the SOCI Abstraction

SOCI's session object hides its underlying database connection behind a polymorphic backend interface. The static `getConnection()` helper uses `s.get_backend()` followed by a `dynamic_cast<soci::sqlite3_session_backend*>` to recover the raw `sqlite3*` connection pointer from the `conn_` member. If the cast fails or the pointer is null, it throws `std::logic_error`. This is the only place in the file where the SOCI abstraction is deliberately broken — it is necessary to invoke SQLite-specific APIs that SOCI does not expose (WAL hooks, memory statistics).

`getKBUsedAll()` calls `sqlite3_memory_used()`, a SQLite process-global metric returned in kilobytes. `getKBUsedDB()` calls `sqlite3_db_status(..., SQLITE_DBSTATUS_CACHE_USED, ...)` to report the page-cache footprint of a specific database connection. Both functions exist to feed the node's performance and diagnostic subsystems.

## Blob Conversion Helpers

Four overloaded `convert()` functions bridge SOCI's `blob` type to and from `std::vector<uint8_t>` and `std::string`. The SOCI blob API operates through `read`/`write` methods on a character buffer, which is awkward to use directly. These helpers centralise the `reinterpret_cast` between `char*` and `uint8_t*` and handle the empty-input edge case: writing a zero-byte blob requires calling `blob.trim(0)` rather than `blob.write(...)` with a null pointer.

## WAL Checkpointing

The most architecturally interesting part of the file is `WALCheckpointer`, an anonymous-namespace class that derives from the public `Checkpointer` interface.

SQLite in WAL mode accumulates writes in a separate log file. Without periodic checkpointing that log file grows without bound and old readers are blocked from being recycled. SQLite does run its own automatic checkpoint at 1000 pages, but that happens synchronously on whichever thread is writing. `WALCheckpointer` offloads this work to the node's `JobQueue` so database writers are never stalled.

The checkpointer installs itself via `sqlite3_wal_hook`. SQLite calls this hook — on the writing thread — after every WAL write. The hook receives a `void*` cookie which `WALCheckpointer` sets to its own integer ID (`std::uintptr_t id_`), not a raw pointer. This is deliberate: a raw `this` pointer would be a use-after-free hazard if the `DatabaseCon` owning the session is destroyed while a WAL write is in progress. The ID is instead used to look up the checkpointer in the global `CheckpointersCollection` (defined in `DatabaseCon.cpp`), which is a process-wide thread-safe map from integer ID to `shared_ptr<Checkpointer>`. If `checkpointerFromId` returns null (because the `DatabaseCon` has been destroyed and its destructor called `checkpointers.erase()`), the hook defensively unregisters itself by calling `sqlite3_wal_hook(conn, nullptr, nullptr)`.

`schedule()` uses a `running_` boolean under a mutex to ensure at most one checkpoint job is in-flight at a time. If the `JobQueue` itself rejects the job (e.g., during shutdown), the `running_` flag is reset so the next hook invocation can try again. The job captures a `weak_ptr<Checkpointer>` (not `this`) so that if the `DatabaseCon` is torn down between the job being enqueued and it executing, the lambda safely observes an expired weak pointer and exits without touching the session.

`checkpoint()` calls `sqlite3_wal_checkpoint_v2` with `SQLITE_CHECKPOINT_PASSIVE`, meaning it checkpoints only WAL frames that are not currently being read. `SQLITE_LOCKED` results are logged at trace level (expected under contention); other errors are logged as warnings. After each checkpoint attempt, `running_` is reset under the mutex.

The `WALCheckpointer` also holds a `weak_ptr<soci::session>` rather than a raw reference. `DatabaseCon::~DatabaseCon` (in `DatabaseCon.cpp`) erases the checkpointer from the global collection, drops its own `shared_ptr` to the checkpointer, then spins waiting for the checkpointer's use count to reach zero before returning. This ensures the session is not destroyed while a checkpoint job is mid-execution on a worker thread.

The `static checkpointPageCount = 1000` module-level constant mirrors SQLite's default auto-checkpoint threshold, making the trigger condition explicit and easy to tune. The inline comment in the `WALCheckpointer` class acknowledges that SQLite already does this automatically, leaving open the question of whether the explicit hook buys anything — the answer is primarily that it routes the work onto the XRPL job queue rather than the caller's thread.