# Consensus.cpp

This file implements the three free functions that encode the core timing and agreement logic of the XRPL consensus algorithm: `shouldCloseLedger`, `checkConsensusReached`, and `checkConsensus`. These are pure decision functions — they take observable network state as inputs and return boolean or enum results. All state is owned elsewhere (in the `Consensus<Adaptor>` template class declared in `Consensus.h`); this file holds only the policies that drive phase transitions.

## shouldCloseLedger

`shouldCloseLedger()` decides whether the currently-open ledger should transition to the `Establish` phase. It is called by the consensus timer and on transaction receipt. The function applies a strict priority ordering of checks:

1. **Sanity bounds.** If `prevRoundTime` or `timeSincePrevClose` fall outside plausible ranges (`-1s`–`10min`), the ledger closes immediately with a warning log. These represent clock errors or node startup edge cases where normal timing heuristics would produce garbage.

2. **Majority already closed.** If more than half of known proposers have already closed this ledger (`proposersClosed + proposersValidated > prevProposers / 2`), the node follows. This is a straightforward network-following rule to prevent stragglers from causing unnecessary divergence.

3. **Idle case (no transactions).** The ledger closes only when `timeSincePrevClose >= idleInterval` (default `ledgerIDLE_INTERVAL` = 15 seconds). Without this, an idle network would close empty ledgers at the minimum close rate, wasting resources.

4. **Minimum open time.** The ledger will not close before `ledgerMIN_CLOSE` (2 seconds) has elapsed regardless of other signals. This ensures other validators have time to compute the LCL and receive transactions.

5. **Rate limiting.** If `openTime < prevRoundTime / 2`, closing is blocked. This is a deliberate throttle: a fast node cannot drive the network faster than twice the prior round's pace, which would exclude slower validators and create instability. The design explicitly prioritizes liveness of slower nodes over throughput.

## checkConsensusReached

`checkConsensusReached()` is an internal helper that converts raw vote counts into a binary reached/not-reached answer. The non-obvious cases:

- **Zero peers (`total == 0`).** The function refuses to declare consensus until `reachedMax` (i.e., `currentAgreeTime > ledgerMAX_CONSENSUS`, 15 seconds). This guards against a race condition where a node hasn't yet received any proposals from the network and might prematurely close on its own position — which would likely cause a desync when the actual network position arrives later.

- **Stalled (`stalled == true`).** Consensus is declared immediately, bypassing the percentage check. The `stalled` condition means all disputed transactions have clear supermajority agreement either for or against inclusion. A Byzantine minority cannot manipulate which transactions make the cut by hovering votes near the threshold, because once everyone's position is stable and clear, the network commits.

- **Self-counting.** When `count_self` is true, the local node's own position is added to both `agreeing` and `total` before the percentage is computed. `checkConsensus()` passes `proposing` here, so validating nodes count themselves while observers do not.

## checkConsensus

`checkConsensus()` returns a `ConsensusState` enum (`No`, `Yes`, `MovedOn`, `Expired`) and calls `checkConsensusReached()` twice with different arguments to answer two distinct questions:

**First call:** Has the network, including us, reached 80% agreement on our position? Uses `currentAgree`, `currentProposers`, and `proposing` (counting self). If yes → `ConsensusState::Yes`.

**Second call:** Have 80% of peers already *finished* this round (moved on to the next ledger), even though we haven't agreed with them? Uses `currentFinished` with `count_self = false`. If yes → `ConsensusState::MovedOn`. This is a distinct outcome from `Yes` — the local node recognizes it has lost the race and must follow the network, not that it genuinely reached agreement.

Before either check, two time-based guards apply: a hard minimum duration floor of `ledgerMIN_CONSENSUS` (1950ms), and a laggard-protection rule that requires extra time when fewer than 75% of the previous round's proposers are currently visible. If neither consensus check passes, the function checks for abandonment using a dynamic timeout of `previousAgreeTime × ledgerABANDON_CONSENSUS_FACTOR` (factor = 10), clamped between `ledgerMAX_CONSENSUS` (15s) and `ledgerABANDON_CONSENSUS` (120s), returning `ConsensusState::Expired` if exceeded.

## Logging

Every function accepts both a `beast::Journal j` and a `std::unique_ptr<std::stringstream> const& clog`. The `CLOG(clog)` macro appends to the stringstream only when it is non-null, while `JLOG` writes to the structured journal. This dual-path design avoids string formatting cost on the hot path while enabling full consensus round traces when the caller provides a log buffer for debugging. Passing null simply skips the append with no risk of dereference.

## Relationship to Other Files

`ConsensusParms.h` owns all numeric thresholds. `ConsensusTypes.h` defines `ConsensusState` and related enums. The large `Consensus<Adaptor>` template in `Consensus.h` calls both `shouldCloseLedger()` during the `open` phase and `checkConsensus()` during the `establish` phase. Keeping this policy logic in a `.cpp` file as free functions — rather than private methods of the template — makes them independently testable and keeps the template header focused on state management.