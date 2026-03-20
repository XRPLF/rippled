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

  - Note: The Consensus template class in `src/xrpld/consensus/Consensus.h` drives phase transitions — Phase 4a instruments directly in the template

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

### Implemented Spans

| Span Name                   | Method                             | Key Attributes                                                                                                                                                                                                        |
| --------------------------- | ---------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `consensus.proposal.send`   | `Adaptor::propose`                 | `xrpl.consensus.round`                                                                                                                                                                                                |
| `consensus.ledger_close`    | `Adaptor::onClose`                 | `xrpl.consensus.ledger.seq`, `xrpl.consensus.mode`                                                                                                                                                                    |
| `consensus.accept`          | `Adaptor::onAccept`                | `xrpl.consensus.proposers`, `xrpl.consensus.round_time_ms`                                                                                                                                                            |
| `consensus.accept.apply`    | `Adaptor::doAccept`                | `xrpl.consensus.close_time`, `close_time_correct`, `close_resolution_ms`, `state`, `proposing`, `round_time_ms`, `ledger.seq`, `parent_close_time`, `close_time_self`, `close_time_vote_bins`, `resolution_direction` |
| `consensus.validation.send` | `Adaptor::onAccept` (via validate) | `xrpl.consensus.proposing`                                                                                                                                                                                            |

#### Close Time Attributes (consensus.accept.apply)

The `consensus.accept.apply` span captures ledger close time agreement details
driven by `avCT_CONSENSUS_PCT` (75% validator agreement threshold):

- **`xrpl.consensus.close_time`** — Agreed-upon ledger close time (epoch seconds). When validators disagree (`consensusCloseTime == epoch`), this is synthetically set to `prevCloseTime + 1s`.
- **`xrpl.consensus.close_time_correct`** — `true` if validators reached agreement, `false` if they "agreed to disagree" (close time forced to prev+1s).
- **`xrpl.consensus.close_resolution_ms`** — Rounding granularity for close time (starts at 30s, decreases as ledger interval stabilizes).
- **`xrpl.consensus.state`** — `"finished"` (normal) or `"moved_on"` (consensus failed, adopted best available).
- **`xrpl.consensus.proposing`** — Whether this node was proposing.
- **`xrpl.consensus.round_time_ms`** — Total consensus round duration.
- **`xrpl.consensus.parent_close_time`** — Previous ledger's close time (epoch seconds). Enables computing close-time deltas across consecutive rounds without correlating separate spans.
- **`xrpl.consensus.close_time_self`** — This node's own proposed close time before consensus voting.
- **`xrpl.consensus.close_time_vote_bins`** — Number of distinct close-time vote bins from peer proposals. Higher values indicate less agreement among validators.
- **`xrpl.consensus.resolution_direction`** — Whether close-time resolution `"increased"` (coarser), `"decreased"` (finer), or stayed `"unchanged"` relative to the previous ledger.

**Exit Criteria** (from [06-implementation-phases.md §6.11.4](./06-implementation-phases.md)):

- [x] Complete consensus round traces
- [x] Phase transitions visible
- [x] Proposals and validations traced
- [x] Close time agreement tracked (per `avCT_CONSENSUS_PCT`)
- [x] No impact on consensus timing

---

# Phase 4a: Establish-Phase Gap Fill & Cross-Node Correlation

> **Goal**: Fill tracing gaps in the consensus establish phase (disputes, convergence,
> threshold escalation, mode changes) and establish cross-node correlation using a
> deterministic shared trace ID derived from `previousLedger.id()`.
>
> **Approach**: Direct instrumentation in `Consensus.h` — the generic consensus
> template has full access to internal state (`convergePercent_`, `result_->disputes`,
> `mode_`, threshold logic). Telemetry access comes via a single new adaptor
> method `getTelemetry()`. Long-lived spans (round, establish) are stored as
> class members using `SpanGuard` directly — NOT the `XRPL_TRACE_*` convenience
> macros (which create local variables named `_xrpl_guard_`). Short-lived
> scoped spans (update_positions, check) can use the macros. All code compiles
> to no-ops when `XRPL_ENABLE_TELEMETRY` is not defined.
>
> **Branch**: `pratik/otel-phase4-consensus-tracing`

