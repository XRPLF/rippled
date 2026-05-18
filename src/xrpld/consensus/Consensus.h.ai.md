# `Consensus.h` — XRPL Consensus Algorithm Core

This file contains the entire implementation of the XRPL consensus algorithm. It defines the `Consensus<Adaptor>` class template, two free-standing decision functions (`shouldCloseLedger`, `checkConsensus`), and a threshold helper (`participantsNeeded`). Because the class is fully templated and header-only, the implementation lives entirely here.

## Role in the System

The XRPL consensus protocol must agree on two things each round: which transactions to include in the next ledger, and when that ledger closes. `Consensus.h` encodes the full state machine and decision logic that drives a single node through that process, while delegating all application-specific concerns — networking, ledger storage, signature validation — to an `Adaptor` template parameter. This is a textbook policy-based design: the core algorithm is isolated and testable without a running network, and the same code drives both the production `RCLConsensus` and unit-test stubs.

## The Adaptor Contract

The `Adaptor` class provides four required type aliases (`Ledger_t`, `TxSet_t`, `NodeID_t`, `PeerPosition_t`) plus a collection of callbacks and queries. Critical callbacks include `onClose()` (called when the open ledger closes, producing the initial `ConsensusResult`), `onAccept()` (called when the round concludes successfully), and `onForceAccept()` (standalone/simulate path). Information queries like `proposersValidated()`, `proposersFinished()`, and `getPrevLedger()` feed the timing and correctness checks. Networking hooks — `propose()`, `share()` (three overloads for position, tx set, and individual transaction) — let the algorithm remain agnostic about transport.

## Phase State Machine

Each round transitions through three phases declared in `ConsensusPhase`: `open → establish → accepted`. The machine is driven externally via periodic `timerEntry()` calls; the `Consensus` object itself has no thread or timer.

**Open phase (`phaseOpen`):** The ledger sits open accumulating transactions. On each timer call, `shouldCloseLedger()` decides whether to close based on how many peers have already proposed, whether there are pending transactions, and elapsed time. The close-time reference is subtle: if the previous close wasn't fully agreed upon (mode is `wrongLedger`, or peers disagreed on close time), the node falls back to `prevCloseTime_`, an internally tracked timestamp, rather than the ledger's recorded close time. This guards against propagating bad close-time estimates.

**Establish phase (`phaseEstablish`):** Once `closeLedger()` fires, the node calls `adaptor_.onClose()` to freeze the open ledger into a `ConsensusResult`, broadcasts its transaction set, and, if proposing, announces its position. From here, every timer tick calls `updateOurPositions()` then checks `shouldPause()` and `haveConsensus()`. The minimum guard `ledgerMIN_CONSENSUS` (1950 ms) is enforced before any position updates begin, ensuring every node has a chance to cast an initial vote.

**Accepted phase:** Once both transaction-set and close-time consensus are reached, `phaseEstablish` sets `phase_` to `accepted` and calls `adaptor_.onAccept()`. The `Consensus` object goes quiet until the next `startRound()` call.

## Avalanche Convergence

XRPL consensus uses an avalanche algorithm to converge on a common transaction set. `ConsensusParms` encodes four states (`init → mid → late → stuck`) with escalating yes-vote requirements: 50%, 65%, 70%, 95%. The state advances as `convergePercent_` — the ratio of current round time to previous round time — crosses the thresholds (50%, 85%, 200% of previous round). This dynamic threshold design matters: early in a round, inclusion of a transaction needs only a bare majority, which makes it easy for new legitimate transactions to enter consensus. As time passes and the round should be converging, the required agreement rises, forcing outlier nodes to adopt the majority position.

`updateOurPositions()` drives this per-tick. It prunes stale peer proposals (older than `proposeFRESHNESS`), updates each `DisputedTx` vote via `dispute.updateVote(convergePercent_, ...)`, and rebuilds a `MutableTxSet` if any vote flipped. If our position changes, the new set is shared with peers and re-proposed.

## Dispute Tracking

`createDisputes()` is called whenever a new peer TxSet arrives that differs from our own. It calls `TxSet::compare()` to find differing transactions and creates a `DisputedTx` object for each. Each `DisputedTx` is initialized with our vote (do we have this tx?) and then immediately populated with every known peer's vote. The `result_->compares` set ensures we only compare each pair of sets once, avoiding quadratic work.

`updateDisputes()` is the incremental path: it registers one peer's vote across all existing disputes. Crucially, any time a peer's vote changes, `peerUnchangedCounter_` is reset to zero. This counter feeds the staleness detection in `haveConsensus()`: if no peer has changed any vote for `avSTALLED_ROUNDS` consecutive timer rounds and close-time consensus is already achieved, the round is declared *stalled*, triggering consensus to proceed without the normal 80% threshold.

## Wrong-Ledger Recovery

At the start of every `timerEntry()` call, `checkLedger()` asks the adaptor for the network's preferred previous ledger via `getPrevLedger()`. If this diverges from `prevLedgerID_`, `handleWrongLedger()` fires. The node immediately calls `leaveConsensus()` — broadcasting a bow-out proposal and dropping to `observing` mode — then clears peer state and calls `playbackProposals()` to replay any buffered peer positions for the new ledger ID. If the correct ledger can be acquired, `startRoundInternal()` restarts in `switchedLedger` mode; otherwise the node enters `wrongLedger` mode and waits.

The `recentPeerPositions_` map (capped at 10 proposals per peer) is the buffer that makes `playbackProposals()` work. It stores proposals regardless of which ledger they reference, so when the node switches context it can retroactively process messages it received while on the wrong ledger. This is a deliberate trade-off: a small bounded buffer beats dropping proposals on ledger switches.

## `MonitoredMode` Inner Class

`MonitoredMode` wraps the `ConsensusMode` enum and overrides `set()` to automatically call `adaptor_.onModeChange(before, after)` on every transition. This design ensures the adaptor's state (e.g., logging, metrics, operating mode decisions) is always in sync with the consensus mode. There is no way to change the mode and silently skip the notification.

## `shouldPause()` — Laggard Waiting

When a validator's own latest locally-validated ledger is behind the network's validated ledger, the algorithm may pause to wait for lagging validators before finalizing consensus. The pause decision uses a 5-phase cycle (0–4), where each phase requires a higher fraction of known-current validators. Phase 0 requires only enough non-laggards to satisfy quorum; phase 4 requires all validators. Intermediate phases interpolate linearly between those bounds. The phase cycles with `(ahead - 1) % 5`, so if being ahead persists, the requirement escalates then resets — encoding the judgment that no single threshold is universally better and that persistent divergence suggests a systemic problem beyond the scope of this mechanism.

## Free-Standing Functions

`shouldCloseLedger()` is not a method because it is independently testable and has no dependence on Consensus object state. It accepts raw observations (peer counts, elapsed times) and returns a boolean. Similarly, `checkConsensus()` determines the `ConsensusState` from vote counts and timing without accessing the object. `participantsNeeded(participants, percent)` rounds `participants * percent / 100` to the nearest integer (with a minimum of 1), used throughout for threshold calculations.

## Thread Safety and the `clog` Pattern

The class explicitly disclaims thread safety and defers synchronization to the caller. A second diagnostic pattern appears throughout: most public and private methods accept `std::unique_ptr<std::stringstream> const& clog = {}`. When non-null, the `CLOG` macro appends detailed step-by-step state to this log object, giving callers access to a complete narrative of a single consensus round's decisions — distinct from the journal's debug output, and suitable for post-hoc audit of a specific round.