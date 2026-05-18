# `LedgerMaster.h` — Central Ledger State Manager

`LedgerMaster` is the single authoritative coordinator for all ledger state inside a running `rippled` node. It tracks four distinct ledger views simultaneously, manages the pipeline that advances a locally-built ledger to fully-validated-and-published status, buffers transactions that arrive between ledger closes, and serves the fetch-pack protocol that lets peers fill historical gaps. Nearly every other subsystem that needs to know "what ledger are we on?" queries `LedgerMaster`.

## Four Concurrent Views of Ledger State

The class maintains four named ledger pointers with carefully differentiated semantics:

| Pointer | Meaning |
|---|---|
| `mClosedLedger` | The most recent last-closed ledger (LCL): consensus has finished but quorum validation has not yet been confirmed. |
| `mValidLedger` | The highest-sequence ledger for which the local node has collected a quorum of trusted validations. |
| `mPubLedger` | The last ledger published to clients (via `NetworkOPs`); can lag `mValidLedger` while `doAdvance` fills any gap. |
| `mPathLedger` / `mHistLedger` | Internal bookmarks used for pathfinding and history-fill work respectively. |

The split between "closed" and "validated" is fundamental to XRPL's consensus model: a node can close a ledger locally well before the network has produced enough validations. `getClosedLedger()` returns `mClosedLedger` directly (the holder is internally synchronized). `getValidatedLedger()` goes through the `LedgerHolder` wrapper which holds its own `std::mutex`. For performance-critical reads that only need the index or sign-time, the class exposes `mValidLedgerSeq`, `mValidLedgerSign`, `mPubLedgerSeq`, and `mPubLedgerClose` as `std::atomic` values so callers never take a lock just to check whether the node is caught up.

## `LedgerHolder` — Immutable-Only Thread-Safe Wrapper

The two "hot" ledgers (`mClosedLedger`, `mValidLedger`) use `LedgerHolder` rather than a raw `shared_ptr`. `LedgerHolder::set()` enforces two invariants at call time: the pointer must be non-null, and `ledger->isImmutable()` must be true. This prevents a mutable working ledger from accidentally escaping into the holder, which would allow races on the ledger's state tree. The holder's internal `std::mutex` is separate from `LedgerMaster`'s own `m_mutex` — so reading the current validated ledger is never blocked by the broader master lock.

## The Advance Pipeline

When consensus finishes, it calls `switchLCL()` (on a networked node) which stores the new LCL and then calls `checkAccept()`. `checkAccept()` queries the validations subsystem for the number of trusted validations for that ledger's hash. If the count meets quorum, `setValidLedger()` is called, which also updates `mValidLedgerSeq`, notifies the amendment table, triggers `SHAMapStore::onLedgerClosed()`, and checks for unsupported enabled amendments that would put the node into amendment-blocked mode.

After a new validated ledger is committed, `tryAdvance()` schedules a job to run `doAdvance()`, which works under `m_mutex`. `doAdvance()` calls `findNewLedgersToPublish()` to collect any contiguous validated ledgers that haven't been published yet, then publishes them in sequence order. If there are gaps in `mCompleteLedgers`, it calls `fetchForHistory()` to acquire missing ledgers from the network, bounded by `MAX_LEDGER_GAP` (100 ledgers), ledger age (`MAX_LEDGER_AGE_ACQUIRE` = 1 minute), and write-load (`MAX_WRITE_LOAD_ACQUIRE`). The private `doAdvance()` and `fetchForHistory()` signatures take a `std::unique_lock<std::recursive_mutex>&` by reference — a documentation pattern making it clear at every call site that the caller must already hold `m_mutex`.

## `canBeCurrent()` — Defense Against Ledger Injection

Before any ledger is accepted as a new "current" reference, `canBeCurrent()` applies three independent sanity checks:

1. The candidate sequence must be ≥ the last validated sequence — never jump backward.
2. The candidate's `parentCloseTime` must be within 5 minutes of the node's wall clock (with a grace period for early startup before ledger 10).
3. The candidate sequence must not exceed `validLedger.seq + 10 + (elapsed_seconds / 2)` — preventing a malicious or diverged majority from bumping the sequence far into the future.

This is a layered defense that makes it expensive to force a node onto a wrong chain even if an attacker controls a significant fraction of the network's validators.

## Held Transactions and `CanonicalTXSet`

Transactions that arrive while the node is closing a ledger or during a gap are placed in `mHeldTransactions`, a `CanonicalTXSet`. At the start of `applyHeldTransactions()`, the set is swapped out under the lock and then passed to `NetworkOPs::processTransactionSet()` outside the lock — keeping the critical section minimal. `popAcctTransaction()` supports per-account transaction chaining: when a transaction for an account succeeds, the caller can immediately pull the next queued transaction for the same account without waiting for the next close cycle.

## Fetch Pack Subsystem

`LedgerMaster` inherits from `AbstractFetchPackContainer`, a narrow interface that exposes only `getFetchPack(hash)`. This abstraction lets peer-layer code retrieve partial ledger data blobs without a direct dependency on `LedgerMaster` itself. Internally, `fetch_packs_` is a `TaggedCache<uint256, Blob>` (65 536-entry capacity, 45-second TTL) that stores raw SHAMap node data. `makeFetchPack()` assembles a response pack for a requesting peer, walking backward from `haveLedgerHash` to collect up to `ledger_fetch_size_` nodes. The `mGotFetchPackThread` atomic flag prevents more than one concurrent gotFetchPack job from being dispatched to the job queue.

## Concurrency Design

The class employs three synchronization primitives at different granularities:

- `m_mutex` (`std::recursive_mutex`) — the main lock covering most mutable state including `mClosedLedger`, `mHeldTransactions`, advance-thread flags, and pathfinding state. Recursive because `tryAdvance()` can be called from within code that already holds the lock.
- `mCompleteLock` (`std::recursive_mutex`) — a separate lock solely for `mCompleteLedgers` (a `RangeSet<uint32_t>`), preventing the expensive range scan from blocking the main lock.
- Atomics (`mValidLedgerSeq`, `mValidLedgerSign`, `mPubLedgerSeq`, `mPubLedgerClose`, `mBuildingLedgerSeq`) — allow lightweight reads of frequently-polled values from any thread without contention.

## Metrics and Network Guard

The nested `Stats` struct wires two `beast::insight::Gauge` meters (`Validated_Ledger_Age`, `Published_Ledger_Age`) through a hook that calls `collect_metrics()` on each reporting cycle. This surfaces latency between the validated and published ledgers to whatever stats backend is configured.

The constant `max_ledger_difference_` (one million sequences) guards against a validator accidentally switching between the test and production networks. If a node's stored `mLastValidLedger` is more than one million sequences ahead of the first ledger being validated after startup, an assertion fires rather than silently accepting the cross-network state.

## Relationship to Sibling Files

`LedgerHistory` stores a `TaggedCache` of recent ledgers by hash and a separate mapping by index, and also tracks the "built vs. validated" divergence per ledger sequence for consensus debugging. `LedgerMaster` delegates all historical cache lookups to it. `LedgerReplay` carries an ordered map of transactions plus parent/replay ledger pointers. `LedgerMaster` holds at most one replay at a time under the main mutex, transferring ownership in and out via `takeReplay()`/`releaseReplay()` — a move-only protocol that prevents the replay state from being shared across threads.