## Design: Switchable Correlation Strategy

Two strategies for cross-node trace correlation, switchable via config:

### Strategy A — Deterministic Trace ID (Default)

Derive `trace_id = SHA256(previousLedger.id())[0:16]` so all nodes in the same
consensus round share the same trace_id without P2P context propagation.

- **Pros**: All nodes appear in the same trace in Tempo/Jaeger automatically.
  No collector-side post-processing needed.
- **Cons**: Overrides OTel's random trace_id generation; requires custom
  `IdGenerator` or manual span context construction.

### Strategy B — Attribute-Based Correlation

Use normal random trace_id but attach `xrpl.consensus.ledger_id` as an attribute
on every consensus span. Correlation happens at query time via Tempo/Grafana
`by attribute` queries.

- **Pros**: Standard OTel trace_id semantics; no SDK customization.
- **Cons**: Cross-node correlation requires query-time joins, not automatic.

### Config

```ini
[telemetry]
# "deterministic" (default) or "attribute"
consensus_trace_strategy=deterministic
```

### Implementation

In `RCLConsensus::Adaptor::startRound()`:

- If `deterministic`:
  1. Compute `trace_id_bytes = SHA256(prevLedgerID)[0:16]`
  2. Construct `opentelemetry::trace::TraceId(trace_id_bytes)`
  3. Create a synthetic `SpanContext` with this trace_id and a random span_id:
     ```cpp
     auto traceId = opentelemetry::trace::TraceId(trace_id_bytes);
     auto spanId  = opentelemetry::trace::SpanId(random_8_bytes);
     auto syntheticCtx = opentelemetry::trace::SpanContext(
         traceId, spanId, opentelemetry::trace::TraceFlags(1), false);
     ```
  4. Wrap in `opentelemetry::context::Context` via
     `opentelemetry::trace::SetSpan(context, syntheticSpan)`
  5. Call `startSpan("consensus.round", parentContext)` so the new span
     inherits the deterministic trace_id.
- If `attribute`: start a normal `consensus.round` span, set
  `xrpl.consensus.ledger_id = previousLedger.id()` as attribute.

Both strategies always set `xrpl.consensus.round_id` (round number) and
`xrpl.consensus.ledger_id` (previous ledger hash) as attributes.

---

## Design: Span Hierarchy

```
consensus.round  (root — created in RCLConsensus::startRound, closed at accept)
│   link → previous round's SpanContext (follows-from)
│
├── consensus.establish  (phaseEstablish → acceptance, in Consensus.h)
│   ├── consensus.update_positions  (each updateOurPositions call)
│   │   └── consensus.dispute.resolve  (per-tx dispute resolution event)
│   ├── consensus.check  (each haveConsensus call)
│   └── consensus.mode_change  (short-lived span in adaptor on mode transition)
│
├── consensus.accept  (existing onAccept span — reparented under round)
│
└── consensus.validation.send  (existing — reparented, follows-from link to round)
```

### Span Links (follows-from relationships)

| Link Source                               | Link Target                | Rationale                                                                      |
| ----------------------------------------- | -------------------------- | ------------------------------------------------------------------------------ |
| `consensus.round` (N+1)                   | `consensus.round` (N)      | Causal chain: round N+1 exists because round N accepted                        |
| `consensus.validation.send`               | `consensus.round`          | Validation follows from the round that produced it; may outlive the round span |
| _(Phase 4b)_ Received proposal processing | Sender's `consensus.round` | Cross-node causal link via P2P context propagation                             |

---

## Task 4a.0: Prerequisites — Extend SpanGuard and Telemetry APIs

**Objective**: Add missing API surface needed by later tasks.

**What to do**:

1. **Add `SpanGuard::addEvent()` with attributes** (needed by Task 4a.5):
   The current `addEvent(string_view name)` only accepts a name. Add an
   overload that accepts key-value attributes:

   ```cpp
   void addEvent(std::string_view name,
       std::initializer_list<
           std::pair<opentelemetry::nostd::string_view,
                     opentelemetry::common::AttributeValue>> attributes)
   {
       span_->AddEvent(std::string(name), attributes);
   }
   ```

