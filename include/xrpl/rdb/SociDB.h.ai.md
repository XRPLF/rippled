# `include/xrpl/rdb/SociDB.h` — SOCI Database Abstraction Layer

This header is the public surface of the XRP Ledger's thin but carefully designed wrapper around the [SOCI](https://soci.sourceforge.net/) database library. It solves three distinct problems: normalizing session creation from the node's configuration system, bridging SOCI's binary column type to idiomatic C++ containers, and managing SQLite Write-Ahead Log (WAL) checkpointing through the XRPL job queue without blocking database operations.

## Why This Layer Exists

SOCI provides a cross-backend, C++ SQL interface, but it does not know about `BasicConfig`, XRPL's diagnostic memory reporting, or the job scheduling system. This file defines the glue that makes SOCI usable inside the XRPL server's operational model. Although the config section `[sqdb]` technically accepts a `backend` key, the implementation in `SociDB.cpp` throws on anything except `"sqlite"` — the abstraction is forward-looking, but only SQLite is supported today.

## `DBConfig` — Deferred Session Opening

`DBConfig` captures the information needed to open a `soci::session` without actually opening it yet. The public constructor takes a `BasicConfig` and a database name, reads the `[sqdb]` section to find the backend, reads `database_path` from the legacy config, and constructs a file path. Two special database names — `"validators"` and `"peerfinder"` — receive a `.sqlite` extension; all others get `.db`. This distinction is baked into `getSociInit()` in the `.cpp` and reflects historical naming conventions in the codebase.

The private constructor `DBConfig(std::string const& dbPath)` takes a raw path and is the only way to store the connection string. This forces all public callers through the config-parsing path, making accidental misuse of bare paths impossible from outside the translation unit.

When a caller is ready to open the session, it calls `DBConfig::open(soci::session& s)`, which delegates to `s.open(soci::sqlite3, connectionString_)`. Alternatively, the two free `open()` functions bypass `DBConfig` and open a session immediately — useful when the session and config are both available at the same point in initialization.

## `getKBUsedAll` and `getKBUsedDB`

These diagnostic functions expose SQLite's internal memory counters through the XRPL layer. `getKBUsedAll()` calls `sqlite3_memory_used()`, which reports total SQLite heap allocation across all connections. `getKBUsedDB()` calls `sqlite3_db_status()` with `SQLITE_DBSTATUS_CACHE_USED` for the per-connection page cache. Both reach the raw `sqlite3*` handle by dynamic-casting the SOCI session backend to `soci::sqlite3_session_backend` and extracting its `conn_` field — a necessary but brittle coupling to SOCI internals that can only be avoided by patching SOCI itself.

## `convert` Overloads — Bridging `soci::blob` and C++ Types

SOCI's `blob` type is the correct mapping for SQLite BLOB columns, but its read/write interface uses raw `char*` buffers. The four `convert()` overloads translate between `soci::blob` and `std::vector<uint8_t>` or `std::string`, handling the empty-container edge case explicitly (calling `blob.trim(0)` on write, and early-returning on read). This normalization means callers never have to reason about SOCI's binary API directly.

## `Checkpointer` — WAL Management via the Job Queue

The `Checkpointer` abstract base class defines the interface for SQLite WAL checkpointing. Its concrete implementation, `WALCheckpointer` (private to `SociDB.cpp`), is where the interesting engineering lives.

SQLite in WAL mode appends writes to a separate log file and periodically must "checkpoint" — copy completed WAL frames back into the main database file. If this never happens, the WAL grows without bound. SQLite does perform automatic checkpoints, but XRPL needs checkpointing to happen on the `JobQueue` thread (`jtWAL`), not inline during a write operation, to avoid latency spikes.

`WALCheckpointer` registers a `sqlite3_wal_hook` during construction. This hook fires synchronously on the database connection thread whenever the WAL reaches `checkpointPageCount` (1000) pages. The hook cannot safely call `schedule()` on the checkpointer directly and also needs a way to find the checkpointer object — since the hook receives only a `void*` user data pointer. The design encodes the checkpointer's ID (a `uintptr_t` cast of its address) as the hook's user data, then looks up the live `shared_ptr<Checkpointer>` via `checkpointerFromId()` (declared in `DatabaseCon.h`). If the lookup fails — meaning the `DatabaseCon` has been destroyed — the hook removes itself by calling `sqlite3_wal_hook(conn, nullptr, nullptr)`.

`schedule()` guards against double-queuing with a mutex-protected `running_` flag. Once the flag is set, it adds a `jtWAL` job to the `JobQueue`, capturing a `std::weak_ptr<Checkpointer>` (via `shared_from_this()`) to avoid keeping the object alive after `DatabaseCon` is destroyed. The actual checkpoint runs `SQLITE_CHECKPOINT_PASSIVE`, which checkpoints as many frames as possible without blocking readers or writers, then clears `running_`.

## Lifetime Safety Design

The most subtle aspect of this file is its ownership model. `DatabaseCon` (in `DatabaseCon.h`) holds a `std::shared_ptr<soci::session>` and passes a `std::weak_ptr<soci::session>` into the `WALCheckpointer`. This allows the `DatabaseCon` to be destroyed while a checkpoint job is still queued: the job locks the weak pointer, finds it null, and exits cleanly. Similarly, the job captures a `weak_ptr<Checkpointer>`, preventing a live job from accessing a destroyed checkpointer even in the rare race where `DatabaseCon` is torn down after the weak pointer is locked but before `checkpoint()` returns.

## Clang Diagnostic Suppression

The header wraps its SOCI include in `#pragma clang diagnostic push/pop` to silence `-Wdeprecated` warnings from SOCI's own headers. This is the correct pattern for suppressing third-party warnings at the include boundary without affecting diagnostics in XRPL's own code.

## Relationship to `DatabaseCon`

`SociDB.h` provides primitives; `DatabaseCon` (in `DatabaseCon.h`) assembles them into the operational database connection used throughout the server. `DatabaseCon` calls `open()` from this header, wraps the session in `LockedSociSession` for mutual exclusion, and optionally wires up a `Checkpointer` via `makeCheckpointer()`. Direct callers of `SociDB.h` outside `DatabaseCon` are limited to diagnostic tooling and tests.