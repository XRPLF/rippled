# `ConsensusParms.h` — Consensus Algorithm Tuning Constants

This header is the single source of truth for every numeric constant that governs the XRP Ledger's consensus algorithm. It lives at the boundary between the abstract consensus engine (`Consensus.h`) and the concrete protocol rules, collecting in one place all the timing windows, percentage thresholds, and state-machine cutoffs that the engine queries on every tick. Nothing here is meant to be altered at runtime — every member is `const`, making the struct effectively a named-constant bundle.

## Two Temporal Domains

The struct explicitly comments on a dual-clock architecture. Validation and proposal parameters (`validationVALID_WALL`, `validationVALID_LOCAL`, `validationVALID_EARLY`, `proposeFRESHNESS`, `proposeINTERVAL`) operate in **NetClock time** at second resolution, because these values are compared against ledger close timestamps that travel over the network and must be meaningful across machines. The remaining consensus-loop timers operate in **millisecond resolution** against an internal monotonic clock, because the engine needs sub-second granularity to decide when to advance its own state. Mixing these up would produce subtle bugs; the comment acts as a firewall reminding callers which domain each constant belongs to.

## Validation and Proposal Windows

`validationVALID_WALL` (5 minutes) and `validationVALID_LOCAL` (3 minutes) serve complementary defensive purposes. The wall-time window protects against very old validations referencing stale ledger close times; the local-observation window handles the rare case where the network produces an unusually small number of validations, allowing faster recovery by keeping freshly-seen validations relevant a bit longer. `validationVALID_EARLY` (3 minutes) is the mirror guard on the other side — it rejects validations timestamped suspiciously far in the past, providing a buffer against extreme clock skew.

`proposeFRESHNESS` (20 seconds) is the staleness threshold for peer proposals; proposals older than this are discarded. `proposeINTERVAL` (12 seconds) forces a node to re-broadcast its own position before the freshness window expires, guaranteeing that the network always sees the node as active even when the node's position hasn't changed.

## Consensus Timing

`ledgerIDLE_INTERVAL` (15 s) is the maximum time a ledger may remain open with no activity before being forcibly closed. `ledgerMIN_CLOSE` (2 s) ensures all nodes have a moment to compute the last-closed ledger before the next round begins. `ledgerGRANULARITY` (1 s) is the tick interval — how often `phaseEstablish` is called and progress is evaluated. The test suite treats it as a unit of simulated time, connecting peers at fractions of `ledgerGRANULARITY`.

`ledgerMIN_CONSENSUS` (1950 ms) and `ledgerMAX_CONSENSUS` (15 s) bound the normal consensus window. The comment on `ledgerMAX_CONSENSUS` is deliberate: this cap must stay comfortably below `validationFRESHNESS` so that a validator waiting for laggards is not mistaken for an offline node by its peers. `ledgerABANDON_CONSENSUS` (120 s) is the absolute timeout; after this the engine drops the round. The companion `ledgerABANDON_CONSENSUS_FACTOR` (10) participates in a guard in `Consensus.h` that prevents abandoning a round too early when individual `phaseEstablish` calls are themselves unusually slow.

## The Avalanche State Machine

The most architecturally interesting part of the file is the four-state avalanche machine that governs transaction-vote convergence.

```
init → mid → late → stuck (loops)
```

`AvalancheState` is an unscoped enum with values `{init, mid, late, stuck}`. `AvalancheCutoff` is a tiny POD struct that bundles three facts about a state: `consensusTime` (the percentage of the previous round's duration that must elapse before this state activates), `consensusPct` (the minimum yes-vote fraction required to include a transaction while in this state), and `next` (the successor state). These are collected into `avalancheCutoffs`, a `std::map<AvalancheState, AvalancheCutoff>`:

| State  | Time threshold | Required yes-vote % | Next state |
|--------|---------------|---------------------|------------|
| `init` | 0%            | 50%                 | `mid`      |
| `mid`  | 50%           | 65%                 | `late`     |
| `late` | 85%           | 70%                 | `stuck`    |
| `stuck`| 200%          | 95%                 | `stuck`    |

The ratchet is asymmetric by design: once a node has been in consensus twice as long as the prior round without converging, the 95 % `stuck` threshold makes it extraordinarily unlikely that any new transaction will be added to the position, forcing the dispute to resolve by attrition. The `stuck` state loops back to itself because there is nowhere else to go — the comment "once we're stuck, we're stuck" is a protocol guarantee that the threshold never relaxes.

Using a `std::map` rather than a `switch` statement is itself a design choice: it allows state traversal to be data-driven and supports hypothetical looping state machines where a later state transitions back to an earlier one. The map is constructed by hand from compile-time literals, so `at()` calls on it are documented as safe despite the theoretical throw.

## `getNeededWeight()`

This free function is the sole interface through which the consensus engine reads the avalanche map. It takes the current state, the elapsed time expressed as a percentage of the previous round, a round counter, and a minimum-rounds guard. It returns a pair: the `consensusPct` in effect right now, and an `optional<AvalancheState>` indicating whether the caller should advance to the next state (the caller is responsible for the actual state update). The optional is `nullopt` when no transition occurs, giving callers a clean check: `if (newState) avalancheState_ = *newState`.

The `minimumRounds` parameter prevents premature escalation: even if the time percentage is high enough to warrant `mid`, the node won't advance until it has spent at least `avMIN_ROUNDS` rounds in the current state. This guards against clock jitter pushing a fast node through all four states before slower peers have had a chance to vote.

`DisputedTx::updateVote()` calls `getNeededWeight()` per-transaction, threading `convergePercent_` (computed in `phaseEstablish` as elapsed ms divided by the max of `prevRoundTime_` and `avMIN_CONSENSUS_TIME`) through each dispute. The close-time consensus path in `Consensus.h` calls it separately with round counts forced to zero because close-time convergence does not track discrete voting rounds.

`avCT_CONSENSUS_PCT` (75 %) is the threshold used exclusively for close-time consensus — separate from the transaction-vote avalanche thresholds because close-time agreement is a simpler majority question rather than a multi-round ratchet. `avSTALLED_ROUNDS` (4) is the round count after which a vote that hasn't moved is declared stalled in `DisputedTx::stalled()`, allowing the engine to detect deadlocked disputes rather than spinning indefinitely.