2. **Add a `Telemetry::startSpan()` overload that accepts span links** (needed by Tasks 4a.2, 4a.8):
   The current `startSpan()` has no span link support. Add an overload that
   accepts a vector of `SpanContext` links for follows-from relationships:

   ```cpp
   virtual opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
   startSpan(
       std::string_view name,
       opentelemetry::context::Context const& parentContext,
       std::vector<opentelemetry::trace::SpanContext> const& links,
       opentelemetry::trace::SpanKind kind = opentelemetry::trace::SpanKind::kInternal) = 0;
   ```

3. **Add `XRPL_TRACE_ADD_EVENT` macro** (needed by Task 4a.5):
   Add to `TracingInstrumentation.h` to expose `addEvent(name, attrs)` through
   the macro interface (consistent with `XRPL_TRACE_SET_ATTR` pattern):
   ```cpp
   #ifdef XRPL_ENABLE_TELEMETRY
   #define XRPL_TRACE_ADD_EVENT(name, ...)               \
       if (_xrpl_guard_.has_value())                     \
       {                                                 \
           _xrpl_guard_->addEvent(name, __VA_ARGS__);   \
       }
   #else
   #define XRPL_TRACE_ADD_EVENT(name, ...) ((void)0)
   #endif
   ```

**Key modified files**:

- `include/xrpl/telemetry/SpanGuard.h` — add `addEvent()` overload
- `include/xrpl/telemetry/Telemetry.h` — add `startSpan()` with links
- `src/xrpld/telemetry/Telemetry.cpp` — implement new overload
- `src/xrpld/telemetry/NullTelemetry.cpp` — no-op implementation
- `src/xrpld/telemetry/TracingInstrumentation.h` — add `XRPL_TRACE_ADD_EVENT` macro

---

## Task 4a.1: Adaptor `getTelemetry()` Method

**Objective**: Give `Consensus.h` access to the telemetry subsystem without
coupling the generic template to OTel headers.

**What to do**:

- Add `getTelemetry()` method to the Adaptor concept (returns
  `xrpl::telemetry::Telemetry&`). The return type is already forward-declared
  behind `#ifdef XRPL_ENABLE_TELEMETRY`.
- Implement in `RCLConsensus::Adaptor` — delegates to `app_.getTelemetry()`.
- In `Consensus.h`, the `XRPL_TRACE_*` macros call
  `adaptor_.getTelemetry()` — when telemetry is disabled, the macros expand to
  `((void)0)` and the method is never called.

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.h` — declare `getTelemetry()`
- `src/xrpld/app/consensus/RCLConsensus.cpp` — implement `getTelemetry()`

---

## Task 4a.2: Switchable Round Span with Deterministic Trace ID

**Objective**: Create a `consensus.round` root span in `startRound()` that uses
the switchable correlation strategy. Store span context as a member for child
spans in `Consensus.h`.

**What to do**:

- In `RCLConsensus::Adaptor::startRound()` (or a new helper):
  - Read `consensus_trace_strategy` from config.
  - **Deterministic**: compute `trace_id = SHA256(prevLedgerID)[0:16]`.
    Construct a `SpanContext` with this trace_id, then start
    `consensus.round` span as child of that context.
  - **Attribute**: start normal `consensus.round` span.
  - Set attributes on both: `xrpl.consensus.round_id`,
    `xrpl.consensus.ledger_id`, `xrpl.consensus.ledger.seq`,
    `xrpl.consensus.mode`.
  - Store the round span in `Consensus` as a member (see Task 4a.3).
  - If a previous round's span context is available, add a **span link**
    (follows-from) to establish the round chain.

- Add `createDeterministicTraceId(hash)` utility to
  `include/xrpl/telemetry/Telemetry.h` (returns 16-byte trace ID from a
  256-bit hash by truncation).

- Add `consensus_trace_strategy` to `Telemetry::Setup` and
  `TelemetryConfig.cpp` parser:
  ```cpp
  /** Cross-node correlation strategy: "deterministic" or "attribute". */
  std::string consensusTraceStrategy = "deterministic";
  ```

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`
- `include/xrpl/telemetry/Telemetry.h` — `createDeterministicTraceId()`
- `src/xrpld/telemetry/TelemetryConfig.cpp` — parse new config option

