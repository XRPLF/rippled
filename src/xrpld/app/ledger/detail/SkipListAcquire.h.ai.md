# `SkipListAcquire.h` — Network Acquisition of a Ledger's Skip List

## Role in the System

`SkipListAcquire` is a subtask in the ledger replay subsystem. When a validator needs to replay a range of historical ledgers (for gap recovery or catch-up), it first needs to know which ledger hashes form the chain it must reconstruct. XRPL encodes this information as a *skip list* — a compact, exponentially-spaced list of ancestor ledger hashes stored at the well-known `keylet::skip()` key in every ledger's state tree. This class is responsible for fetching that skip list for a specific ledger identified by its 256-bit hash, either from local storage or from the peer network.

The class sits alongside `LedgerDeltaAcquire` (which fetches a ledger's header and transactions) and `TransactionAcquire` inside `src/xrpld/app/ledger/detail/`, sharing the same `TimeoutCounter` infrastructure. `LedgerReplayer` orchestrates both kinds of subtasks, keeping a `hash_map<uint256, std::weak_ptr<SkipListAcquire>>` so that multiple concurrent replay tasks that need the same ledger's skip list share a single acquisition object.

## Class Design

`SkipListAcquire` inherits from three bases:

- **`TimeoutCounter`** — provides the asynchronous timer loop: after construction, `init()` arms a repeating deadline timer (250 ms by default, sourced from `LedgerReplayParameters::SUB_TASK_TIMEOUT`). Each expiry calls the virtual `onTimer()` override. If progress stalls beyond `SUB_TASK_MAX_TIMEOUTS` (10) expirations the task is marked failed. The base class uses a `recursive_mutex` exposed as `mtx_` and a `ScopedLockType = std::unique_lock<recursive_mutex>` that all derived classes share.
- **`std::enable_shared_from_this`** — required so `pmDowncast()` can safely hand the base class a `weak_ptr<TimeoutCounter>` pointing to the same control block.
- **`CountedObject<SkipListAcquire>`** — a lightweight diagnostic wrapper that tracks live instance counts, useful for monitoring the replay subsystem's memory footprint.

## Acquisition Flow

`init(numPeers)` is the entry point. It acquires the mutex, calls `trigger()` to begin the first round, then calls `setTimer()` to arm the retry clock.

`trigger(limit, sl)` implements a **local-first** strategy: it checks `LedgerMaster::getLedgerByHash()` before going to the network. If the ledger already exists locally (e.g., the node downloaded it independently), `retrieveSkipList()` extracts the skip list from the state map via `keylet::skip()` — avoiding any peer communication. This short-circuit is important because the replay subsystem is often triggered precisely when the node is catching up and may already have the needed ledger.

If the ledger is absent locally, `trigger()` sends a `TMProofPathRequest` network message to up to `limit` new peers, filtered to those that both claim to hold the ledger (`peer->hasLedger(hash_, 0)`) and support the `ProtocolFeature::LedgerReplay` protocol extension. The response path eventually calls `processData()` on the matching `SkipListAcquire` instance in `LedgerReplayer::gotSkipList()`.

## Fallback to Legacy Acquisition

A non-obvious design decision: XRPL's `LedgerReplay` feature is relatively new, and peers on the network may not support it. If `trigger()` encounters `MAX_NO_FEATURE_PEER_COUNT` (2) peers that lack the feature, it activates `fallBack_ = true`, switches the timer interval to the longer `SUB_TASK_FALLBACK_TIMEOUT` (1000 ms), and starts calling `InboundLedgers::acquire()` — the older, full-ledger download path. This is heavier but guaranteed to work with any peer. The extended timeout compensates for the fact that full ledger downloads take longer than proof-path responses.

## Data Path and `processData()`

When a peer responds, the overlay layer delivers verified data to `processData(ledgerSeq, item)`. The `item` is a `SHAMapItem` whose content has already been verified against the ledger hash by the caller — the note in the header makes this contract explicit. Inside, the method deserializes the item into a `SLE` (State Ledger Entry), reads the `sfHashes` field (the skip list), and calls `onSkipListAcquired()` if the list is non-empty. Any deserialization exception is caught with an empty catch block and translated into a failure, since bad data from a peer should not propagate as an exception.

## Immutable Result and `SkipListData`

Once acquired, the result is stored as `std::shared_ptr<SkipListData const>` — a shared pointer to an immutable `SkipListData` struct holding the ledger sequence number and the vector of hashes. The `const` here is load-bearing: because the pointer itself is `shared_ptr<... const>`, all readers get a stable snapshot without needing to hold the mutex. `getData()` simply locks, copies the shared pointer, and returns it.

## Callback Management and `notify()`

Multiple callers can register completion callbacks via `addDataCallback(cb)` before the task finishes. If a callback is added *after* the task has already completed, `addDataCallback` immediately calls `notify()` — handling the race gracefully rather than silently dropping the notification.

`notify()` has a careful locking protocol to avoid re-entrant deadlocks: it swaps the entire callback vector out into a local `toCall`, calls `sl.unlock()` before invoking any of them, and relocks afterward. This means callbacks are fired outside the mutex, permitting them to call back into the `SkipListAcquire` API (e.g., `getData()`) or trigger further work without deadlocking on the `recursive_mutex`. All private methods requiring the lock accept `ScopedLockType& sl` as an explicit parameter, making the locking discipline visible at the call site rather than buried in per-method acquire/release pairs.

## Test Seam

The `friend class test::LedgerReplayClient` declaration grants the test harness direct access to private state, a common pattern in this codebase for integration tests that need to inject verified data or inspect internal fields without going through the full network stack.