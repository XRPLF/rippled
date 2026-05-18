# `LedgerDeltaAcquire.cpp` — Single-Ledger Delta Fetcher for Ledger Replay

## Role in the System

XRPL nodes that fall behind the network can catch up by *replaying* a range of historical ledgers rather than downloading each full state trie. `LedgerDeltaAcquire` is the subtask responsible for acquiring the delta of a single ledger in that range: the ledger header (`LedgerHeader`) and its ordered transaction set (`STTx`). It is one of two subtask types in the ledger replay subsystem; the other is `SkipListAcquire`, which retrieves the skip list needed to enumerate the ledgers to replay. Both are orchestrated by `LedgerReplayTask`, which is in turn managed by `LedgerReplayer`.

## Inheritance and the Timer Loop

`LedgerDeltaAcquire` extends `TimeoutCounter`, a base class that implements an active-object retry loop backed by a Boost.Asio steady-clock timer and the application's `JobQueue`. The base class handles the mechanics: set a timer → fire a job → call `onTimer` → if still not done, re-arm. Subclasses override `onTimer()` and `pmDowncast()` (a `weak_ptr` downcast required so timer callbacks can safely check whether the object is still alive without extending its lifetime). The constructor registers `jtREPLAY_TASK` jobs capped at `MAX_QUEUED_TASKS` to bound the ledger-replay system's impact on the shared job queue.

## Initialization and Peer Selection (`init`, `trigger`)

`init()` acquires the object's mutex and calls `trigger()` followed by `setTimer()`. This two-step is intentional: `trigger()` may complete the task immediately (if the ledger is already in local storage), in which case the timer is still set but will see `isDone()` on its first fire and exit without doing work. The symmetry keeps every code path using the same timer lifecycle.

Inside `trigger()`, the first check is `getLedgerByHash(hash_)`. If the local node already has this ledger (e.g. it was stored by a parallel acquisition), the task completes immediately by marking `complete_ = true` and calling `notify()`. This short-circuit avoids unnecessary network requests and is always checked before any peer communication.

The normal path sends `TMReplayDeltaRequest` protocol messages to up to `limit` peers. The `addPeers` call uses two lambdas: a *filter* that requires the peer to both support `ProtocolFeature::LedgerReplay` and have the specific ledger, and an *action* that actually sends the request. Only peers supporting the replay feature receive the message; peers that lack it increment `noFeaturePeerCount`. When that counter reaches `MAX_NO_FEATURE_PEER_COUNT` (2), the task switches to *fallback mode*: it calls `InboundLedgers::acquire()` to download the full ledger by the traditional `GENERIC` path and extends `timerInterval_` from 250ms to 1000ms. This graceful degradation ensures the node can still catch up even if it is surrounded by peers running an older protocol version.

## Receiving Data and Two-Phase Building

When the overlay layer receives a `TMReplayDeltaResponse`, it routes the parsed, hash-verified data to `LedgerReplayer::gotReplayDelta()`, which forwards it to the relevant `LedgerDeltaAcquire` via `processData()`. At this point only a *lightweight* ledger is constructed — `replayTemp_` is a `Ledger` initialized from the header info alone, without a full SHAMap state. This object is not a valid, standalone ledger; it exists solely to carry the header metadata into `LedgerReplay` later.

The actual ledger build happens in `tryBuild()`, called by `LedgerReplayTask` when it has both a parent ledger and a completed `LedgerDeltaAcquire` subtask. `tryBuild()` assembles a `LedgerReplay` value from the parent, `replayTemp_`, and `orderedTxns_`, then calls `buildLedger()` which re-executes each transaction against the parent state and constructs the resulting SHAMap. The critical correctness check is the final hash comparison: `fullLedger_->header().hash == hash_`. If the replayed ledger's hash does not match what was requested, the task throws `std::runtime_error` — a hard failure because it indicates data corruption or a logic error, not a transient network condition.

Two `XRPL_ASSERT` calls enforce the precondition that the parent sequence is exactly one less than the target and that the parent hash matches the target's `parentHash` field. These are invariants that `LedgerReplayTask` is responsible for guaranteeing before calling `tryBuild()`.

## Callback Registration and Notification

Multiple consumers can register interest in the same delta via `addDataCallback()`. Each call adds an `OnDeltaDataCB` to `dataReadyCallbacks_` and registers a `Reason` (e.g., `GENERIC`). The `reasons_` set is used later in `onLedgerBuilt()` to decide what to do with the finished ledger — currently only `GENERIC` triggers `LedgerMaster::storeLedger()`; other reasons have placeholder `TODO` branches for future use.

`notify()` drains the callback vector by swapping it into a local before releasing the lock, then fires each callback, then reacquires the lock. This unlock-callback-relock pattern is essential: callbacks invoke `LedgerReplayTask::deltaReady()`, which takes its own lock and may call back into `LedgerDeltaAcquire`'s public interface. Without the unlock, this would deadlock on the recursive mutex. `XRPL_ASSERT(isDone())` at the top of `notify()` enforces the contract that callbacks are never dispatched while the acquisition is still in progress.

`onLedgerBuilt()` posts a `jtREPLAY_TASK` job rather than calling `LedgerMaster` inline. This keeps mutex hold times short and avoids priority inversions on a hot path that may be exercised for many sequential ledgers during a catchup.

## Timeout and Failure Handling

`onTimer()` uses a straightforward threshold policy: if `timeouts_` exceeds `SUB_TASK_MAX_TIMEOUTS` (10), the task is marked failed and `notify()` is called to propagate failure up to `LedgerReplayTask`. Otherwise it calls `trigger(1, sl)` to try adding one more peer. This means the task is willing to retry up to 10 times before giving up, giving the network roughly 10 × 250ms = 2.5 seconds (or 10 × 1000ms = 10 seconds in fallback mode) to respond. Once `LedgerReplayTask` sees a failed delta, it marks its own task as failed and propagates the failure to the node's ledger acquisition logic.