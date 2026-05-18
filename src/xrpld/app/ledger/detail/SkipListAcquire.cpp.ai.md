# SkipListAcquire.cpp

## Role in the Ledger Replay Subsystem

`SkipListAcquire` is one of two sub-task types in the XRPL ledger replay system (the other being `LedgerDeltaAcquire`). Its sole responsibility is to retrieve the **skip list** for a given ledger — a special account-state object stored under `keylet::skip()` whose `sfHashes` field is a `STVector256` of ancestor ledger hashes at exponentially-spaced intervals. The `LedgerReplayTask` cannot determine which ancestor ledgers to download until it has this list, making `SkipListAcquire` the mandatory first step for every replay operation.

`LedgerReplayer` creates one `SkipListAcquire` per unique finish-ledger hash, storing a `weak_ptr` in its `skipLists_` map. If two replay tasks share the same finish hash they also share a single `SkipListAcquire`, and both tasks register callbacks that fire when the skip list is available.

## TimeoutCounter Base Class

`SkipListAcquire` inherits from `TimeoutCounter`, which implements a timer-driven active-object loop. After construction, calling `init()` starts the loop: `setTimer()` arms an async timer; when it fires, `queueJob()` submits a job to the application's job queue; the job calls `invokeOnTimer()`, which calls the virtual `onTimer()` hook; if the object is still not done, the loop repeats. The concrete subclass overrides `onTimer()` to retry, give up, or take whatever action it needs. This means `SkipListAcquire` never blocks — all network interaction is asynchronous and driven by job-queue callbacks.

The `pmDowncast()` override returning `shared_from_this()` as `weak_ptr<TimeoutCounter>` is the mechanism by which `TimeoutCounter` safely extends its own lifetime when scheduling jobs: it captures a weak pointer to itself, preventing use-after-free if the object is destroyed between the timer firing and the job executing.

## Acquisition Strategy and the Fallback Path

`trigger()` is the core logic, called both during `init()` and on each timer expiry. It has three layers:

1. **Local short-circuit**: If the ledger is already present in `LedgerMaster`, `retrieveSkipList()` reads `sfHashes` directly from the ledger object without touching the network. This is the fast path for a node that already has the ledger cached.

2. **LedgerReplay protocol** (primary): `peerSet_->addPeers()` selects connected peers, filtered by both `peer->supportsFeature(ProtocolFeature::LedgerReplay)` and `peer->hasLedger(hash_, 0)`. For qualifying peers, a `TMProofPathRequest` is sent requesting the `keylet::skip()` item from the account state map. This protocol returns a Merkle proof path, meaning the caller can cryptographically verify the response before passing it to `processData()`.

3. **Generic fallback**: Peers that don't support `LedgerReplay` increment `noFeaturePeerCount_`. Once this counter reaches `MAX_NO_FEATURE_PEER_COUNT` (2), `fallBack_` is set, the timer interval is tripled from 250 ms to 1000 ms, and the acquisition falls back to `inboundLedgers_.acquire()` with `Reason::GENERIC` — a full ledger download. The timer extension is deliberate: a full ledger download takes far longer than a single proof-path response, so polling more slowly avoids unnecessary work.

The two modes are not mutually exclusive: after `fallBack_` is set, `trigger()` still attempts to expand the peer set on each timeout (since the first `if (!fallBack_)` block is skipped, not the entire function), and `inboundLedgers_.acquire()` is also called on every `onTimer()` trigger.

## Data Processing and Verification

`processData()` is called by `LedgerReplayer::gotSkipList()` with data that has already been verified against the ledger hash before arrival. The method deserializes the raw `SHAMapItem` bytes into an `SLE` (Serialized Ledger Entry) using `SerialIter`, then extracts the `sfHashes` field. The entire deserialization is wrapped in a bare `catch(...)` — if anything throws, `failed_` is set and all callbacks are notified with `successful=false`. This is intentional defensive coding: malformed or unexpected network data should never crash the node, and the replay task can handle failures by giving up or retrying at a higher level.

The `XRPL_ASSERT` at the top of `processData()` checks that `ledgerSeq != 0` and `item` is non-null — these are preconditions documented by the fact that the `LedgerReplayer` only routes verified data here. The assertion exists as a development-time guard, not a runtime safety net.

## Callback Notification and Concurrency

`addDataCallback()` allows any number of `LedgerReplayTask` objects to register `OnSkipListDataCB` callbacks. The design handles a subtle race: if a callback is registered after the `SkipListAcquire` has already completed (e.g., a second replay task attaches to a `SkipListAcquire` that already succeeded), `addDataCallback()` detects `isDone()` and immediately calls `notify()` rather than leaving the callback enqueued indefinitely.

`notify()` uses an explicit unlock/re-lock around the callback invocations. It `std::swap`s the callback vector out under lock, then drops the lock, fires each callback, and re-acquires the lock before returning. This is necessary because the callbacks re-enter `LedgerReplayer` and `LedgerReplayTask`, which have their own mutexes — calling them with the `SkipListAcquire` mutex held would create a lock-ordering hazard. The swap-then-unlock pattern ensures each callback is called exactly once even if `addDataCallback()` is called concurrently.

`TimeoutCounter` uses a `recursive_mutex` rather than a plain `mutex`. This is required because both `init()` and `onTimer()` call `trigger()` while holding the lock, and `trigger()` may in turn call `retrieveSkipList()` then `onSkipListAcquired()` then `notify()` — all inside the same lock scope. A non-recursive mutex would deadlock immediately.

## Lifecycle and Ownership

`LedgerReplayer` holds a `weak_ptr<SkipListAcquire>` in its `skipLists_` map. The strong reference is held by `LedgerReplayTask`, which stores a `shared_ptr<SkipListAcquire>` to keep the object alive for the duration of the task. When all replay tasks complete and release their `shared_ptr`, the `SkipListAcquire` is destroyed. The `LedgerReplayer::sweep()` operation is where the stale `weak_ptr` entries are cleaned up. This ownership model means the skip list fetcher lives exactly as long as it is needed, without requiring explicit cancellation.