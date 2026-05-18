# `LedgerDeltaAcquire` — Ledger Delta Network Acquisition

`LedgerDeltaAcquire` is a subtask in the XRPL ledger-replay pipeline, responsible for fetching a single ledger's **delta** — its header (`LedgerHeader`) and ordered transaction set — from the peer network so the ledger can be rebuilt deterministically from a known parent. It sits alongside `SkipListAcquire` as one of two concrete subtask types managed by `LedgerReplayTask` and, at a higher level, by `LedgerReplayer`.

## Position in the Replay Architecture

The replay subsystem exists to let a node reconstruct historical ledgers without downloading their full SHAMap state. Instead of fetching every account object, the node fetches only the transactions that changed the ledger, replays them against an already-validated parent, and derives the child. `LedgerDeltaAcquire` handles precisely the network-fetch phase: it collects and stores the raw ingredients (header + transactions) and then later, when `tryBuild()` is called with the parent ledger, drives the actual replay through `buildLedger()`.

## Lifecycle

The object moves through a clear two-phase sequence. First, `init(numPeers)` starts the timer loop (inherited from `TimeoutCounter`) and calls `trigger()` to dispatch requests. `trigger()` always checks `LedgerMaster::getLedgerByHash()` first — if the ledger is already present locally the task marks itself complete immediately and notifies without touching the network.

When data arrives from a peer (validated externally before being passed in), `processData()` builds a header-only `replayTemp_` ledger — a lightweight `Ledger` object constructed from just the `LedgerHeader` and the current `Rules`. Storing transactions in `orderedTxns_` (keyed by transaction index) alongside this skeleton is sufficient because `buildLedger()` needs the parent state and a replay descriptor, not a pre-existing SHAMap.

The second phase is `tryBuild()`, called by `LedgerReplayTask` once the parent ledger is in hand. It asserts that `parent->seq() + 1 == replayTemp_->seq()` and that the parent hash matches `replayTemp_->header().parentHash`, then constructs a `LedgerReplay` object and calls `buildLedger()`. If the resulting ledger's hash matches `hash_`, the task succeeds and `onLedgerBuilt()` is triggered; otherwise the task is marked failed and throws `std::runtime_error`, signalling a data corruption or malicious peer.

## Peer Selection and Fallback

The primary fetch strategy uses `peerSet_->addPeers()` to select peers that both support `ProtocolFeature::LedgerReplay` and claim to hold the target ledger, then sends a `TMReplayDeltaRequest`. If the `PeerSet` repeatedly returns peers that lack the feature, `noFeaturePeerCount` increments and once it reaches `MAX_NO_FEATURE_PEER_COUNT` (2), `fallBack_` is set. In fallback mode `trigger()` abandons the replay-specific protocol and calls `inboundLedgers_.acquire()` with `InboundLedger::Reason::GENERIC`, falling back to the traditional full-ledger acquisition path. The timer interval is simultaneously widened from `SUB_TASK_TIMEOUT` (250 ms) to `SUB_TASK_FALLBACK_TIMEOUT` (1000 ms) to give the heavier acquisition time to complete. This graceful degradation means a node that cannot find replay-capable peers can still fill gaps in its history, just less efficiently.

## Timeout Handling

`onTimer()` is called by `TimeoutCounter`'s async timer loop on each expiry. If `timeouts_` exceeds `SUB_TASK_MAX_TIMEOUTS` (10) the task is marked failed and `notify()` is called; otherwise `trigger(1, sl)` attempts a fresh single-peer request. The timeout count thus acts as a retry budget: the task will keep probing the network roughly every 250 ms for up to about 2.5 seconds before giving up.

## Callback and Notification Pattern

Callers (in practice, `LedgerReplayTask` instances) register interest via `addDataCallback(reason, cb)`. The `reason` parameter feeds into `reasons_` — a `std::set<InboundLedger::Reason>` that controls post-build ledger processing. The callback itself is pushed into `dataReadyCallbacks_`. Because a `LedgerDeltaAcquire` may serve multiple `LedgerReplayTask` instances that were created for different purposes (e.g., once for `GENERIC` catch-up and once for a second task added later), `addDataCallback()` correctly handles the already-done case: if the task is already complete when a new callback is registered, `notify()` is called immediately.

`notify()` uses a swap-then-unlock pattern: it swaps `dataReadyCallbacks_` into a local vector, releases the mutex, then invokes each callback, then relocks. This avoids calling arbitrary external code while holding the lock — a classical deadlock-avoidance technique.

## Post-Build Processing

`onLedgerBuilt()` enqueues a `jtREPLAY_TASK` job that iterates the collected reasons. For `InboundLedger::Reason::GENERIC`, it calls `LedgerMaster::storeLedger()`. Only on the **first** build (i.e., not when a new reason is added to an already-built ledger) does it also call `LedgerMaster::tryAdvance()`, triggering the ledger advancement machinery. This distinction prevents duplicate advancement attempts when the same delta is reused for a new reason.

## Thread Safety

All mutable state is guarded by `TimeoutCounter::mtx_`, a `std::recursive_mutex`. Every public method acquires `ScopedLockType sl(mtx_)` before touching state. Private methods that participate in a call chain take the lock by reference (`ScopedLockType& sl`) as a convention that communicates "caller already holds the lock." The `recursive_mutex` permits `trigger()` to be called both from `init()` and from `onTimer()` — both of which already hold the lock — without deadlocking.

## Design Notes

The two-stage representation (`replayTemp_` for the header-skeleton, `fullLedger_` for the fully built ledger) separates concerns cleanly: data arrival and ledger construction are decoupled so that the parent ledger does not need to be available at the moment the delta data arrives from peers. The friend declarations for `LedgerReplayTask` (asserts only) and `test::LedgerReplayClient` are deliberately narrow — the former needs internal visibility only for debug invariant checks, and the test client needs direct state inspection without going through the public API.