# `PendingSaves.h` — In-Flight Ledger Save Tracking

`PendingSaves` solves a narrow but critical consistency problem: when a validated ledger is being written to the SQLite relational database, there is a window in which the ledger exists in memory but its index entries are incomplete on disk. Any code that reports the "validated range" of ledgers to peers or clients must exclude these in-progress sequences, otherwise it could direct a requester to query a partially-written row.

The class lives in `include/xrpl/ledger/PendingSaves.h`, is instantiated once as a value member (`pendingSaves_`) inside the application object, and is exposed through the `ServiceRegistry` interface as `getPendingSaves()`. Two call sites drive its lifecycle: `pendSaveValidated()` / `saveValidatedLedger()` in `LedgerPersistence.cpp`, and `LedgerMaster::getValidatedRange()` which reads a snapshot to shrink the reported range.

## Internal State Machine

The core state is `std::map<LedgerIndex, bool> map_`, protected by a `std::mutex`. Each entry encodes one of three observable states:

| Map state | Meaning |
|---|---|
| key absent | Not pending; safe for DB queries |
| key present, value `false` | Registered/dispatched, but no thread has started the DB write yet |
| key present, value `true` | A thread is actively writing to SQLite |

The `std::condition_variable await_` is signaled in `finishWork()` so that threads blocked in `shouldWork()` can re-evaluate after a write completes.

## The Four-Method Protocol

**`shouldWork(seq, isSynchronous)`** is the entry point. If `seq` is absent, it inserts the entry as `false` and returns `true` — the caller owns the right to dispatch this work. If `seq` is already present with `false` (dispatched but unstarted), an asynchronous caller returns `false` (already dispatched, no duplication needed), but a *synchronous* caller returns `true` (it can "steal" the work before any thread claims it). If `seq` is present with `true` (actively in progress), a synchronous caller blocks on `await_` in a `do/while` loop, re-checking after each notification until the entry disappears. This blocking path ensures that a synchronous `pendSaveValidated` call does not return until the database write is complete.

**`startWork(seq)`** atomically claims the work. It flips the entry from `false` to `true` and returns `true`. If the entry is absent or already `true`, it returns `false` — meaning either the save completed out from under the caller, or another thread already started it. The caller in `saveValidatedLedger()` uses this as a guard: a `false` return causes an early return with a "Save aborted" log, preventing double-writes.

**`finishWork(seq)`** erases the entry from the map and calls `notify_all()`. Erasing rather than resetting the boolean is intentional: absence is the canonical "done" state used by `pending()` and the outer loop in `shouldWork()`.

**`getSnapshot()`** returns a copy of the entire map under the lock. This is used by `LedgerMaster::getValidatedRange()` which iterates the snapshot to trim the min/max validated sequence range, excluding any ledger whose sequence number appears in the map (regardless of whether its flag is `false` or `true`). The trimming applies first to the boundary values, then uses a best-effort midpoint split for any interior pending sequences — trading range width for correctness.

## Concurrency Design

The combination of a single mutex with a condition variable is a deliberate simplicity choice: the critical section for any single operation is tiny (map lookup, insert, erase), so contention is negligible. The blocking synchronous path in `shouldWork()` re-acquires the same lock after waking, and re-checks in a loop because `notify_all()` can wake multiple waiters, only one of which will find the entry gone.

The unit test in `PendingSaves_test.cpp` explicitly exercises the "work stealing" scenario: `shouldWork(0, false)` registers the entry, then `shouldWork(0, true)` by a synchronous caller returns `true` (steals the work), and `startWork(0)` by the first caller subsequently succeeds while a second `startWork(0)` returns `false`. This guards against duplicate DB writes when asynchronous dispatch races against synchronous demand.

The class does not own or reference a `JobQueue` or thread pool — it is purely a coordination primitive. All scheduling decisions live in `pendSaveValidated()`, which decides whether to enqueue an async job or fall through to a synchronous write based on `isSynchronous` and `JobQueue` availability. `PendingSaves` is concerned only with tracking state, not policy.