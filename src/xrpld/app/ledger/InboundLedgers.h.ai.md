# `InboundLedgers.h` — Ledger Acquisition Manager Interface

This header defines the abstract interface governing how a rippled node fetches ledgers it does not yet hold locally. Whenever a node is catching up from behind, handling a consensus round for which it lacks the prior ledger, or backfilling historical data, it must request and reassemble ledger data from network peers. `InboundLedgers` is the central coordinator for that process: it owns the map of in-flight `InboundLedger` acquisitions, tracks recently-failed attempts, and exposes the lifecycle hooks that the rest of the application uses to interact with active fetches.

## Two Acquisition Entry Points

The interface deliberately offers two paths for requesting a ledger: `acquire()` and `acquireAsync()`.

`acquire()` is designed for callers that may need an immediate answer. It looks up or creates an `InboundLedger` entry and — if that ledger happens to already be complete — returns a `shared_ptr<Ledger const>` right away. If acquisition is still in progress or has failed, it returns `nullptr`. The implementation wraps the entire operation with `perf::measureDurationAndLog`, flagging calls that take longer than 500ms as slow.

`acquireAsync()` is explicitly for callers already executing on the Job Queue. It adds the hash to a `pendingAcquires_` set under its own mutex, then calls through to `acquire()`. The set acts as a deduplication guard: if the same hash is already pending, it returns without doing anything. This matters because Job Queue tasks can be dispatched concurrently, and without this gate they could create redundant `InboundLedger` objects racing to initialize. The header comment notes that it may eventually be possible to migrate all callers to this path — an acknowledgment that the two-path design is partly historical and partly a pragmatic performance split.

## Find vs. Acquire

`find()` is separate from `acquire()` by design: it looks up an existing in-flight acquisition without triggering a new one. This prevents callers that merely want to check progress — such as routing incoming peer data to the right `InboundLedger` — from accidentally spawning new network requests.

## Routing Incoming Peer Data

`gotLedgerData()` is called by the networking layer when a peer delivers a `TMLedgerData` message. The implementation uses `find()` to locate the matching in-flight acquisition and calls `InboundLedger::gotData()` on it. If `gotData()` returns true (indicating the data is fresh and processing hasn't been dispatched yet), a `jtLEDGER_DATA` job is enqueued to run `InboundLedger::runData()` on the Job Queue.

The more interesting case is when `find()` returns nothing — the acquisition has already completed or been swept. Rather than discarding the late data entirely, `gotLedgerData()` checks whether the packet carries state node data (`liAS_NODE`). If so, it schedules a call to `gotStaleData()`, which parses each `SHAMapTreeNode` from the wire format, re-serializes it in prefix format, and stashes it in the `LedgerMaster`'s fetch pack cache. The reasoning: the node already paid the bandwidth cost to receive the data, so it might as well be cached in case a future acquisition or historical backfill can use it. The existing `VFALCO TODO` in the header flags the direct `Peer` dependency in `gotLedgerData()` as a design smell — ideally this layer would operate only on protocol messages, not peer handles.

## Failure Tracking

`logFailure()`, `isFailure()`, and `clearFailures()` exist because the network cannot always deliver every requested ledger. Without failure bookkeeping the system would repeatedly retry undeliverable hashes. Internally, `InboundLedgersImp` uses a `beast::aged_map<uint256, uint32_t>` named `mRecentFailures`. Entries expire after `kReacquireInterval` (five minutes), so a failed hash will eventually become eligible for retry. `isFailure()` calls `beast::expire()` on the map before doing its lookup, meaning stale failures are pruned lazily on each check rather than requiring a separate timer-driven cleanup pass.

`clearFailures()` also clears `mLedgers` entirely — a notable side effect that makes it a heavier operation than its name suggests, used during shutdown or when a fundamental state reset is needed.

## Fetch Rate Tracking

`fetchRate()` returns the rate of completed historical ledger fetches in fetches-per-minute. The underlying measurement is a `DecayWindow<30>` (a 30-second exponentially-decaying sample), and the value is scaled by 60 to express it per minute. The `onLedgerFetched()` method increments this counter; its comment notes it should only be called for `InboundLedger::Reason::HISTORY`, keeping the metric focused on catch-up throughput rather than consensus-driven fetches.

## Housekeeping

`sweep()` evicts acquisitions that have been idle for more than one minute, collecting them into a local vector before erasing from the map so that their destructors run outside the lock. Entries that were last active in the future (a clock-skew guard) have their timestamps touched forward. The method also calls `beast::expire()` on `mRecentFailures` to prune stale failure records. `stop()` sets a `stopping_` flag that causes all subsequent `acquire()` calls to return immediately, then clears both the ledger map and failure map.

`gotFetchPack()` is called when new fetch pack data becomes available in the `LedgerMaster`. It snapshot-copies the current `mLedgers` map under the lock, then calls `checkLocal()` on every in-flight `InboundLedger` outside the lock, giving each acquisition a chance to satisfy itself from the newly-arrived data without holding the global mutex.

## Factory and Interface Design

`make_InboundLedgers()` is the sole public construction path, returning a `unique_ptr<InboundLedgers>` to the concrete `InboundLedgersImp`. This follows the standard XRPL pattern of pairing an abstract interface with a hidden implementation, enabling test injection via mock implementations and shielding consumers from internal data structure choices. The factory accepts an `Application&` for access to shared subsystems (Job Queue, LedgerMaster, NetworkOPs), a `beast::abstract_clock<steady_clock>` for injectable time (enabling deterministic tests), and a `beast::insight::Collector` for emitting the `ledger_fetches` counter to the configured metrics backend.