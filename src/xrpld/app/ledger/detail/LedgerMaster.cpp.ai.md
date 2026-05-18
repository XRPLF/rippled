# `src/xrpld/app/ledger/detail/LedgerMaster.cpp`

## Role in the System

`LedgerMaster` is the single most central bookkeeping object in a running rippled node. It owns the authoritative answers to several concurrent questions: *Which ledger should new transactions enter?* *What is the last ledger we believe the network validated?* *What is the last ledger we have published to clients?* *What history do we hold, and what are we still acquiring?* Everything downstream — consensus, path-finding, the RPC layer, the overlay — defers to `LedgerMaster` for these answers.

The implementation file is ~2170 lines. At that scale it is better understood as a state machine with five interlocking concerns: ledger state management, validation advancement, ledger publication, history backfill, and fetch-pack serving. Each is described below.

---

## Ledger State Hierarchy

The class maintains four distinct views of ledger state:

| Pointer | Meaning |
|---|---|
| `app_.getOpenLedger().current()` | Mutable ledger accepting new transactions |
| `mClosedLedger` | Most recently consensus-closed ledger (not yet validated) |
| `mValidLedger` | Highest-sequence ledger with a quorum of trusted validations |
| `mPubLedger` | Last ledger announced to clients; may lag `mValidLedger` |

`mClosedLedger` and `mValidLedger` are `LedgerHolder` objects — thin wrappers that enforce immutability (via `isImmutable()` check on set) and provide mutex-guarded reads. The publication sequence number `mPubLedgerSeq` and the validation timestamp `mValidLedgerSign` are `std::atomic` so that `getPublishedLedgerAge()` and `getValidatedLedgerAge()` can be called from any thread without taking `m_mutex`.

A non-obvious invariant: `mValidLedgerSeq` can advance ahead of `mPubLedgerSeq` whenever the node receives enough validations but cannot yet acquire all intervening ledgers. `findNewLedgersToPublish` bridges that gap; if the gap exceeds `MAX_LEDGER_GAP` (100), the system skips forward directly to the validated ledger rather than trying to reconstruct every intermediate ledger.

---

## Validation Advancement

The critical path runs: **`consensusBuilt` → `checkAccept` → `setValidLedger` → `tryAdvance`**.

`consensusBuilt()` is called by the consensus engine when a locally-built ledger is finalized. It hands the ledger to `checkAccept(ledger)`, which applies three filters before promoting the ledger to validated status:

1. **`canBeCurrent`** enforces Byzantine defense: it rejects any ledger whose sequence precedes the last validated ledger, whose parent close time differs from local clock time by more than five minutes, or whose sequence is unreasonably far ahead of the last validated ledger. The maximum future sequence is computed as `validSeq + 10 + elapsed_seconds / 2` — a rough model that assumes at most one ledger per two seconds, with 10 ledgers of slack for clock drift.

2. **Quorum check**: the call to `app_.getValidations().getTrustedForLedger()` fetches trusted validations for the ledger hash, then passes them through `app_.getValidators().negativeUNLFilter()` which excludes validators temporarily suppressed by the negative-UNL mechanism. If the count falls below `getNeededValidations()` (the configured quorum, 0 in standalone mode), `checkAccept` returns early.

3. `setValidLedger()` finalises promotion. It calculates a *median signature time* across validator sign times (not the ledger close time) and stores it in `mValidLedgerSign`. This median is used to detect whether the node's own clock is consistent with the network. It then updates `SHAMapStore`, the amendment table, and `NetworkOPs`. Critically, it checks `app_.getAmendmentTable().hasUnsupportedEnabled()`: if any amendment the current binary does not understand has activated, the node calls `setAmendmentBlocked()`, rendering it inoperative for new transactions.

When consensus succeeds but the resulting ledger has not yet received a quorum (a situation that can happen during network partitions), `consensusBuilt` also scans all *current* trusted validations for any separately-validated ledger that might have crossed the quorum threshold. The inner `valSeq` class (defined locally) is used as a lightweight accumulator to find the highest-sequence ledger with enough votes without constructing separate collections.

---

## Ledger Publication and the Advance Loop

`tryAdvance()` is a throttled dispatcher: it sets `mAdvanceWork = true` and, if no advance thread is already running, submits a `jtADVANCE` job. The actual work happens in `doAdvance()`, which is always called with `m_mutex` held (the `std::unique_lock<std::recursive_mutex>` parameter is a compiler-enforced reminder of this invariant).

`doAdvance()` runs a `do…while(mAdvanceWork)` loop so that work discovered mid-execution reruns immediately. On each iteration it calls `findNewLedgersToPublish()`:

