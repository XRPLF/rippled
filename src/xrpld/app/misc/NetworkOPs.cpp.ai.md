# `src/xrpld/app/misc/NetworkOPs.cpp`

## Role and Purpose

`NetworkOPs.cpp` is one of the largest and most central files in the rippled server. It contains the complete implementation of `NetworkOPsImp`, which realizes the `NetworkOPs` abstract interface. The class acts as the primary coordinator between the consensus engine, the ledger system, the peer overlay, the transaction processing pipeline, and the real-time subscription infrastructure that clients use to observe ledger activity. Almost every major subsystem in the server routes through this class at some point during normal operation.

## `NetworkOPsImp`: Core Implementation Class

`NetworkOPsImp` is a `final` class defined within the `xrpl` namespace. It is constructed via the free function `make_NetworkOPs()` and is never instantiated directly by the rest of the codebase. The constructor takes an extensive set of dependencies — `ServiceRegistry`, `LedgerMaster`, `JobQueue`, `ValidatorKeys`, `boost::asio::io_context`, and an insight metrics `Collector` — which it wires together. The `ServiceRegistry` reference is used throughout the implementation to reach nearly every other application subsystem.

A notable initialization detail is that if `start_valid` is true (signalling that the node has been configured to skip initial sync), the operating mode starts as `OperatingMode::FULL` and `minPeerCount_` is set to zero. This matters for validator nodes that already have the full ledger history and need to begin participating in consensus immediately.

## Transaction Processing Pipeline

The file implements a sophisticated two-path transaction pipeline controlled by three primitives: a `std::mutex` (`mMutex`), a `std::condition_variable` (`mCond`), and a `DispatchState` enum with values `none`, `scheduled`, and `running`.

**Local (synchronous) path** — `doTransactionSync()`: Used for transactions submitted by a directly connected RPC client. The calling thread joins the batch, then blocks on `mCond` until the batch containing its transaction completes. The retry loop in `doTransactionSyncBatch()` continues looping as long as `transaction->getApplying()` is still set, meaning the calling thread will wait through multiple batch cycles if the batch is already running when it arrives.

**Remote (asynchronous) path** — `doTransactionAsync()`: Used for peer-relayed transactions. The transaction is enqueued and a `jtBATCH` job is posted to the `JobQueue`. Fire-and-forget: the submitter returns immediately.

The actual application happens in `apply()`, which swaps the pending `mTransactions` vector under the lock, unlocks, then acquires both the `masterMutex` and `ledgerMaster` mutex using `std::lock()` for deadlock-safe dual acquisition. It calls `TxQ::apply()` for each transaction against the open ledger view. After the batch, it re-acquires the lock, clears the `applying` flag on each transaction, notifies all waiters via `mCond.notify_all()`, and sets `mDispatchState = none`.

A critical design choice here: the batch lock is released before calling into the transaction queue and the open ledger. This avoids holding the batch mutex while performing expensive ledger mutations, but it means the condition variable wait loop must re-check the guard condition, not just wake up once.

After a successful application, each transaction is broadcast to peers via `Overlay::relay()` and, if it was included in the open ledger, `pubProposedTransaction()` is called to notify real-time subscribers.

## Operating Mode State Machine

The server progresses through five `OperatingMode` states: `DISCONNECTED`, `CONNECTED`, `SYNCING`, `TRACKING`, and `FULL`. These modes are stored in `std::atomic<OperatingMode> mMode` for lock-free reads by other threads.

`setMode()` has intentional collapsing logic: attempting to set `CONNECTED` when the validated ledger is less than one minute old automatically promotes to `SYNCING`, and vice versa. This prevents oscillation around the CONNECTED/SYNCING boundary for nodes that are nearly caught up.

Blocking conditions — `amendmentBlocked_` and `unlBlocked_` — are enforced as `std::atomic<bool>` flags. Both `setAmendmentBlocked()` and `setUNLBlocked()` forcibly call `setMode(OperatingMode::CONNECTED)`, preventing the node from advancing to FULL while blocked. The composite `isBlocked()` check returns true if either flag is set. `getServerInfo()` injects machine-readable warning objects into its output when these conditions are active, giving operators structured error data rather than just logs.

## Heartbeat Timer and Consensus Entry Point

`processHeartbeatTimer()` fires on the `ledgerGRANULARITY` interval (typically around 1 second). It is the main entry point into the consensus engine for timer-driven events. Under the master mutex it:

1. Calls `LoadManager::heartbeat()` to track resource usage.
2. Counts connected peers and drops to `DISCONNECTED` if below `minPeerCount_`, returning without advancing consensus. This prevents a partially connected node from mistakenly driving the consensus clock.
3. Transitions from `DISCONNECTED` to `CONNECTED` when peer count recovers.
4. Calls `mConsensus.timerEntry()` with the current network close time to advance the RCL consensus state machine.
5. Detects consensus phase changes and dispatches `reportConsensusStateChange()` for subscription notifications.