---

## Task 4a.3: Span Members in `Consensus.h`

**Objective**: Add span storage to the `Consensus` class so that spans created
in `startRound()` (adaptor) are accessible from `phaseEstablish()`,
`updateOurPositions()`, and `haveConsensus()` (template methods).

**What to do**:

- Add to `Consensus` private members (guarded by `#ifdef XRPL_ENABLE_TELEMETRY`):
  ```cpp
  #ifdef XRPL_ENABLE_TELEMETRY
  std::optional<xrpl::telemetry::SpanGuard> roundSpan_;
  std::optional<xrpl::telemetry::SpanGuard> establishSpan_;
  opentelemetry::context::Context prevRoundContext_;
  #endif
  ```
- `roundSpan_` is created in `startRound()` via the adaptor and stored.
  Its `SpanGuard::Scope` member keeps the span active on the thread context
  for the entire round lifetime.
- `establishSpan_` is created when entering phaseEstablish and cleared on accept.
  It becomes a child of `roundSpan_` via OTel's thread-local context propagation.
- `prevRoundContext_` stores the previous round's context for follows-from links.

**Threading assumption**: `startRound()`, `phaseEstablish()`, `updateOurPositions()`,
and `haveConsensus()` all run on the same thread (the consensus job queue thread).
This is required for the `SpanGuard::Scope`-based parent-child hierarchy to work.
The `Consensus` class documentation confirms it is NOT thread-safe and calls are
serialized by the application.

- Add conditional include at top of `Consensus.h`:
  ```cpp
  #ifdef XRPL_ENABLE_TELEMETRY
  #include <xrpl/telemetry/SpanGuard.h>
  #include <xrpld/telemetry/TracingInstrumentation.h>
  #endif
  ```

**Key modified files**:

- `src/xrpld/consensus/Consensus.h`

---

## Task 4a.4: Instrument `phaseEstablish()`

**Objective**: Create `consensus.establish` span wrapping the establish phase,
with attributes for convergence progress.

**What to do**:

- At the start of `phaseEstablish()` (line 1298), if `establishSpan_` is not
  yet created, create it as child of `roundSpan_` using the **direct API**
  (NOT the `XRPL_TRACE_CONSENSUS` macro, which creates a local variable):

  ```cpp
  #ifdef XRPL_ENABLE_TELEMETRY
  if (!establishSpan_ && adaptor_.getTelemetry().shouldTraceConsensus())
  {
      establishSpan_.emplace(
          adaptor_.getTelemetry().startSpan("consensus.establish"));
  }
  #endif
  ```

- Set attributes on each call:
  - `xrpl.consensus.converge_percent` — `convergePercent_`
  - `xrpl.consensus.establish_count` — `establishCounter_`
  - `xrpl.consensus.proposers` — `currPeerPositions_.size()`

- On phase exit (transition to accept), close the establish span and record
  final duration.

**Key modified files**:

- `src/xrpld/consensus/Consensus.h` — `phaseEstablish()` method

---

## Task 4a.5: Instrument `updateOurPositions()`

**Objective**: Trace each position update cycle including dispute resolution
details.

**What to do**:

- At the start of `updateOurPositions()` (line 1418), create a scoped child
  span. This method is called and returns within a single `phaseEstablish()`
  call, so the `XRPL_TRACE_CONSENSUS` macro works here (scoped local):

  ```cpp
  XRPL_TRACE_CONSENSUS(adaptor_.getTelemetry(), "consensus.update_positions");
  ```

- Set attributes:
  - `xrpl.consensus.disputes_count` — `result_->disputes.size()`
  - `xrpl.consensus.converge_percent` — current convergence
  - `xrpl.consensus.proposers_agreed` — count of peers with same position
  - `xrpl.consensus.proposers_total` — total peer positions

