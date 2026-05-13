# Consensus

Template-based state machine in `Consensus.h` parameterized by an `Adaptor` (production: `RCLConsensus`). Three phases: `open → establish → accepted`. Four modes: `proposing`, `observing`, `wrongLedger`, `switchedLedger`. Header-only because of templating; policy decisions (`shouldCloseLedger`, `checkConsensus`, `checkConsensusReached`) live as free functions in `Consensus.cpp` for independent testability.

## Architecture

The consensus engine is fully decoupled from XRPL types via the `Adaptor` template parameter. `Adaptor` provides four type aliases (`Ledger_t`, `TxSet_t`, `NodeID_t`, `PeerPosition_t`) plus callbacks (`onClose`, `onAccept`, `onForceAccept`, `onModeChange`) and queries (`proposersValidated`, `proposersFinished`, `getPrevLedger`). Networking is hooked via `propose()` and three `share()` overloads (position, tx set, individual tx).

The engine itself has no thread or timer — it is driven externally by `timerEntry()` calls. Thread safety is the caller's responsibility.

## Key Invariants

- A ledger cannot close until the previous ledger reaches consensus AND (has transactions OR close time reached)
- Proposals must have strictly increasing sequence numbers per peer; stale proposals are silently dropped
- `ConsensusResult` constructor asserts `txns.id() == position.position()` — a node's declared position is always a commitment to a specific tx set
- The Avalanche state machine progressively raises consensus thresholds over time (`init → mid → late → stuck`) to force convergence
- `minCONSENSUS_PCT = 80` is the baseline for `checkConsensus`; timing: `ledgerMIN_CONSENSUS = 1950ms`, `ledgerMAX_CONSENSUS = 15s`, `ledgerABANDON_CONSENSUS = 120s`
- `ledgerMAX_CONSENSUS` must stay below `validationFRESHNESS` so waiting validators aren't mistaken for offline
- Dead nodes (`deadNodes_`) are permanently excluded for the round once they bow out
- LedgerTrie compression invariant: non-root nodes with zero `tipSupport` must have ≥2 children
- `ConsensusResult::disputes` holds only genuinely-differing transactions; `compares` set prevents O(n²) work when multiple peers share a tx set

## Phases and Modes

### Phase transitions (`ConsensusPhase` in `ConsensusTypes.h`)
```
       "close"             "accept"
 open --------> establish ---------> accepted
   ^               |                    |
   |---------------|                    |
   |       "startRound"                 |
   |------------------------------------|
```
Mid-`establish` re-entry to `open` happens inside `handleWrongLedger()` — it preserves surrounding state rather than aborting. `timerEntry`, `gotTxSet`, and `peerProposal` all short-circuit when phase is `accepted`.

### Mode transitions (`ConsensusMode`)
```
proposing               observing
   \                       /
    \---> wrongLedger <---/
               ^
               v
         switchedLedger
```
`switchedLedger` is a distinct mode (not just `observing`) because close-time logic checks the mode label when deciding whether the previous ledger's close time is authoritative. `MonitoredMode` inner class wraps the enum to make silent mode changes structurally impossible — every `set()` calls `adaptor_.onModeChange(before, after)`.

## Phase Logic

### Open phase
`shouldCloseLedger()` is called per timer tick. Priority order (`Consensus.cpp`):
1. Sanity bounds — close immediately if `prevRoundTime` or `timeSincePrevClose` outside `[-1s, 10min]`
2. Majority closed — close if `proposersClosed + proposersValidated > prevProposers / 2`
3. Idle case — only close on `timeSincePrevClose >= ledgerIDLE_INTERVAL` (15s) when no transactions
4. Minimum open time — never close before `ledgerMIN_CLOSE` (2s)
5. Rate limit — block close if `openTime < prevRoundTime / 2` (prevents fast node from outrunning slower validators)

Close-time reference: if mode is `wrongLedger` or close-time wasn't agreed, use internal `prevCloseTime_` rather than the ledger's recorded close time.

### Establish phase
Per tick: `updateOurPositions()` → `shouldPause()` → `haveConsensus()`. `ledgerMIN_CONSENSUS` is enforced before any position updates. `updateOurPositions()`:
- Prunes stale peer proposals (older than `proposeFRESHNESS` = 20s)
- Calls `dispute.updateVote(convergePercent_, ...)` on each `DisputedTx`
- Rebuilds the `MutableTxSet` if any vote flipped, re-shares + re-proposes

`shouldPause()` uses a 5-phase cycle (0–4) keyed off `(ahead - 1) % 5`. Each phase requires progressively more validators current; phase 4 requires all. This cycles to avoid any single threshold being universally right.