The timer re-arms itself unconditionally at the end with another call to `setHeartbeatTimer()`. The `setTimer()` helper wraps the Boost Asio timer in a `ClosureCounter`, ensuring that outstanding async handlers are tracked and joined during `stop()` to prevent use-after-free on teardown.

## Consensus Lifecycle Methods

`beginConsensus()` starts a new consensus round by calling `mConsensus.startRound()` and returns false if the node is not sufficiently connected.

`endConsensus()` is more complex. It first clears stale peer status entries (peers that report the previous LCL as their current ledger), then calls `checkLastClosedLedger()` to determine whether the node agrees with the network on the last closed ledger. This comparison uses both the validation trie and a raw peer census. If the node's LCL disagrees with the network's preferred LCL, `switchLastClosedLedger()` is called — an abnormal "JUMP" path that resets the node's ledger pointer and drops the mode to `CONNECTED`. Assuming consensus on the LCL, `endConsensus()` promotes the operating mode toward `FULL` if the current ledger's close time is sufficiently recent, then chains into `beginConsensus()` for the next round.

`consensusViewChange()` drops FULL or TRACKING nodes to CONNECTED when the consensus view changes, a signal that the current proposal set may be invalid.

## Subscription Infrastructure

`NetworkOPsImp` maintains several distinct subscription maps. The `mStreamMaps` array holds nine `SubMapType` slots indexed by `SubTypes` enum values: `sLedger`, `sManifests`, `sServer`, `sTransactions`, `sRTTransactions`, `sValidations`, `sPeerStatus`, `sConsensusPhase`, and `sBookChanges`. All operations on these maps are serialized by a single `mSubLock` mutex.

Account-level subscriptions (`mSubAccount`, `mSubRTAccount`) map `AccountID → SubMapType`. The dual maps separate validated-transaction notifications from real-time (proposed) notifications, since some clients want only confirmed results while others want speculative feed.

The account-history subscription system (`mSubAccountHistory`, `addAccountHistoryJob()`) is a more complex flow: on subscribe, a background `jtCLIENT_ACCT_HIST` job is posted that pages backward through the `RelationalDatabase` (currently SQLite only) to replay historical transactions in chronological order, while the forward live stream catches new events simultaneously. The `SubAccountHistoryInfoWeak` struct holds a `weak_ptr` to the `InfoSub` sink so that if the client disconnects mid-stream, the job detects the expired pointer and self-terminates cleanly.

All publisher methods (`pubLedger`, `pubValidatedTransaction`, `pubValidation`, etc.) use the same idiom: iterate the relevant stream map, attempt to lock each `weak_ptr<InfoSub>`, send to live subscribers, and erase expired entries inline. This lazy GC avoids a separate cleanup pass but means subscriber lifetime is only truly observed during publication.

## `StateAccounting` and Metrics

The inner `StateAccounting` class records cumulative microseconds and transition counts for each operating mode using an array of `Counters`. It captures the time of the last transition (`start_`) and records the first-ever entry into `FULL` mode as `initialSyncUs_`, giving operators a metric for cold-start sync time. The `StateAccounting::json()` method adds a live estimate of time-in-current-state by computing `steady_clock::now() - start_` and appending it to the current-mode counter before formatting the JSON.

The `Stats` struct, populated by the `collect_metrics()` hook, maps these counters to `beast::insight::Gauge` gauges that flow into the metrics infrastructure (Prometheus, StatsD, etc.). This hook-based design means the metrics system pulls data only when it polls, rather than pushing on every state change.

## `ServerFeeSummary` and Fee Change Detection

`ServerFeeSummary` captures a snapshot of `loadFactorServer`, `loadBaseServer`, `baseFee`, and optional `TxQ::Metrics` escalation fields. `reportFeeChange()` compares the current summary against the cached `mLastFeeSummary` and, if anything changed, posts a `jtCLIENT_FEE_CHANGE` job that calls `pubServer()`. This ensures subscribers to the `server` stream receive fee updates promptly without constant polling, and avoids spurious notifications when fees are stable.

## `getBookPage` and Order Book Queries

`getBookPage()` iterates the ledger's offer directory sorted by quality (price ratio). For each offer it computes the effective available funds by querying `accountHolds()` once per offer owner (caching subsequent results in `umBalance`) and then applying the currency issuer's transfer fee. If the owner's balance covers the full offer, `taker_gets_funded` is omitted; if not, the funded amounts are annotated separately so clients can display partially-funded orders correctly. The `#ifndef USE_NEW_BOOK_PAGE` guard reveals there is a dead alternative implementation using an `OrderBookIterator`, noted to have poor performance, which has been compiled out but preserved for reference.

## Construction via `make_NetworkOPs`

The `make_NetworkOPs()` factory function returns a `std::unique_ptr<NetworkOPs>` backed by `NetworkOPsImp`. The implementation exposes no public constructor, keeping the concrete class fully hidden from callers. This allows the subsystem to be replaced in tests by substituting a different `NetworkOPs` implementation without changing any call sites.