# `include/xrpl/server/State.h` — Online Deletion State Database Interface

## Role in the System

This header defines the persistence interface for the XRPL node's **online deletion** subsystem. When a rippled instance is configured to delete old ledger history as it advances (`[online_delete]` configuration), it needs a small metadata database to survive restarts safely: it must remember which database file is currently writable, which is the archive, and at which ledger sequence the last database rotation occurred. `State.h` provides exactly this — the `SavedState` aggregate and a thin set of free functions that read and write it through a SOCI session.

## The `SavedState` Struct

```cpp
struct SavedState {
    std::string writableDb;
    std::string archiveDb;
    LedgerIndex lastRotated{};
};
```

This three-field struct is the entire persistent state of the online-delete rotation engine. The XRPL online deletion implementation (`SHAMapStoreImp`) maintains two rotating SQLite databases for node-store data — one actively receiving writes (`writableDb`) and one being archived or pruned (`archiveDb`). When rotation occurs, the names swap. `lastRotated` records the ledger sequence at which the most recent rotation happened, anchoring the deletion eligibility window. Without this checkpoint, a restart would lose track of where the sweep left off and could either skip deletions or double-rotate.

## Free Function Design

Rather than encapsulating the database behind an object, `State.h` exposes six free functions that each accept a raw `soci::session&`. This is a deliberate design choice: the caller (`SHAMapStoreImp::SavedStateDB`) owns the session directly as a plain `soci::session` member and wraps all calls with its own `std::mutex`. The free-function API lets the mutex layer sit at the call site without forcing any particular ownership model on the persistence layer itself.

`initStateDB()` creates the schema if it does not exist and inserts the single seed row in each table. The implementation sets `PRAGMA synchronous=FULL` before any schema work — a SQLite durability setting that forces every write to be fully flushed to disk before the call returns. For a metadata database that checkpoints crash-critical state, this is the right tradeoff despite its write-latency cost.

Both `DbState` and `CanDelete` tables use a fixed single-row pattern (always `Key = 1`). These tables function as named key-value pairs, not relational collections. This is efficient and correct for state that exists exactly once per server instance.

## Advisory Delete Fence

The `CanDelete` table stores a single `LedgerIndex` that acts as an operator-controlled fence: no ledger with sequence ≤ `canDelete` will be automatically purged. The `getCanDelete()` and `setCanDelete()` functions expose this fence. `setCanDelete()` returns the new sequence, which the caller (`SHAMapStoreImp`) also caches in the atomic `canDelete_` member for lock-free reads in the hot rotation loop.

The `can_delete` RPC handler (`CanDelete.cpp`) exposes this fence to operators, accepting symbolic values like `always`, `never`, `now`, or a ledger hash — translating them to a `LedgerIndex` before calling `setCanDelete`. This gives operators precise control over which historical data the node may prune, without halting the node.

## Relationship to `SHAMapStoreImp`

The functions here are not called directly by most of the codebase. Instead, `SHAMapStoreImp` declares an inner class `SavedStateDB` that wraps each free function behind a `std::mutex`, exposing `getState()`, `setState()`, `setLastRotated()`, `getCanDelete()`, and `setCanDelete()` as thread-safe methods. The outer `SHAMapStoreImp::run()` loop — which runs on a dedicated thread — consults `lastRotated` to determine when the next rotation is due and reads `canDelete_` to verify operator permission before pruning.

`setLastRotated()` exists as a dedicated function (separate from `setSavedState()`) because updating just the rotation sequence is a frequent, targeted operation during the sweep loop, while a full `setSavedState()` write is only needed when the database filenames change at rotation time.

## Invariants and Failure Modes

`initStateDB()` throws `std::runtime_error` if the row-count query returns a null result from SOCI — a condition that indicates a deeper database corruption or connection failure rather than a transient error. By throwing at initialization time, the system fails fast rather than silently operating with a zero-initialized state that could lead to incorrect deletion decisions. The default-zero `lastRotated` is handled explicitly in the rotation loop: a zero value means "not yet rotated," triggering an immediate bootstrap write of the current validated ledger sequence.