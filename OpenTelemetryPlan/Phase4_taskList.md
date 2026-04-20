# Phase 4: Consensus Tracing Task List

> **Goal**: Full observability into consensus rounds — track round lifecycle, phase transitions, proposal handling, and validation. This is the RUN phase that completes the distributed tracing story.
>
> **Scope**: RCLConsensus instrumentation for round starts, phase transitions (open/establish/accept), proposal send/receive, validation handling, and correlation with transaction traces from Phase 3.
>
> **Branch**: `pratik/otel-phase4-consensus-tracing` (from `pratik/otel-phase3-tx-tracing`)

### Related Plan Documents

| Document                                                     | Relevance                                                   |
| ------------------------------------------------------------ | ----------------------------------------------------------- |
| [04-code-samples.md](./04-code-samples.md)                   | Consensus instrumentation (§4.5.2), consensus span patterns |
| [01-architecture-analysis.md](./01-architecture-analysis.md) | Consensus round flow (§1.4), key trace points (§1.6)        |
| [06-implementation-phases.md](./06-implementation-phases.md) | Phase 4 tasks (§6.5), definition of done (§6.11.4)          |
| [02-design-decisions.md](./02-design-decisions.md)           | Consensus attribute schema (§2.4.4)                         |

---

## Task 4.1: Instrument Consensus Round Start

**Objective**: Create a root span for each consensus round that captures the round's key parameters.

**What to do**:

