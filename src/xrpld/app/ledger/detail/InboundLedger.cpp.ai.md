# `InboundLedger.cpp` — Peer-Sourced Ledger Acquisition Engine

`InboundLedger` is the core state machine responsible for fetching a complete ledger from the peer network. A single validated XRPL ledger consists of three independently verifiable pieces: the ledger header (which commits to the roots of both tries), the transaction SHAMap, and the account-state SHAMap. `InboundLedger` tracks which pieces have arrived, orchestrates requests to peers, and drives retry logic until all three parts are assembled or the attempt is abandoned.

## Inheritance and Lifecycle

`InboundLedger` inherits from `TimeoutCounter`, which provides the asynchronous heartbeat loop: a recurring 3-second timer (`ledgerAcquireTimeout`) fires `onTimer` via the job queue. The subclass uses this to escalate its request strategy. The class also inherits `enable_shared_from_this`, which is essential — it shares itself into closures dispatched to the job queue, preventing premature destruction while work is in flight.

Construction is deliberately cheap: the object is initialized, a `touch()` sets the last-activity timestamp, and nothing else happens synchronously. `init()` — called separately under the `InboundLedgers` collection lock — does the real setup work. The caller holds the collection lock on entry; `init()` immediately acquires the internal `mtx_` and then releases the collection lock, establishing a clear ordering and minimizing the time the collection is locked.

## Local Store First: `tryDB`

Before sending any network request, `tryDB` attempts to satisfy the acquisition entirely from local data. It checks both the `NodeStore` database and the `LedgerMaster`'s fetch pack cache (compressed batches of node data propagated during consensus). The rationale for checking both is that a ledger's nodes may have arrived by different paths: the header might be in the main node store while its SHAMap nodes arrived in a fetch pack.

A subtle cross-database case is handled: if the ledger header is found in a source DB that differs from the ledger's own family DB (which happens when shard databases are in play), the header blob is copied to the correct destination before proceeding. This avoids later cache misses during state map traversal.

`tryDB` sets `mHaveHeader`, `mHaveTransactions`, and `mHaveState` individually. For the SHAMaps, it first fetches the root node via `fetchRoot`, then probes with `neededTxHashes`/`neededStateHashes` asking for a single missing hash — if the answer is empty, the entire map is locally available without fetching every node one by one. A ledger with an empty transaction hash (`txHash.isZero()`) correctly short-circuits to `mHaveTransactions = true`. An empty account hash, however, is treated as a fatal error and sets `failed_`, since every valid ledger must have a non-empty account state.

## Peer Selection and Request Triggering

`addPeers` delegates to `mPeerSet`, passing two lambdas: a predicate that filters peers to those claiming to have the target ledger (`peer->hasLedger(hash_, mSeq)`), and a callback that triggers requests immediately — except for `Reason::HISTORY` acquisitions where a fetch pack is likely incoming and premature requests waste bandwidth.

`trigger` is the central request dispatcher. It chooses what to request based on which pieces are still missing, in priority order: header first (nothing else can proceed without the root hashes), then account state (the largest map, prioritized because it's the most valuable if the acquisition is abandoned partway), then transaction data. Query depth is adapted to the trigger reason: `TriggerReason::reply` uses depth 1 (or depth 2 for high-latency peers, since the round trip is already being paid), while `added` and `timeout` use depth 0 — querying blindly into an unknown tree is wasteful.

## Escalation: Aggressive Mode and `filterNodes`

Two escalation mechanisms kick in as timeouts accumulate. First, `mRecentNodes` tracks SHAMap node hashes that were recently requested. `filterNodes` uses `std::stable_partition` to move duplicate nodes to the back, then erases them — preventing the same node from flooding the same peer on successive calls. On a `TriggerReason::timeout` trigger, even duplicate nodes are sent, ensuring stalled acquisitions retry everything. `mRecentNodes` is cleared at the start of each timer tick.

Second, after `ledgerBecomeAggressiveThreshold = 4` timeouts without progress, the code switches protocols entirely: instead of using the SHAMap tree-walk protocol (`TMGetLedger`), it gathers the specific hashes of all missing nodes via `getNeededHashes` and sends a `TMGetObjectByHash` request directly asking for those content-addressed objects. This bypasses the tree traversal when normal traversal is stalling, effectively asking the network "give me these exact bytes" rather than "walk this tree with me."

The `timeouts_` counter after `ledgerTimeoutRetriesMax = 6` terminates the acquisition unconditionally by setting `failed_` and calling `done()`.

## Two-Lock Receive Pipeline

Incoming data takes a path specifically designed to avoid blocking the overlay receive thread. `gotData` is called from the networking layer under a lightweight `mReceivedDataLock` (a plain non-recursive `std::mutex`); it appends to `mReceivedData` and returns `true` exactly once via the `mReceiveDispatched` flag. That single `true` return value signals that the caller needs to dispatch `runData` to the job queue. Subsequent packets accumulate in the queue without dispatching a new job.

`runData` drains the entire pending queue in a loop, processing each queued `TMLedgerData` via `processData`. It uses the `detail::PeerDataCounts` helper — defined in a local `detail` namespace within this file — to track how many useful SHAMap nodes each peer contributed. After processing, `PeerDataCounts::prune()` eliminates peers that returned less than half the maximum useful-node count. `sampleN` then randomly selects at most 6 survivors to receive followup `trigger` calls. This approach rewards productive peers without creating a deterministic ordering that could be gamed, and avoids hammering every peer in the set after every response.

`processData` validates each incoming packet — checking for non-empty node lists, verifying that each node has both an ID and data payload, and charging `Resource::feeMalformedRequest` or `Resource::feeInvalidData` against misbehaving peers — before delegating to `receiveNode`, `takeHeader`, `takeAsRootNode`, or `takeTxRootNode`.

## Completion and `done()`

`done()` uses `mSignaled` to ensure exactly-once semantics — it can be called from multiple code paths (the timer, `trigger`, `receiveNode`). A completed, non-failed ledger is made immutable before being routed by `mReason`:

- `Reason::HISTORY`: notifies `InboundLedgers::onLedgerFetched()` to update the historical fetch rate; does not store via `LedgerMaster` since history backfill has its own pipeline.
- All other reasons: calls `LedgerMaster::storeLedger`.

Crucially, `done()` dispatches an `AcqDone` job rather than calling `checkAccept` and `tryAdvance` inline. This is because `done()` may be called while holding the internal `mtx_` lock, and those `LedgerMaster` operations could attempt to re-enter structures that would deadlock or simply be too expensive for a hot path.

## Fee Invariant

An `XRPL_ASSERT` appears at every completion boundary — in `tryDB`, `init`, and `done` — verifying that any ledger at or after `XRP_LEDGER_EARLIEST_FEES` has a fee settings entry in its state map. This invariant reflects a protocol-level guarantee: after a certain ledger sequence, the fee object must exist. Asserting it at acquisition time catches corruption or bugs in the SHAMap assembly before the ledger propagates into consensus state.

## Destructor and Stale Data Recycling

The destructor forwards any account-state (`liAS_NODE`) packets that were received but not yet processed to `InboundLedgers::gotStaleData`. Account-state nodes are ledger-agnostic in the sense that many of them appear across consecutive ledgers; discarding them on cancellation would waste bandwidth. `gotStaleData` allows the subsystem to apply them to other in-progress acquisitions.