### checkConsensus outcomes (`ConsensusState` in `ConsensusTypes.h`)
- `No` — insufficient agreement
- `Yes` — local + network agree on tx set (80% with self counted, via `proposing` flag in `checkConsensusReached`)
- `MovedOn` — 80% of peers finished without us (self not counted); we lost the race
- `Expired` — abandoned after `prevAgreeTime * ledgerABANDON_CONSENSUS_FACTOR` (factor=10), clamped to `[ledgerMAX_CONSENSUS, ledgerABANDON_CONSENSUS]`

The zero-peer case in `checkConsensusReached` deliberately refuses consensus until `reachedMax` — prevents premature self-close on a network slow to deliver proposals. The `stalled` case bypasses the percentage check entirely; when all disputed transactions have clear supermajority agreement either way, network commits immediately.

## Avalanche Voting

Four states defined in `ConsensusParms.h` as `std::map<AvalancheState, AvalancheCutoff>` (data-driven, not switch — supports hypothetical loops):

| State   | Time threshold (% of prior round) | Required yes-vote | Next   |
|---------|-----------------------------------|-------------------|--------|
| `init`  | 0%                                | 50%               | `mid`  |
| `mid`   | 50%                               | 65%               | `late` |
| `late`  | 85%                               | 70%               | `stuck`|
| `stuck` | 200%                              | 95%               | `stuck`|

`getNeededWeight()` returns `(consensusPct, optional<nextState>)`; caller does the actual state update. `avMIN_ROUNDS` prevents premature escalation on clock jitter; `avalancheCounter_` resets to zero on every state transition.

`DisputedTx::updateVote()` behaves asymmetrically:
- Proposing: `weight = (yays_*100 + (ourVote_?100:0)) / (nays_+yays_+1)`; `newPosition = weight > requiredPct`
- Not proposing: `newPosition = yays_ > nays_`, `weight = -1`. Observer never distorts proposers' weighted vote.

`DisputedTx` uses `boost::container::flat_map<NodeID_t, bool>` for peer votes (cache-friendly for small sets), pre-reserved to `numPeers`. `yays_` and `nays_` counters allow O(1) percentage computation without scanning the map. `setVote()` returns `true` on any change (including a new vote), which feeds `peerUnchangedCounter_` tracking.

Stall detection (`DisputedTx::stalled`) — all must hold:
1. `nextCutoff.consensusTime <= currentCutoff.consensusTime` (terminal `stuck` state)
2. ≥ `avMIN_ROUNDS` rounds in state
3. `peersUnchanged >= avSTALLED_ROUNDS` **OR** `currentVoteCounter_ >= avSTALLED_ROUNDS` (OR not AND — defends against a peer flip-flopping to reset the counter)
4. Vote split exceeds `minCONSENSUS_PCT` (80%) in either direction

`peerUnchangedCounter_` resets to 0 on any peer vote change in `updateDisputes()`. Close-time consensus uses a separate threshold `avCT_CONSENSUS_PCT` (75%) — close-time agreement is a simpler majority, not a multi-round ratchet.

## Proposals (`ConsensusProposal.h`)

Five fields hashed for signing: `HashPrefix::proposal`, `proposeSeq_`, `closeTime_`, `prevLedgerID_`, `position_`. Hash is `mutable std::optional<uint256>`, lazily computed; `changePosition()` and `bowOut()` must call `signingHash_.reset()` before mutating.

Sequence sentinels:
- `seqJoin = 0` — initial proposal (`isInitial()`); `ConsensusCloseTimes` collects these for clock-drift measurement
- `seqLeave = 0xffffffff` — bow-out; `changePosition()` refuses to increment past this