- Inside the dispute resolution loop, for each dispute that changes our vote,
  add an **event** with attributes using `XRPL_TRACE_ADD_EVENT` (from Task 4a.0):
  ```cpp
  XRPL_TRACE_ADD_EVENT("dispute.resolve", {
      {"xrpl.tx.id", std::string(tx_id)},
      {"xrpl.dispute.our_vote", our_vote},
      {"xrpl.dispute.yays", static_cast<int64_t>(yays)},
      {"xrpl.dispute.nays", static_cast<int64_t>(nays)}
  });
  ```

**Key modified files**:

- `src/xrpld/consensus/Consensus.h` — `updateOurPositions()` method

---

## Task 4a.6: Instrument `haveConsensus()` (Threshold & Convergence)

**Objective**: Trace consensus checking including threshold escalation
(`ConsensusParms::AvalancheState::{init, mid, late, stuck}`).

**What to do**:

- At the start of `haveConsensus()` (line 1598), create a scoped child span:

  ```cpp
  XRPL_TRACE_CONSENSUS(adaptor_.getTelemetry(), "consensus.check");
  ```

- Set attributes:
  - `xrpl.consensus.agree_count` — peers that agree with our position
  - `xrpl.consensus.disagree_count` — peers that disagree
  - `xrpl.consensus.converge_percent` — convergence percentage
  - `xrpl.consensus.result` — ConsensusState result (Yes/No/MovedOn)

- The free function `checkConsensus()` in `Consensus.cpp` (line 151) determines
  thresholds based on `currentAgreeTime`. Threshold values come from
  `ConsensusParms::avalancheCutoffs` (defined in `ConsensusParms.h`).
  The escalation states are `ConsensusParms::AvalancheState::{init, mid, late, stuck}`.
  Record the effective threshold as an attribute on the span:
  - `xrpl.consensus.threshold_percent` — current threshold from `avalancheCutoffs`

**Key modified files**:

- `src/xrpld/consensus/Consensus.h` — `haveConsensus()` method

---

## Task 4a.7: Instrument Mode Changes

**Objective**: Trace consensus mode transitions (proposing ↔ observing,
wrongLedger, switchedLedger).

**What to do**:

Mode changes are rare (typically 0-1 per round), so a **standalone short-lived
span** is appropriate (not an event). This captures timing of the mode change
itself.

- In `RCLConsensus::Adaptor::onModeChange()`, create a scoped span:

  ```cpp
  XRPL_TRACE_CONSENSUS(app_.getTelemetry(), "consensus.mode_change");
  XRPL_TRACE_SET_ATTR("xrpl.consensus.mode.old", to_string(before).c_str());
  XRPL_TRACE_SET_ATTR("xrpl.consensus.mode.new", to_string(after).c_str());
  ```

- Note: `MonitoredMode::set()` (line 304 in `Consensus.h`) calls
  `adaptor_.onModeChange(before, after)` — so the span is created in the
  adaptor, which already has telemetry access. No instrumentation needed
  in `Consensus.h` for this task.

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp` — `onModeChange()`

---

## Task 4a.8: Reparent Existing Spans Under Round

**Objective**: Make existing consensus spans (`consensus.accept`,
`consensus.accept.apply`, `consensus.validation.send`) children of the
`consensus.round` root span instead of being standalone.

**What to do**:

- The existing spans in `onAccept()`, `doAccept()`, and `validate()` use
  `XRPL_TRACE_CONSENSUS(app_.getTelemetry(), ...)` which creates standalone
  spans on the current thread's context.
- After Task 4a.2 creates the round span and stores it, these methods run on
  the same thread within the round span's scope, so they automatically become
  children. Verify this works correctly.
- For `consensus.validation.send`: add a **span link** (follows-from) to the
  round span context, since the validation may be processed after the round
  completes.

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp` — verify parent-child hierarchy

---

## Task 4a.9: Build Verification and Testing

**Objective**: Verify all Phase 4a changes compile cleanly with telemetry ON
and OFF, and don't affect consensus timing.

**What to do**:

1. Build with `telemetry=ON` — verify no compilation errors
2. Build with `telemetry=OFF` — verify macros expand to no-ops, no new includes
   leak into `Consensus.h` when disabled
