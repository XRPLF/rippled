# `DisputedTx.h` — Per-Transaction Dispute Tracking During XRPL Consensus

## Role in the System

XRPL's consensus protocol works by having validators iteratively converge on a shared transaction set. When two validators propose different sets, the transactions that appear in one set but not the other become *disputed*. `DisputedTx<Tx_t, NodeID_t>` is the object that manages one such dispute: it records every peer's yes/no vote on a single transaction and drives the local node's own vote toward consensus over time.

Critically, there is no `DisputedTx` object for transactions that all validators agree on — only for the transactions that genuinely differ between at least two observed positions. Objects are created inside `Consensus::createDisputes()` when the engine compares its own transaction set to a peer's, and they live inside `ConsensusResult::disputes` (a `hash_map<TxID, Dispute_t>`) for exactly the duration of the establish phase.

## Vote Storage

Peer votes are kept in a `boost::container::flat_map<NodeID_t, bool>`. The flat map is a sorted, contiguous-memory structure that outperforms `std::map` for small-to-medium collections due to cache locality. Capacity is reserved up-front in the constructor using `numPeers` — the number of currently-connected validators — to avoid rehashing during the burst of incoming `setVote` calls that follows dispute creation.

`setVote()` maintains two independent integer counters, `yays_` and `nays_`, so that percentage computations in `updateVote()` never need to scan the full map. The method returns `true` on any *change* (including a brand-new vote), which allows the caller in `Consensus::phaseEstablish()` to track whether any peer has moved during a given round via `peerUnchangedCounter_`. `unVote()` is called when a peer disconnects or its position is superseded, keeping the counters accurate.

## The Avalanche Voting State Machine

The most subtle part of the class is how `updateVote()` decides to flip the local node's vote. XRPL's consensus uses a strategy borrowed from *avalanche* protocols: the required percentage of "yes" votes needed to adopt a transaction rises as the consensus round grows longer. This escalating threshold prevents a small minority of lagging validators from indefinitely blocking convergence, while still giving genuine disagreements time to resolve.

`ConsensusParms` defines four states with their thresholds:

| State   | Enters after (% of prior round) | Required "yes" | Next state |
|---------|----------------------------------|----------------|------------|
| `init`  | 0% (always)                      | 50%            | `mid`      |
| `mid`   | 50%                              | 65%            | `late`     |
| `late`  | 85%                              | 70%            | `stuck`    |
| `stuck` | 200%                             | 95%            | `stuck`    |

`updateVote()` calls `getNeededWeight()` (defined in `ConsensusParms.h`) on every invocation, passing the current `avalancheState_` and a counter `avalancheCounter_` that increments with each call. A state transition only occurs when both the time threshold is met *and* the state has been active for at least `avMIN_ROUNDS` rounds, preventing rapid-fire advancement through states. When a transition fires, `avalancheCounter_` resets to zero, enforcing a minimum dwell time in each state.

The `stuck` state is deliberately self-looping — once `percentTime` exceeds 200% of the prior round, the required threshold jumps to 95% and stays there. At that point it is almost certain that network topology or a genuinely controversial transaction is preventing agreement; the near-unanimity requirement forces the transaction to be either widely accepted or definitively dropped.

## Proposing vs. Non-Proposing Behavior

`updateVote()` receives a `proposing` flag that fundamentally changes how votes are counted. When the local node is actively proposing:

```
weight = (yays_ * 100 + (ourVote_ ? 100 : 0)) / (nays_ + yays_ + 1)
newPosition = weight > requiredPct
```

The node counts its own vote as one more unit alongside all peers, and the integer percentage must exceed the current avalanche threshold.

When not proposing (i.e., the node is in "observer" mode), the logic simplifies to `newPosition = yays_ > nays_` and `weight = -1`. The observer never drives the threshold logic — it simply follows majority direction. This asymmetry prevents non-proposing nodes from inadvertently distorting the weighted vote that proposing nodes rely on.

## Stall Detection

`stalled()` is a diagnostic predicate called by `Consensus::checkConsensus()` once close-time consensus has been established but disputed transactions remain. A transaction is declared stalled when all of the following hold:

1. The avalanche state machine has reached a terminal condition — `nextCutoff.consensusTime <= currentCutoff.consensusTime`, i.e., we're in the `stuck` loop.
2. At least `avMIN_ROUNDS` rounds have elapsed in the current state.
3. Either `peersUnchanged >= avSTALLED_ROUNDS` (peers haven't shifted) *or* `currentVoteCounter_ >= avSTALLED_ROUNDS` (our own vote hasn't moved). Using *or* rather than *and* is deliberate: a malicious peer that flip-flops its vote to reset the peer counter cannot indefinitely prevent the stall declaration as long as our own vote has stabilised.
4. The current vote split exceeds `minCONSENSUS_PCT` (80%) in either direction, i.e., there is lopsided agreement.

A stall is flagged as an error in the journal because consensus stalling on even a single transaction is an abnormal event that warrants investigation.

## Design Choices

The class is a header-only template rather than a concrete class tied to XRPL's transaction and node-ID types. This mirrors the broader `Consensus<Adaptor>` design where the algorithm itself is decoupled from the ledger implementation, making the consensus layer independently testable against simulated transaction types. `Tx_t::ID` is extracted as `TxID_t` via a nested typedef, mirroring the convention used throughout `ConsensusTypes.h`.

`currentVoteCounter_` — the number of consecutive rounds without a vote flip — feeds both `stalled()` (stall guard) and the log output in `updateVote()`. Resetting it to zero on every flip means it accurately measures the *current* streak, not a historical maximum, which is the right signal for both purposes.