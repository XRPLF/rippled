# `DatabaseCon.cpp` — SQLite WAL Checkpointer Lifecycle Management

## Role in the System

This file provides the runtime glue between `DatabaseCon` (the XRPL wrapper around a SOCI SQLite session) and the WAL (Write-Ahead Log) checkpointing subsystem. Its central problem is a tricky ownership and shutdown puzzle: SQLite's WAL hook is a raw C callback registered on the native connection, yet the checkpoint logic lives in a C++ object that may be executing asynchronously on a `JobQueue` thread at exactly the moment its owning connection is being destroyed. The code here solves that safely without requiring a global lock across the entire checkout/checkpoint cycle.

## `CheckpointersCollection` — A Stable Bridge for C Callbacks

The file-private class `CheckpointersCollection` is a mutex-protected registry that maps monotonically-incrementing integer IDs (`std::uintptr_t`) to live `shared_ptr<Checkpointer>` instances. It exposes three operations: `create`, `fromId`, and `erase`.

The ID scheme is the key design insight. When a `WALCheckpointer` is created, its numeric ID is cast to a `void*` and registered with SQLite's `sqlite3_wal_hook`. When SQLite fires the hook (on the thread doing a write commit), the hook receives only that `void*`. It calls the free function `checkpointerFromId()`, which delegates to `CheckpointersCollection::fromId()`. If the ID maps to a live checkpointer, `schedule()` is called; if the map entry has already been erased (because the `DatabaseCon` was torn down), `fromId()` returns `nullptr` and the hook removes itself by calling `sqlite3_wal_hook(conn, nullptr, nullptr)`.

This design avoids the alternative of embedding a raw `this` pointer in the hook, which would be immediately dangerous — there is no guarantee the `WALCheckpointer` object lives long enough for a pending hook invocation. The ID-based lookup through a guarded collection ensures the hook always either finds a valid, reference-counted object or gracefully deregisters itself.

The global `checkpointers` instance is a plain namespace-scope variable (`CheckpointersCollection checkpointers;`), making it a process-wide singleton. All `DatabaseCon` instances share this one registry.

## Session Ownership Design

`DatabaseCon` holds `session_` as a `std::shared_ptr<soci::session>`. The `WALCheckpointer` (in `SociDB.cpp`) holds only a `std::weak_ptr<soci::session>`. This split is intentional and documented in the header:

> The checkpointer may outlive the `DatabaseCon` when the checkpointer job queue callback locks a weak pointer and the `DatabaseCon` is then destroyed.

If the checkpointer held a `shared_ptr` to the session, a `jtWAL` job in flight would keep the session alive indefinitely — which is fine for the session object itself, but the calling code that destroys `DatabaseCon` might immediately open a new connection to the same SQLite file, which would fail if the old session still holds the WAL lock. The `weak_ptr` approach means the session is destroyed when `DatabaseCon` is, and `WALCheckpointer::checkpoint()` detects the expired session via `session_.lock()` returning null and exits without touching the connection.

## Destructor — Bounded Blocking Shutdown

`DatabaseCon::~DatabaseCon()` contains the most architecturally significant logic:

```cpp
checkpointers.erase(checkpointer_->id());
std::weak_ptr<Checkpointer> const wk(checkpointer_);
checkpointer_.reset();
while (wk.use_count() != 0)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
```

The sequence matters precisely:

1. **Erase from the collection** so that any future SQLite WAL hook invocation finds nothing and deregisters itself.
2. **Drop `DatabaseCon`'s own `shared_ptr`** so the only remaining references are inside any in-flight `JobQueue` lambdas.
3. **Poll the `weak_ptr` use count** until it reaches zero, meaning all job queue references have been released and the checkpoint job has finished.

The 100 ms busy-poll is a deliberate trade-off: a condition variable would require more plumbing inside `WALCheckpointer`, and database teardown is rare enough that the polling cost is negligible. Without this wait, opening a new `DatabaseCon` to the same SQLite file immediately after destroying the old one could fail because the old WAL checkpoint job might still hold a lock on the database file.

## `setupCheckpointing()` — Deferred Wiring

`setupCheckpointing(JobQueue*, ServiceRegistry&)` is separated from the constructors so that checkpointing can be conditionally enabled. Constructors that accept a `CheckpointerSetup` struct delegate to the base constructor first (to open and initialize the database), then call `setupCheckpointing`. Passing a null `JobQueue*` is detected immediately and throws `std::logic_error` via the XRPL `Throw<>` utility — a programming error, not a runtime failure.

The function creates a `WALCheckpointer` via `makeCheckpointer()`, assigns it to `checkpointer_`, and simultaneously stores it in the global `CheckpointersCollection`. Storing in the collection must happen before the checkpointer is returned, because the SQLite WAL hook is armed inside the `WALCheckpointer` constructor — hook invocations can begin arriving immediately.

## Relationship to `SociDB.cpp`

The actual checkpoint logic — the `WALCheckpointer` class, the `sqlite3_wal_hook` registration, the `SQLITE_CHECKPOINT_PASSIVE` call — lives entirely in `SociDB.cpp`. `DatabaseCon.cpp` knows nothing about SQLite directly. It only manages the `Checkpointer` abstract interface: creating instances via `makeCheckpointer`, registering them in the collection, and cleaning them up on destruction. This separation keeps the lifecycle management (this file) decoupled from the checkpoint implementation details.