3. Run existing consensus unit tests
4. Verify `#ifdef XRPL_ENABLE_TELEMETRY` guards on all new members in
   `Consensus.h`
5. Run `pccl` pre-commit checks

**Verification Checklist**:

- [x] Build succeeds with telemetry ON
- [x] Build succeeds with telemetry OFF
- [x] Existing consensus tests pass
- [x] `Consensus.h` has zero OTel includes when telemetry is OFF
- [x] No new virtual calls in hot consensus paths
- [x] `pccl` passes

---

## Phase 4a Summary

| Task | Description                                      | New Files | Modified Files | Depends On |
| ---- | ------------------------------------------------ | --------- | -------------- | ---------- |
| 4a.0 | Prerequisites: extend SpanGuard & Telemetry APIs | 0         | 4              | Phase 4    |
| 4a.1 | Adaptor `getTelemetry()` method                  | 0         | 2              | Phase 4    |
| 4a.2 | Switchable round span with deterministic traceID | 0         | 3              | 4a.0, 4a.1 |
| 4a.3 | Span members in `Consensus.h`                    | 0         | 1              | 4a.1       |
| 4a.4 | Instrument `phaseEstablish()`                    | 0         | 1              | 4a.3       |
| 4a.5 | Instrument `updateOurPositions()`                | 0         | 1              | 4a.0, 4a.3 |
| 4a.6 | Instrument `haveConsensus()` (thresholds)        | 0         | 1              | 4a.3       |
| 4a.7 | Instrument mode changes                          | 0         | 1              | 4a.1       |
| 4a.8 | Reparent existing spans under round              | 0         | 1              | 4a.0, 4a.2 |
| 4a.9 | Build verification and testing                   | 0         | 0              | 4a.0-4a.8  |

**Parallel work**: Tasks 4a.0 and 4a.1 can run in parallel. Tasks 4a.4, 4a.5, 4a.6, and 4a.7 can run in parallel after 4a.3 (and 4a.0 for 4a.5).

### New Spans (Phase 4a)

| Span Name                    | Location           | Key Attributes                                                                     |
| ---------------------------- | ------------------ | ---------------------------------------------------------------------------------- |
| `consensus.round`            | `RCLConsensus.cpp` | `round_id`, `ledger_id`, `ledger.seq`, `mode`; link → prev round                   |
| `consensus.establish`        | `Consensus.h`      | `converge_percent`, `establish_count`, `proposers`                                 |
| `consensus.update_positions` | `Consensus.h`      | `disputes_count`, `converge_percent`, `proposers_agreed`, `proposers_total`        |
| `consensus.check`            | `Consensus.h`      | `agree_count`, `disagree_count`, `converge_percent`, `result`, `threshold_percent` |
| `consensus.mode_change`      | `RCLConsensus.cpp` | `mode.old`, `mode.new`                                                             |

### New Events (Phase 4a)

| Event Name        | Parent Span                  | Attributes                          |
| ----------------- | ---------------------------- | ----------------------------------- |
| `dispute.resolve` | `consensus.update_positions` | `tx_id`, `our_vote`, `yays`, `nays` |

### New Attributes (Phase 4a)

```cpp
// Round-level (on consensus.round)
"xrpl.consensus.round_id"              = int64    // Consensus round number
"xrpl.consensus.ledger_id"             = string   // previousLedger.id() hash
"xrpl.consensus.trace_strategy"        = string   // "deterministic" or "attribute"

// Establish-level
"xrpl.consensus.converge_percent"      = int64    // Convergence % (0-100+)
"xrpl.consensus.establish_count"       = int64    // Number of establish iterations
"xrpl.consensus.disputes_count"        = int64    // Active disputes
"xrpl.consensus.proposers_agreed"      = int64    // Peers agreeing with us
"xrpl.consensus.proposers_total"       = int64    // Total peer positions
"xrpl.consensus.agree_count"           = int64    // Peers that agree (haveConsensus)
"xrpl.consensus.disagree_count"        = int64    // Peers that disagree
"xrpl.consensus.threshold_percent"     = int64    // Current threshold (50/65/70/95)
"xrpl.consensus.result"                = string   // "yes", "no", "moved_on"

// Mode change
"xrpl.consensus.mode.old"              = string   // Previous mode
"xrpl.consensus.mode.new"              = string   // New mode
```