`seenTime()` is local wall-clock time when last updated, NOT `closeTime_` (the proposer's estimate of when the ledger should close in `NetClock`). Don't conflate them. `isStale(cutoff)` uses `seenTime()`. `operator==` includes `seenTime()`, so logically-identical proposals seen at different times don't compare equal.

The production wrapper `RCLCxPeerPos` (in `app/consensus/`) adds cryptographic signature and public key for network propagation. Template parameters `(NodeID_t, LedgerID_t, Position_t)` allow unit-test instantiation over simple integer types.

## `ConsensusTypes.h` — Vocabulary Types

- **`ConsensusTimer`**: dual `tick()` overloads — wall-clock (`steady_clock::time_point`) and fixed-increment (for deterministic simulation). Both update `dur_`; `read()` always valid. Backing `roundTime` in `ConsensusResult` feeds `prevRoundTime_`.
- **`ConsensusCloseTimes`**: `peers` is `std::map<NetClock::time_point, int>` (ordered for deterministic traversal when resolving close time); `self` is local estimate. Collects initial (`seqJoin`) proposals for clock-drift measurement.
- **`ConsensusResult`**: instantiated once per round by `closeLedger`, lives in `Consensus::result_` as `std::optional`. Holds `disputes`, `compares` work-avoidance set, `proposers` snapshot. `state` field records `ConsensusState` outcome for diagnostics.

## Wrong-Ledger Recovery

At every `timerEntry()`, `checkLedger()` calls `adaptor_.getPrevLedger()`. If diverged, `handleWrongLedger()`:
1. Calls `leaveConsensus()` — broadcasts bow-out, drops to `observing`
2. Clears peer state
3. Calls `playbackProposals()` — replays proposals from `recentPeerPositions_` (capped at 10/peer, stored regardless of ledger ID)
4. If correct ledger acquired: `startRoundInternal()` in `switchedLedger` mode; else: stays in `wrongLedger`

The bounded `recentPeerPositions_` buffer is a deliberate trade-off: small bounded buffer beats dropping proposals during switches. Recovery re-enters `open` phase mid-`establish` via `handleWrongLedger()`, preserving surrounding state.

## Common Bug Patterns

- Proposals referencing a stale `prevLedgerID_` after a ledger switch cause split-brain; always check `newPeerProp.prevLedger() != prevLedgerID_` before processing
- Resetting the consensus timer during `establish` phase causes re-convergence and potential split; timer must only reset on phase transitions
- `DisputedTx::updateVote` changes local vote based on peer pressure; bugs here cause determinism failures across nodes
- `createDisputes()` deduplicates via `result_->compares` set; missing this check creates duplicate disputes that skew vote counts
- The `peerUnchangedCounter_` is reset to 0 when any vote changes; bugs in this counter cause premature consensus declaration
- Forgetting `signingHash_.reset()` before mutating a `ConsensusProposal` returns stale hashes
- Comparing wall-clock `seenTime()` against `NetClock` `closeTime_` is a type-shaped bug waiting to happen
- Two temporal domains in `ConsensusParms`: validation/proposal parms use **NetClock seconds**; consensus-loop timers use **steady-clock milliseconds** — mixing them produces subtle bugs

## Key Code Patterns

### Proposal Validation
```cpp
if (newPeerProp.prevLedger() != prevLedgerID_)
{
    JLOG(j_.debug()) << "Got proposal for " << newPeerProp.prevLedger()
                     << " but we are on " << prevLedgerID_;
    return;
}
```

### Complete Bow-Out Handling
```cpp
if (newPeerProp.isBowOut())
{
    if (result_)
        for (auto& it : result_->disputes)
            it.second.unVote(peerID);
    if (currPeerPositions_.find(peerID) != currPeerPositions_.end())
        currPeerPositions_.erase(peerID);
    deadNodes_.insert(peerID);  // permanently excluded this round
}
```

### CLOG diagnostic pattern
Most methods take `std::unique_ptr<std::stringstream> const& clog = {}`. `CLOG(clog)` macro appends only when non-null — full round trace available without paying formatting cost on the hot path.

## Validations (`Validations.h`)

`Validations<Adaptor>` is templated; production uses `RCLValidationsAdaptor`. Five coordinated structures under one `mutex_`:
- `current_`: most recent per node, fast-path for quorum
- `byLedger_`: aged unordered map keyed by ledger ID
- `bySequence_`: aged unordered map for Byzantine detection
- `trie_`: `LedgerTrie<Ledger>` for preferred-ledger calc
- `acquiring_`: validations waiting on locally-unavailable ledgers

`ValidationParms` windows: `validationCURRENT_WALL=5min`, `validationCURRENT_LOCAL=3min`, `validationCURRENT_EARLY=3min`, `validationSET_EXPIRES=10min`, `validationFRESHNESS=20s` (used only for laggard detection, not staleness). Fields are mutable instance members, not `constexpr` — simulations inject alternate values.

`isCurrent()` checks two clocks independently: signer's wall time and our local steady-clock first-observation time. Arithmetic promotes to signed 64-bit to avoid underflow on untrusted `signTime`.

`SeqEnforcer<Seq>` rejects regressed/duplicate sequences but resets its high-water mark after `validationSET_EXPIRES` with no new validation — long-offline validators can rejoin.

`add()` classification (in order):
- Same seq, different ledger/sign time → `ValStatus::conflicting` (possible Byzantine)
- Same seq + ledger, different cookie → `ValStatus::multiple` (misconfig/duplicate)
- Otherwise → `ValStatus::badSeq`

All trie queries go through `withTrie()`, which first flushes stale entries via `current()` then promotes newly-available ledgers via `checkAcquired()`. `lastLedger_` tracks each node's trie contribution so `removeTrie()` can atomically undo before re-inserting.

`getPreferred(curr)` fallback: trie → `acquiring_` (max waiters) → `nullopt`. Conservative switch rule: if preferred is an immediate child of current working ledger, stay put.

`trustChanged()` iterates `current_` and full `byLedger_` to propagate UNL changes — trie reflects only currently trusted validators.

`setSeqToKeep([low, high))` pins a range against eviction by "touching" entries near expiry. Throttled to once per `(validationSET_EXPIRES - validationFRESHNESS)` window.

## LedgerTrie (`LedgerTrie.h`)

Compressed prefix trie over ledger ancestry — ledger history is treated as a string over the alphabet of ledger IDs. Each `Node` carries a `Span` (half-open `[start_, end_)`), two counters, raw parent pointer, owned children.

- `tipSupport`: validations exactly matching this node's tip
- `branchSupport`: `tipSupport` + sum of descendants' `branchSupport`

Counters propagate up the parent chain on every `insert`/`remove`. Non-root nodes with zero tip and ≤1 child violate the compression invariant and are merged.

`insert()` may do up to two structural ops:
1. Split — extract suffix into new child inheriting children + counts, truncate found node
2. Branch — append new leaf

`remove()` uses `findByLedgerID()` (O(n) exact match), not the prefix-based `find()`.

`getPreferred(largestIssued)` — the algorithmic heart. Walks from root using "preferred by branch": validators with last validation below the current frontier are *uncommitted* (could swing any branch). A branch advances only when `branchSupport` exceeds *uncommitted*, and a child wins only when its `branchSupport` lead over the runner-up exceeds *uncommitted* (with `startID()` tie-break). The strictly-greater-than margin prevents thrashing when validators lag.

`seqSupport: std::map<Seq, uint32_t>` (ordered for in-sequence walk) drives the uncommitted accounting.

`checkInvariants()` does full DFS — used heavily in tests; verifies compression rule, counter consistency, parent links, and `seqSupport` sums.

`Ledger` template contract: cheap copy, `seq()`, `operator[](Seq)` returning `ID{0}` for unknowns, `MakeGenesis{}` tag, free `mismatch(Ledger,Ledger)`. Unique history invariant: agreement on any ancestor ID implies agreement on all earlier ancestors.

`SpanTip<Ledger>` is the return type of `getPreferred()` — a lightweight struct with the tip's seq, ID, and a ledger copy for ancestor lookups. `Span::diff()` delegates to `mismatch()` to find first divergence point.

## Amendments

- 80% validator support for 2 weeks to enable; tracked via `AmendmentTable` with `amendmentMap_`
- New amendments: add to `features.macro` with `XRPL_FEATURE`/`XRPL_FIX`, increment `numFeatures` in `Feature.h`
- Unsupported enabled amendment blocks the server (`setAmendmentBlocked`); no mechanism to disable/revoke
- Voting happens each consensus round in `doVoting`; votes persisted in `FeatureVotes` SQLite table
- `fixAmendmentMajorityCalc` changed the threshold calculation; check which applies

## UNL and Negative UNL

- N-UNL temporarily disables unreliable validators (max 25% of UNL: `negativeUNLMaxListed = 0.25`)
- Scoring via `buildScoreTable` over recent ledger history; low watermark 50% = disable candidate, high 80% = re-enable
- Candidate selection deterministic via previous ledger hash as randomizing pad
- `newValidatorDisableSkip = FLAG_LEDGER_INTERVAL * 2` prevents disabling newly joined validators

## Transaction Ordering

- `CanonicalTXSet`: salted account key (XOR random salt) → seq proxy → tx ID. Salt prevents ordering manipulation
- `TxQ` uses `OrderCandidates`: higher fee level first, then `txID XOR parentHash` tiebreaker
- Per-account limit `maximumTxnPerAccount`; blocked transactions held until blocker resolves

## Key Files

- `src/xrpld/consensus/Consensus.h` — state machine (header-only template)
- `src/xrpld/consensus/Consensus.cpp` — free policy functions (`shouldCloseLedger`, `checkConsensus`, `checkConsensusReached`)
- `src/xrpld/consensus/ConsensusParms.h` — all numeric thresholds; dual-clock (NetClock seconds vs steady ms)
- `src/xrpld/consensus/ConsensusTypes.h` — `ConsensusMode`, `ConsensusPhase`, `ConsensusState`, `ConsensusTimer`, `ConsensusCloseTimes`, `ConsensusResult`
- `src/xrpld/consensus/ConsensusProposal.h` — proposal record with sequence protocol and lazy signing hash
- `src/xrpld/consensus/DisputedTx.h` — per-tx avalanche voting and stall detection
- `src/xrpld/consensus/Validations.h` — validation tracking, indexing, trie integration
- `src/xrpld/consensus/LedgerTrie.h` — compressed ancestry trie for preferred-ledger calc
- `src/xrpld/app/consensus/RCLConsensus.cpp` — XRPL `Adaptor` implementation
- `src/xrpld/app/misc/detail/AmendmentTable.cpp` — amendment voting logic
- `src/xrpld/app/misc/NegativeUNLVote.cpp` — N-UNL voting
- `src/xrpld/app/misc/CanonicalTXSet.h` — tx ordering
