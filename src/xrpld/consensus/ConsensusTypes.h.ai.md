# `ConsensusTypes.h` — Core Vocabulary for the XRPL Consensus Algorithm

This header is the shared type vocabulary for the entire XRPL consensus subsystem. It defines no algorithm logic — that lives in `Consensus.h` — but every piece of consensus state machinery (`ConsensusMode`, `ConsensusPhase`, `ConsensusResult`, etc.) originates here and is consumed throughout the round lifecycle. Reading this file is effectively reading the state-machine skeleton of the consensus protocol.

## `ConsensusMode` — How a Node Participates

```
proposing               observing
   \                       /
    \---> wrongLedger <---/
               ^
               |
               v
         switchedLedger
```

A node enters each round in one of two initial states: `proposing` (actively broadcasting its position) or `observing` (listening but silent). If at any point the node detects that it is building on the wrong prior ledger, it transitions to `wrongLedger` and tries to acquire the correct one. Once it successfully switches, it becomes `switchedLedger`.

The critical design choice is that `switchedLedger` is a distinct mode rather than just resetting to `observing`. It carries the semantic history — "we caught up mid-round" — which `Consensus.h` uses when computing close times: code at the close-ledger transition explicitly guards against `wrongLedger` mode when deciding whether the previous ledger's close time is authoritative. `switchedLedger` behaves like `observing` in most voting logic, but the mode label is preserved throughout the round so diagnostics, JSON snapshots, and close-time calculations can account for the recovery.

The `MonitoredMode` helper inside `Consensus.h` wraps `ConsensusMode` to ensure `Adaptor::onModeChange()` is always called when the mode transitions, making silent state changes structurally impossible.

## `ConsensusPhase` — Where a Round Is Right Now

```
      "close"             "accept"
 open ------- > establish ---------> accepted
   ^               |                    |
   |---------------|                    |
   ^                     "startRound"   |
   |------------------------------------|
```

Three phases govern the coarse structure of a single ledger round. The `open` phase is the accumulation window: transactions arrive but no position has been declared. `establish` begins when the node closes its open ledger and starts exchanging proposals. `accepted` is the quiescent state where the ledger has been committed and the node waits for `startRound` to kick off the next cycle.

The unusual path — going back to `open` mid-`establish` — happens inside `Consensus::handleWrongLedger`. Rather than aborting and starting fresh from outside, the consensus engine internally re-enters the open phase on the correct ledger, preserving the surrounding state. `ConsensusPhase` guards almost every major entry point in `Consensus.h`: `timerEntry`, `gotTxSet`, and `peerProposal` all short-circuit immediately if the phase is `accepted`, preventing stale work from contaminating the next round.

## `ConsensusTimer` — Dual-Mode Elapsed Time

`ConsensusTimer` is a small class with two distinct `tick()` overloads. The wall-clock overload computes `duration_cast<milliseconds>(tp - start_)` from a `steady_clock::time_point`, giving real elapsed time. The fixed-increment overload accumulates a caller-provided `milliseconds` delta, enabling deterministic simulation in unit tests without mocking the system clock. Both methods update the same `dur_` field, so `read()` is always valid regardless of which variant was used. This is the timing substrate for `ConsensusResult::roundTime`, which `Consensus.h` uses to record how long the `establish` phase took and feed into the `prevRoundTime_` heuristic for the next round's timeout.

## `ConsensusCloseTimes` — Distributed Clock Drift Tracking

Each peer's initial consensus proposal includes that peer's view of when the ledger closed. `ConsensusCloseTimes` collects those views: `peers` is a `std::map<NetClock::time_point, int>` (a histogram of proposed close times), and `self` holds the local node's own estimate. The choice of `std::map` over `hash_map` is deliberate — the comment says "keep ordered for predictable traverse." During close-time consensus resolution, the engine iterates this map in ascending time order to find the most agreed-upon close time bucket, so deterministic ordering matters for reproducibility across nodes. `rawCloseTimes_` in `Consensus.h` is the live instance populated during the `establish` phase.

## `ConsensusState` — The Outcome of Calling `checkConsensus`

`ConsensusState` is a four-value enum that captures every meaningful outcome from the `checkConsensus` free function:

- `No` — insufficient agreement has been reached yet.
- `Yes` — the local node and the network agree on the transaction set.
- `MovedOn` — enough of the network has finished the round that the local node should accept whatever the network decided, even if it didn't directly achieve agreement. This handles slow nodes that fall behind.
- `Expired` — the consensus time limit has hard-expired, forcing acceptance to prevent indefinite stalls.

The distinction between `MovedOn` and `Expired` matters operationally: `MovedOn` is normal network dynamics; `Expired` is a safety valve triggered only when the round has gone on far too long, potentially with stalled disputed transactions. Both cause `phase_` to transition to `accepted`, but they are logged and recorded differently in `ConsensusResult::state`.

## `ConsensusResult<Traits>` — The Aggregated Round Outcome

`ConsensusResult` is a policy-parameterized struct that bundles everything needed to finalize and hand off a consensus round. It is instantiated once per round during `closeLedger` and lives in `Consensus::result_` as a `std::optional<Result>`.

Its constructor enforces a hard invariant via `XRPL_ASSERT`:

```cpp
XRPL_ASSERT(txns.id() == position.position(), "xrpl::ConsensusResult : valid inputs");
```

The transaction set's ID must match the proposal's stated position. This is the core consistency guarantee: the node's declared position is always a commitment to a specific transaction set, not an approximate label. Any construction path that violates this cannot proceed.

The `disputes` map holds the live `DisputedTx` objects — only transactions where peers disagree. The `compares` set is a work-avoidance cache: when the engine processes a new peer transaction set, it checks `compares` first to avoid recomputing disputes for sets already processed. This is especially important during `establish`, where multiple peers may share the same transaction set ID. The `proposers` field snapshots how many peers participated, feeding into the next round's threshold calculations via `prevProposers_`.

Together, these types form a tight vocabulary: `ConsensusPhase` and `ConsensusMode` describe where the engine is and how it participates; `ConsensusTimer` and `ConsensusCloseTimes` track timing evidence; `ConsensusState` records what the engine decided; and `ConsensusResult` packages the conclusions for handoff to the application layer via `Adaptor::onAccept`.