- If new ledgers are ready, it calls `setFullLedger` and `app_.getOPs().pubLedger()` for each, then triggers path-finding work via `newPFWork`.
- If the publish and valid sequences are equal (we are caught up), it looks for missing history using `prevMissing(mCompleteLedgers, pubSeq, earliestSeq)` and calls `fetchForHistory` when `shouldAcquire` permits it. `shouldAcquire` respects three throttles: whether the candidate is within the configured `ledger_history_` window, whether it exceeds the SHAMapStore's `minimumOnline` requirement, and whether it might be the current ledger.

`findNewLedgersToPublish` releases `m_mutex` (via `scope_unlock`) while doing I/O. That requires the function and its caller to be aware that state may change while the lock is dropped; the function therefore re-reads `mValidLedger` at entry and does not rely on stale copies.

When `LEDGER_REPLAY` mode is enabled, `findNewLedgersToPublish` walks backward from the validated ledger through `mLedgerHistory` to narrow the replay gap, then hands the work to `app_.getLedgerReplayer()`.

---

## History Backfill

`fetchForHistory()` resolves a missing sequence number by:

1. Calling `getLedgerHashForHistory()` which looks up the hash via the skip list embedded in the validated ledger's state map, using `mHistLedger` as an alternative reference when the primary validated ledger does not span the target index.
2. Attempting to get the ledger from the in-memory history cache.
3. If not cached, checking whether a prior inbound-ledger acquire for that hash has already permanently failed before dispatching a new `InboundLedgers::acquire`.
4. If the ledger is still unavailable, requesting a *fetch pack* from the best-scoring peer that claims to hold the missing range.

Once a historical ledger is acquired, `fetchForHistory` calls `setFullLedger` and may spawn a `TryFill` job, which walks backward through the relational database (in batches of 500 sequences) to mark an entire contiguous range in `mCompleteLedgers` without re-fetching each ledger individually.

---

## Fetch Packs

A fetch pack is a batch of SHAMap tree nodes bundled together so a peer can acquire a historical ledger in fewer round trips. The two sides of the protocol are both here:

**Requesting** (`getFetchPack`, called from `fetchForHistory`): The node selects the active peer with the highest randomized-but-latency-weighted score that advertises the needed range, then sends a `TMGetObjectByHash` of type `otFETCH_PACK` pointing at the *next* ledger's hash (so the peer can compute which nodes are missing).

**Serving** (`makeFetchPack`): A peer-serving path that builds a response. It respects several rate-limiting gates: request age > 1 second (stale, discard), local load too high, or validated ledger too old. The core loop serializes the ledger header, then calls the file-static `populateFetchPack()` to walk the SHAMap differences between what the requester already has (`have->stateMap()`) and the ledger being packed (`want->stateMap()`). Transaction maps are packed unconditionally with `nullptr` as the "have" map because remote peers are unlikely to have historical transactions. The loop continues to older ledgers until 512 objects have been added or one second of uptime has elapsed.

Received fetch pack data is cached in `fetch_packs_`, a `TaggedCache<uint256, Blob>` with a 45-second TTL and 65536 entries. Retrieval validates the blob's hash before returning it, so a corrupted or substituted entry is silently discarded.

---

## Concurrency Design

Two locks serve distinct purposes:
- `m_mutex` (recursive) guards nearly all mutable state. It is recursive because several public methods call private methods that also lock it.
- `mCompleteLock` (non-recursive) guards only `mCompleteLedgers` (a boost interval set). Using a separate lock prevents `mCompleteLedgers` operations from blocking unrelated state reads.

Atomic members (`mPubLedgerClose`, `mPubLedgerSeq`, `mValidLedgerSign`, `mValidLedgerSeq`, `mBuildingLedgerSeq`) allow hot-path reads without locking. The single `mGotFetchPackThread` atomic flag prevents more than one `GotFetchPack` job from being queued simultaneously.

The `scope_unlock` RAII guard appears throughout `doAdvance` and `findNewLedgersToPublish` to release `m_mutex` around expensive operations (I/O, network sends) while ensuring it is re-acquired when the scope ends, preventing long stalls in the job queue.

---

## Fee Mediation and Upgrade Detection

`checkAccept(ledger)` also serves two secondary roles. First, it collects fee load factors from validations for the ledger and its parent, takes the median, and calls `app_.getFeeTrack().setRemoteFee(fee)` — a mechanism by which the node tracks whether the broader network is under higher fee pressure than local state would indicate.

Second, every 256 ledgers (`ledger->seq() % 256 == 0`), it inspects the `sfServerVersion` field in validator messages to detect whether 60% or more of xrpld validators are running a newer binary. If so, it emits an upgrade warning to stderr and the error log. This check is throttled to at most once per week by `upgradeWarningPrevTime_`.

---

## Metrics

The nested `Stats` struct registers two Graphite-style gauges — `LedgerMaster/Validated_Ledger_Age` and `LedgerMaster/Published_Ledger_Age` — through the beast insight collector. These are polled via the `hook` callback connected to `collect_metrics()`, providing continuous observability into how stale the node's ledger views are.