### Implementation Notes

- **Separation of concerns**: All non-trivial telemetry code extracted to private
  helpers (`startRoundTracing`, `createValidationSpan`, `startEstablishTracing`,
  `updateEstablishTracing`, `endEstablishTracing`). Business logic methods contain
  only single-line `#ifdef` blocks calling these helpers.
- **Thread safety**: `createValidationSpan()` runs on the jtACCEPT worker thread.
  Instead of accessing `roundSpan_` across threads, a `roundSpanContext_` snapshot
  (lightweight `SpanContext` value type) is captured on the consensus thread in
  `startRoundTracing()` and read by `createValidationSpan()`. The job queue
  provides the happens-before guarantee.
- **Macro safety**: `XRPL_TRACE_ADD_EVENT` uses `do { } while (0)` to prevent
  dangling-else issues.
- **Config validation**: `consensus_trace_strategy` is validated to be either
  `"deterministic"` or `"attribute"`, falling back to `"deterministic"` for
  unrecognised values.
- **Plan deviation**: `roundSpan_` is stored in `RCLConsensus::Adaptor` (not
  `Consensus.h`) because the adaptor has access to telemetry config and can
  implement the deterministic trace ID strategy. `establishSpan_` is correctly
  in `Consensus.h` as planned.

---

# Phase 4b: Cross-Node Propagation (Future — Documentation Only)

> **Goal**: Wire `TraceContextPropagator` for P2P messages so that proposals
> and validations carry trace context between nodes. This enables true
> distributed tracing where a proposal sent by Node A creates a child span
> on Node B.
>
> **Status**: NOT IMPLEMENTED. The protobuf fields and propagator class exist
> but are not wired. This section documents the design for future work.

## Architecture

```
Node A (proposing)                         Node B (receiving)
─────────────────                         ──────────────────
consensus.round                           consensus.round
├── propose()                             ├── peerProposal()
│   └── TraceContextPropagator            │   └── TraceContextPropagator
│       ::injectToProtobuf(               │       ::extractFromProtobuf(
│           TMProposeSet.trace_context)   │           TMProposeSet.trace_context)
│                                         │   └── span link → Node A's context
└── validate()                            └── onValidation()
    └── inject into TMValidation              └── extract from TMValidation
```

## Wiring Points

| Message         | Inject Location                    | Extract Location                    | Protobuf Field             |
| --------------- | ---------------------------------- | ----------------------------------- | -------------------------- |
| `TMProposeSet`  | `Adaptor::propose()`               | `PeerImp::onMessage(TMProposeSet)`  | field 1001: `TraceContext` |
| `TMValidation`  | `Adaptor::validate()`              | `PeerImp::onMessage(TMValidation)`  | field 1001: `TraceContext` |
| `TMTransaction` | `NetworkOPs::processTransaction()` | `PeerImp::onMessage(TMTransaction)` | field 1001: `TraceContext` |

## Span Link Semantics

Received messages use **span links** (follows-from), NOT parent-child:

- The receiver's processing span links to the sender's context
- This preserves each node's independent trace tree
- Cross-node correlation visible via linked traces in Tempo/Jaeger

## Interaction with Deterministic Trace ID (Strategy A)

When using deterministic trace_id (Phase 4a default), cross-node spans already
share the same trace_id. P2P propagation adds **span-level** linking:

- Without propagation: spans from different nodes appear in the same trace
  (same trace_id) but without parent-child or follows-from relationships.
- With propagation: spans have explicit links showing which proposal/validation
  from Node A caused processing on Node B.

## Prerequisites

- Phase 4a (this task list) — establish phase tracing must be in place
- `TraceContextPropagator` class (already exists in
  `include/xrpl/telemetry/TraceContextPropagator.h`)
- Protobuf `TraceContext` message (already exists, field 1001)
