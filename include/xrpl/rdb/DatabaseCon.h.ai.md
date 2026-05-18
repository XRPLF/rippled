# `DatabaseCon.h` — SQLite Connection Management for the XRP Ledger

## Purpose

`DatabaseCon.h` defines the two central abstractions the XRPL node uses to interact with its embedded SQLite databases: `LockedSociSession`, which makes individual database operations thread-safe, and `DatabaseCon`, which owns and configures a SOCI session from first open through optional WAL checkpointing. Every ledger, transaction, and wallet database in the node passes through this layer.

## `LockedSociSession` — Thread-Safe Session Handle

`LockedSociSession` is a move-only RAII type that pairs a `std::shared_ptr<soci::session>` with a `std::unique_lock<std::recursive_mutex>`. The design is deliberate: callers acquire the session and its lock together as a single atomic step, hold them for the duration of their query or transaction, and release both automatically on destruction.

Copying is explicitly deleted (`= delete`) while moving is allowed, which makes the ownership semantics clear: at any moment, exactly one active scope holds the lock. The `get()`, `operator*()`, and `operator->()` accessors all delegate to the underlying `soci::session*`, so callers use the object as if it were a raw session pointer without needing to manage locking separately.

The use of `std::recursive_mutex` rather than a plain `std::mutex` accommodates code paths that may legitimately acquire a `LockedSociSession` while already holding the lock on the same connection — for example, in test scaffolding or in the wallet database where identity initialization may call back into the same connection.

## `DatabaseCon` — Connection Owner and Lifecycle Manager

`DatabaseCon` owns a single SQLite database connection and is responsible for opening it, applying SQLite PRAGMA settings, running initialization DDL, and optionally enabling WAL-mode checkpointing.

### Construction and the `Setup` Struct

The `Setup` struct captures everything about the node's configuration that affects how a database is opened:

- `startUp` distinguishes between `Fresh`, `Normal`, `Load`, `LoadFile`, `Replay`, and `Network` startup modes (defined in `StartUpType.h`).
- `standAlone` and `startUp` together determine whether the node uses real on-disk database files or ephemeral files. When `standAlone` is true and the startup mode is neither `Load`, `LoadFile`, nor `Replay`, the path passed to SOCI is an empty string, causing SQLite to use a temporary database that disappears on close. This avoids polluting disk during testing or short-lived standalone runs.
- `globalPragma` is a `static std::unique_ptr<std::vector<std::string> const>` — shared across all connections. It holds node-wide SOCI PRAGMA settings (journal mode, sync mode, temp store) built once from the node configuration and applied to every connection via `commonPragma()`. The `useGlobalPragma` flag on each `Setup` controls whether these are applied to a specific connection, with an assertion that `globalPragma` is non-null whenever `useGlobalPragma` is true.
- `txPragma` (4 entries) and `lgrPragma` (1 entry) are per-database arrays for connection-specific tuning.

The template constructors accept `std::array<std::string, N>` for pragmas and `std::array<char const*, M>` for initialization SQL. Using compile-time fixed-size arrays (rather than `std::vector`) lets callers pass literals like `TxDBInit` or `LgrDBInit` from `DBInit.h` without heap allocation, and the size is validated at compile time.

There are four public constructors — two accepting `Setup` (which resolves the file path and temporary-vs-real decision) and two accepting a raw `boost::filesystem::path` (for callers who already know the location). Each of these has an overload that also accepts a `CheckpointerSetup`, enabling WAL checkpointing without requiring a separate call after construction.

The private canonical constructor is where the actual work happens: it calls `open()` from `SociDB.h` with the backend name `"sqlite"`, then runs all pragmas (per-connection first, then shared global ones), then runs each `initSQL` statement in a prepared `soci::statement`. All of this happens inside the constructor body, so a fully-constructed `DatabaseCon` always has a live, initialized connection — no two-phase initialization.

### Session Ownership and the Checkpointer's `weak_ptr` Invariant

The underlying `soci::session` is stored as `std::shared_ptr<soci::session>` rather than a plain member or `unique_ptr`. This choice is driven entirely by the `Checkpointer` teardown hazard: a checkpoint job submitted to the `JobQueue` may still be running when the `DatabaseCon` is destroyed. If the session were owned by value or `unique_ptr`, the destructor would delete it while the job is executing.

By holding a `shared_ptr` to the session, the `DatabaseCon` allows the `Checkpointer` to hold a corresponding `weak_ptr`. The checkpointer's job callback locks the `weak_ptr` before accessing the session — if the lock fails (the `DatabaseCon` has been destroyed and the session's refcount hit zero), the callback aborts cleanly. The `DatabaseCon` destructor then waits — via a spin loop in `DatabaseCon.cpp` — until all references to the session are gone before returning. This ensures no job ever writes through a dangling session pointer.

The `checkpointer_` member is itself a `shared_ptr<Checkpointer>`, and checkpointers are registered in a global collection (maintained in `DatabaseCon.cpp`) keyed by a monotonically increasing numeric ID. The free function `checkpointerFromId()` provides safe external lookup into that collection, used by job callbacks that need to retrieve their checkpointer without coupling to the `DatabaseCon`.

### `checkoutDb()` vs. `getSession()`

`checkoutDb()` is the primary, safe access point. It constructs a `LockedSociSession` while wrapping the lock acquisition in `perf::measureDurationAndLog()` with a 10ms threshold. If acquiring the lock takes longer than 10 milliseconds — indicating contention — a warning is logged to `j_`. This makes lock contention on database connections observable in the node's performance logs without adding overhead to the common case.

`getSession()` returns an unlocked reference to the session. It is retained for contexts where a higher-level lock already guarantees exclusive access, or during initial setup before multi-threaded operation begins. Callers using `getSession()` take on the responsibility of ensuring no concurrent access occurs.

### Relationship to `DBInit.h` and `SociDB.h`

`DBInit.h` defines the three standard XRPL databases (`ledger.db`, `transaction.db`, `wallet.db`) and their initialization SQL arrays. These arrays map directly onto the `initSQL` template parameter of `DatabaseCon`'s constructors. The pragma constants in `DBInit.h` (`CommonDBPragmaJournal`, `CommonDBPragmaSync`, `CommonDBPragmaTemp`) are formatted at startup and stored into `Setup::globalPragma`.

`SociDB.h` provides the `open()` function used to connect the SOCI session, the `Checkpointer` abstract interface, and the `makeCheckpointer()` factory — all of which `DatabaseCon` delegates to rather than reimplementing.