- Edit `src/xrpld/app/consensus/RCLConsensus.cpp`:
  - In `RCLConsensus::startRound()` (or the Adaptor's startRound):
    - Create `consensus.round` span using `XRPL_TRACE_CONSENSUS` macro
    - Set attributes:
      - `xrpl.consensus.ledger.prev` — previous ledger hash
      - `xrpl.consensus.ledger.seq` — target ledger sequence
      - `xrpl.consensus.proposers` — number of trusted proposers
      - `xrpl.consensus.mode` — "proposing" or "observing"
    - Store the span context for use by child spans in phase transitions

- Add a member to hold current round trace context:
  - `opentelemetry::context::Context currentRoundContext_` (guarded by `#ifdef`)
  - Updated at round start, used by phase transition spans

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`
- `src/xrpld/app/consensus/RCLConsensus.h` (add context member)

**Reference**:

- [04-code-samples.md §4.5.2](./04-code-samples.md) — startRound instrumentation example
- [01-architecture-analysis.md §1.4](./01-architecture-analysis.md) — Consensus round flow

---

## Task 4.2: Instrument Phase Transitions

**Objective**: Create child spans for each consensus phase (open, establish, accept) to show timing breakdown.

**What to do**:

- Edit `src/xrpld/app/consensus/RCLConsensus.cpp`:
  - Identify where phase transitions occur (the `Consensus<Adaptor>` template drives this)
  - For each phase entry:
    - Create span as child of `currentRoundContext_`: `consensus.phase.open`, `consensus.phase.establish`, `consensus.phase.accept`
    - Set `xrpl.consensus.phase` attribute
    - Add `phase.enter` event at start, `phase.exit` event at end
    - Record phase duration in milliseconds

  - In the `onClose` adaptor method:
    - Create `consensus.ledger_close` span
    - Set attributes: close_time, mode, transaction count in initial position

  - Note: The Consensus template class in `include/xrpl/consensus/Consensus.h` drives phase transitions — check if instrumentation goes there or in the Adaptor

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`
- Possibly `include/xrpl/consensus/Consensus.h` (for template-level phase tracking)

**Reference**:

- [04-code-samples.md §4.5.2](./04-code-samples.md) — phaseTransition instrumentation

---

## Task 4.3: Instrument Proposal Handling

**Objective**: Trace proposal send and receive to show validator coordination.

**What to do**:

- Edit `src/xrpld/app/consensus/RCLConsensus.cpp`:
  - In `Adaptor::propose()`:
    - Create `consensus.proposal.send` span
    - Set attributes: `xrpl.consensus.round` (proposal sequence), proposal hash
    - Inject trace context into outgoing `TMProposeSet::trace_context` (from Phase 3 protobuf)

  - In `Adaptor::peerProposal()` (or wherever peer proposals are received):
    - Extract trace context from incoming `TMProposeSet::trace_context`
    - Create `consensus.proposal.receive` span as child of extracted context
    - Set attributes: `xrpl.consensus.proposer` (node ID), `xrpl.consensus.round`

  - In `Adaptor::share(RCLCxPeerPos)`:
    - Create `consensus.proposal.relay` span for relaying peer proposals

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`

**Reference**:

- [04-code-samples.md §4.5.2](./04-code-samples.md) — peerProposal instrumentation
- [02-design-decisions.md §2.4.4](./02-design-decisions.md) — Consensus attribute schema

---

## Task 4.4: Instrument Validation Handling

**Objective**: Trace validation send and receive to show ledger validation flow.

**What to do**:

- Edit `src/xrpld/app/consensus/RCLConsensus.cpp` (or the validation handler):
  - When sending our validation:
    - Create `consensus.validation.send` span
    - Set attributes: validated ledger hash, sequence, signing time

  - When receiving a peer validation:
    - Extract trace context from `TMValidation::trace_context` (if present)
    - Create `consensus.validation.receive` span
    - Set attributes: `xrpl.consensus.validator` (node ID), ledger hash

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`
- `src/xrpld/app/misc/NetworkOPs.cpp` (if validation handling is here)

---

## Task 4.5: Add Consensus-Specific Attributes

**Objective**: Enrich consensus spans with detailed attributes for debugging and analysis.

**What to do**:

- Review all consensus spans and ensure they include:
  - `xrpl.consensus.ledger.seq` — target ledger sequence number
  - `xrpl.consensus.round` — consensus round number
  - `xrpl.consensus.mode` — proposing/observing/wrongLedger
  - `xrpl.consensus.phase` — current phase name
  - `xrpl.consensus.phase_duration_ms` — time spent in phase
  - `xrpl.consensus.proposers` — number of trusted proposers
  - `xrpl.consensus.tx_count` — transactions in proposed set
  - `xrpl.consensus.disputes` — number of disputed transactions
  - `xrpl.consensus.converge_percent` — convergence percentage

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`

---

## Task 4.6: Correlate Transaction and Consensus Traces

**Objective**: Link transaction traces from Phase 3 with consensus traces so you can follow a transaction from submission through consensus into the ledger.

**What to do**:

- In `onClose()` or `onAccept()`:
  - When building the consensus position, link the round span to individual transaction spans using span links (if OTel SDK supports it) or events
  - At minimum, record the transaction hashes included in the consensus set as span events: `tx.included` with `xrpl.tx.hash` attribute

- In `processTransactionSet()` (NetworkOPs):
  - If the consensus round span context is available, create child spans for each transaction applied to the ledger

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`
- `src/xrpld/app/misc/NetworkOPs.cpp`

---

## Task 4.7: Build Verification and Testing

**Objective**: Verify all Phase 4 changes compile and don't affect consensus timing.

**What to do**:

1. Build with `telemetry=ON` — verify no compilation errors
2. Build with `telemetry=OFF` — verify no regressions (critical for consensus code)
3. Run existing consensus-related unit tests
4. Verify that all macros expand to no-ops when disabled
5. Check that no consensus-critical code paths are affected by instrumentation overhead

**Verification Checklist**:

- [ ] Build succeeds with telemetry ON
- [ ] Build succeeds with telemetry OFF
- [ ] Existing consensus tests pass
- [ ] No new includes in consensus headers when telemetry is OFF
- [ ] Phase timing instrumentation doesn't use blocking operations

---

## Summary

| Task | Description                           | New Files | Modified Files | Depends On    |
| ---- | ------------------------------------- | --------- | -------------- | ------------- |
| 4.1  | Consensus round start instrumentation | 0         | 2              | Phase 3       |
| 4.2  | Phase transition instrumentation      | 0         | 1-2            | 4.1           |
| 4.3  | Proposal handling instrumentation     | 0         | 1              | 4.1           |
| 4.4  | Validation handling instrumentation   | 0         | 1-2            | 4.1           |
| 4.5  | Consensus-specific attributes         | 0         | 1              | 4.2, 4.3, 4.4 |
| 4.6  | Transaction-consensus correlation     | 0         | 2              | 4.2, Phase 3  |
| 4.7  | Build verification and testing        | 0         | 0              | 4.1-4.6       |

**Parallel work**: Tasks 4.2, 4.3, and 4.4 can run in parallel after 4.1 is complete. Task 4.5 depends on all three. Task 4.6 depends on 4.2 and Phase 3.

**Exit Criteria** (from [06-implementation-phases.md §6.11.4](./06-implementation-phases.md)):

- [ ] Complete consensus round traces
- [ ] Phase transitions visible
- [ ] Proposals and validations traced
- [ ] No impact on consensus timing
