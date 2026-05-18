# `src/libxrpl/server/State.cpp` — Online-Delete State Persistence

This file implements the low-level SQLite persistence layer for the XRPL node's **online-delete** (ledger rotation) subsystem. When a rippled node is configured with online deletion enabled, it must survive restarts without losing track of which backing stores are active and how far deletion has already progressed. `State.cpp` encapsulates exactly that bookkeeping: two minimal tables, six thin SQL-wrapper functions, and carefully validated initialization logic.

## The Two Tables

`initStateDB` creates and seeds two tables in an SQLite database whose path and name come from the node configuration:

**`DbState`** tracks which of the two rotating node-store shards is currently writable, which is the archive (read-only), and the ledger sequence number at which the last rotation completed (`LastRotatedLedger`). The pair of shard path names (`WritableDb`, `ArchiveDb`) are filenames resolved at runtime; on a rotation, the old writable shard becomes the archive and a fresh shard becomes writable. Persisting these names means the node can reattach to the correct backends after a crash or a planned restart.

**`CanDelete`** stores a single ledger sequence — the high-water mark below which the deletion thread is permitted to discard ledger data. This is the persistence side of rippled's "advisory delete" feature: an operator or the `can_delete` RPC command (`CanDelete.cpp`) can advance this threshold, and the value must survive process restarts.

Both tables follow a **singleton-row pattern**: every SELECT and UPDATE targets `WHERE Key = 1`, and `initStateDB` inserts that row with blank/zero defaults if it doesn't already exist. This simplicity is intentional — the tables have no business being multi-row; they are effectively two named fields that outlive the process.

## Initialization and Failure Handling

The most structurally interesting part of `initStateDB` is how it verifies that the seed row was written. Rather than relying on `INSERT`'s side effects, it first counts the existing row with `SELECT COUNT(Key) FROM DbState WHERE Key = 1` and, critically, captures the result via `boost::optional<std::int64_t>` rather than `std::optional`. A comment in the code explains this directly: SOCI's bind-into mechanism requires `boost::optional`, not the C++17 standard variant. If SOCI returns a null indicator (which should not happen for `COUNT(*)` but is theoretically possible if the session is in a bad state), the optional is empty and `Throw<std::runtime_error>` aborts startup immediately. This pattern runs twice — once for `DbState`, once for `CanDelete` — before either `INSERT` is attempted.

`PRAGMA synchronous=FULL` is applied during init. This is the strictest SQLite durability setting, forcing full fsync on every write. For a data file this small and this infrequently written, the performance cost is negligible; the benefit is that a power loss cannot corrupt the rotation bookmarks. Losing track of which shard is writable would corrupt the node store.

## The API Surface

The six free functions in the `xrpl` namespace are deliberately stateless: each one accepts a `soci::session&` and does exactly one SQL operation. There is no caching, no in-memory shadow, and no object lifetime to manage. This is the right trade-off because the sole consumer is `SHAMapStoreImp::SavedStateDB` (in `SHAMapStoreImp.h` / `SHAMapStoreImp.cpp`), which wraps every call site with a `std::mutex`. Thread safety is the caller's responsibility; the functions here are intentionally unaware of it.

`setCanDelete` returns the value that was just stored — the same value passed in. The return is present to match the shape of the `SHAMapStore` public interface and to allow call sites to confirm what was persisted, but no caller currently inspects it. `setLastRotated` is a narrower sibling of `setSavedState`: it updates only `LastRotatedLedger` without touching the shard names, which is useful during a mid-rotation progress update where the names have not yet changed.

## Role in the Broader System

`SHAMapStoreImp::SavedStateDB` is the only direct user of these functions. It is a private inner class of `SHAMapStoreImp`, the component that manages the rotating dual-shard NodeStore. On startup, `SavedStateDB::init` calls `initStateDB` to open the database and ensure the schema exists. As the rotation thread advances, it calls `setState` (which calls `setSavedState`) to atomically record both the new shard names and the rotated ledger index, or `setLastRotated` to checkpoint progress within a rotation. The `can_delete` RPC handler (`CanDelete.cpp`) reaches `setCanDelete` through `SHAMapStore`'s public interface, giving operators runtime control over the deletion threshold without restarting the node.

The `SavedState` struct — three fields, defined in `State.h` — is the only data type exchanged between this layer and its callers. Its simplicity reflects the limited scope of what needs to survive a restart: two path strings and one integer. Everything else (in-memory caches, rotation scheduling, health checks) lives in `SHAMapStoreImp` itself and is rebuilt from scratch on each startup using these three persisted values as the starting point.