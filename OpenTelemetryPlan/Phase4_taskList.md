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

## Task 4.1: Instrument Consensus Round Start ✅

**Objective**: Create a root span for each consensus round that captures the round's key parameters.

**Status**: DONE (implemented via Task 4a.2 `startRoundTracing()` helper).

**What was done**:

- `RCLConsensus::Adaptor::startRoundTracing()` creates `consensus.round` span
  via `SpanGuard::hashSpan()` (deterministic) or `SpanGuard::span()` (attribute strategy)
- Attributes set: `xrpl.consensus.ledger_id`, `xrpl.consensus.ledger.seq`,
  `xrpl.consensus.mode`, `xrpl.consensus.trace_strategy`, `xrpl.consensus.round_id`
- Round span stored as `roundSpan_` member in `RCLConsensus::Adaptor`
- `roundSpanContext_` snapshot captured for cross-thread span linking

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`
- `src/xrpld/app/consensus/RCLConsensus.h` (span and context members)

**Reference**:

- [04-code-samples.md §4.5.2](./04-code-samples.md) — startRound instrumentation example
- [01-architecture-analysis.md §1.4](./01-architecture-analysis.md) — Consensus round flow

---

## Task 4.2: Instrument Phase Transitions — PARTIALLY DONE

**Objective**: Create child spans for each consensus phase (open, establish, accept) to show timing breakdown.

**Status**: Partially implemented. Instead of `consensus.phase.{open,establish,accept}` spans with a `phase` attribute, the implementation uses distinct span names per lifecycle stage:

- `consensus.establish` — created in `Consensus.h::startEstablishTracing()`
- `consensus.ledger_close` — created in `RCLConsensus.cpp::onClose()`
- `consensus.accept` / `consensus.accept.apply` — created in `onAccept()` / `doAccept()`

**Not implemented**:

- `consensus.phase.open` span — open phase is not separately instrumented
- `xrpl.consensus.phase` attribute — phases are distinguished by span names instead
- `phase.enter` / `phase.exit` events — not added (span start/end serves this purpose)
- `xrpl.consensus.phase_duration_ms` attribute — not set (span duration captures this)

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`
- `src/xrpld/consensus/Consensus.h` (template-level establish phase tracking)

**Reference**:

- [04-code-samples.md §4.5.2](./04-code-samples.md) — phaseTransition instrumentation

---

## Task 4.3: Instrument Proposal Handling — PARTIALLY DONE

**Objective**: Trace proposal send and receive to show validator coordination.

**Status**: Only `consensus.proposal.send` is implemented.

**What was done**:

- In `Adaptor::propose()`:
  - Creates `consensus.proposal.send` span via `SpanGuard::span()`
  - Sets `xrpl.consensus.round` attribute

**Not implemented** (deferred to Phase 4b — cross-node propagation):

- `consensus.proposal.receive` span in `peerProposal()` — requires trace context extraction from protobuf
- `consensus.proposal.relay` span in `share(RCLCxPeerPos)` — requires trace context injection
- Trace context injection/extraction for `TMProposeSet::trace_context`

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`

**Reference**:

- [04-code-samples.md §4.5.2](./04-code-samples.md) — peerProposal instrumentation
- [02-design-decisions.md §2.4.4](./02-design-decisions.md) — Consensus attribute schema

---

## Task 4.4: Instrument Validation Handling — PARTIALLY DONE

**Objective**: Trace validation send and receive to show ledger validation flow.

**Status**: Only `consensus.validation.send` is implemented.

**What was done**:

- In `Adaptor::validate()` (called from `doAccept()`):
  - Creates `consensus.validation.send` span via `Adaptor::createValidationSpan()`
  - Uses `SpanGuard::linkedSpan()` to create a follows-from link to the round span
  - Thread-safe: uses `roundSpanContext_` snapshot (captured on consensus thread,
    read on jtACCEPT thread)
  - Sets `xrpl.consensus.ledger.seq` and `xrpl.consensus.proposing` attributes

**Not implemented** (deferred to Phase 4b — cross-node propagation):

- `consensus.validation.receive` span — requires trace context extraction from `TMValidation`
- Validated ledger hash, signing time attributes on send span (see Task 4.8)

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`

---

## Task 4.5: Add Consensus-Specific Attributes — PARTIALLY DONE

**Objective**: Enrich consensus spans with detailed attributes for debugging and analysis.

**Status**: Most core attributes are set across various spans. Some originally planned attributes were not implemented because the span design made them redundant.

**Implemented attributes** (across various spans):

- `xrpl.consensus.ledger.seq` — on `consensus.round`, `consensus.accept.apply`
- `xrpl.consensus.round` — on `consensus.proposal.send`
- `xrpl.consensus.mode` — on `consensus.round`, `consensus.ledger_close`
- `xrpl.consensus.proposers` — on `consensus.accept`, `consensus.establish`, `consensus.update_positions`
- `xrpl.consensus.converge_percent` — on `consensus.establish`, `consensus.update_positions`, `consensus.check`

**Not implemented**:

- `xrpl.consensus.phase` — phases distinguished by span names instead
- `xrpl.consensus.phase_duration_ms` — span duration captures this
- `xrpl.consensus.tx_count` — transactions in proposed set not recorded
- `xrpl.consensus.disputes` — dispute count not set as span attribute (individual dispute events recorded instead via `dispute.resolve`)

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`
- `src/xrpld/consensus/Consensus.h`

---

## Task 4.6: Correlate Transaction and Consensus Traces — NOT DONE

**Objective**: Link transaction traces from Phase 3 with consensus traces so you can follow a transaction from submission through consensus into the ledger.

**Status**: Not implemented. No tx-consensus correlation exists. `NetworkOPs.cpp` was not modified.

**What was planned**:

- In `onClose()` or `onAccept()`:
  - Link the round span to individual transaction spans using span links or events
  - Record `tx.included` events with `xrpl.tx.hash` attribute

- In `processTransactionSet()` (NetworkOPs):
  - Create child spans for each transaction applied to the ledger

**Key files (not modified)**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`
- `src/xrpld/app/misc/NetworkOPs.cpp`

---

## Task 4.7: Build Verification and Testing ✅

**Objective**: Verify all Phase 4 changes compile and don't affect consensus timing.

**What to do**:

1. Build with `telemetry=ON` — verify no compilation errors
2. Build with `telemetry=OFF` — verify no regressions (critical for consensus code)
3. Run existing consensus-related unit tests
4. Verify that `SpanGuard` factory methods compile to no-ops when disabled
5. Check that no consensus-critical code paths are affected by instrumentation overhead

**Verification Checklist**:

- [x] Build succeeds with telemetry ON
- [x] Build succeeds with telemetry OFF
- [x] Existing consensus tests pass
- [x] `SpanGuard` no-op implementation prevents overhead when telemetry is OFF
- [x] Phase timing instrumentation doesn't use blocking operations

---

## Task 4.8: Consensus Validation Span Enrichment — NOT DONE

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md) — adds validation agreement context inspired by the community [xrpl-validator-dashboard](https://github.com/realgrapedrop/xrpl-validator-dashboard).
>
> **Upstream**: Phase 4 tasks 4.1-4.4 (span creation must exist).
> **Downstream**: Phase 7 (ValidationTracker reads these attributes), Phase 10 (validation checks).

**Objective**: Add ledger hash, validation type, and quorum data to consensus validation spans on both send and receive paths. This enables trace-level validation agreement analysis — filter by ledger hash to see which validators agreed for a given ledger.

**Status**: Not implemented. None of the enrichment attributes are set. The `consensus.validation.send` span only has `ledger.seq` and `proposing`. The `consensus.accept` span has `quorum` set to `result.proposers` (not the actual validator quorum from `app_.validators().quorum()`). No `PeerImp.cpp` changes were made.

**What to do**:

- Edit `src/xrpld/app/consensus/RCLConsensus.cpp`:
  - On the `consensus.validation.send` span (in `validate()` / `doAccept()`):
    - Add `xrpl.validation.ledger_hash` (string) — the ledger hash being validated
    - Add `xrpl.validation.full` (bool) — whether this is a full validation (not partial)
  - On the `consensus.accept` span (in `onAccept()`):
    - Add `xrpl.consensus.validation_quorum` (int64) — from `app_.validators().quorum()`
    - Add `xrpl.consensus.proposers_validated` (int64) — from `result.proposers`

- Edit `src/xrpld/overlay/detail/PeerImp.cpp`:
  - On the `peer.validation.receive` span:
    - Add `xrpl.peer.validation.ledger_hash` (string) — from deserialized `STValidation` object
    - Add `xrpl.peer.validation.full` (bool) — from `STValidation` flags

**New span attributes**:

| Span                        | Attribute                            | Type   | Source                            |
| --------------------------- | ------------------------------------ | ------ | --------------------------------- |
| `consensus.validation.send` | `xrpl.validation.ledger_hash`        | string | Ledger hash from validate() args  |
| `consensus.validation.send` | `xrpl.validation.full`               | bool   | Full vs partial validation        |
| `peer.validation.receive`   | `xrpl.peer.validation.ledger_hash`   | string | From STValidation deserialization |
| `peer.validation.receive`   | `xrpl.peer.validation.full`          | bool   | From STValidation flags           |
| `consensus.accept`          | `xrpl.consensus.validation_quorum`   | int64  | `app_.validators().quorum()`      |
| `consensus.accept`          | `xrpl.consensus.proposers_validated` | int64  | `result.proposers`                |

**Rationale**: The external dashboard's most valuable feature is validation agreement tracking. By recording the ledger hash on both outgoing and incoming validation spans, we create the raw data for agreement analysis at the trace level. Example Tempo query:

```
{name="consensus.validation.send"} | xrpl.validation.ledger_hash = "A1B2C3..."
```

Phase 7's `ValidationTracker` builds metric-level aggregation (1h/24h agreement %) on top of this data.

**Key modified files (not yet modified)**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`
- `src/xrpld/overlay/detail/PeerImp.cpp`

**Exit Criteria**:

- [ ] `consensus.validation.send` spans carry `xrpl.validation.ledger_hash` and `xrpl.validation.full`
- [ ] `peer.validation.receive` spans carry `xrpl.peer.validation.ledger_hash` and `xrpl.peer.validation.full`
- [ ] `consensus.accept` spans carry `xrpl.consensus.validation_quorum` and `xrpl.consensus.proposers_validated`
- [ ] Ledger hash attributes match between send and receive for the same ledger
- [ ] No impact on consensus performance

---

## Summary

| Task | Description                                 | Status                 | New Files | Modified Files | Depends On    |
| ---- | ------------------------------------------- | ---------------------- | --------- | -------------- | ------------- |
| 4.1  | Consensus round start instrumentation       | ✅ Done                | 0         | 2              | Phase 3       |
| 4.2  | Phase transition instrumentation            | ⚠️ Partial             | 0         | 1-2            | 4.1           |
| 4.3  | Proposal handling instrumentation           | ⚠️ Partial (send only) | 0         | 1              | 4.1           |
| 4.4  | Validation handling instrumentation         | ⚠️ Partial (send only) | 0         | 1-2            | 4.1           |
| 4.5  | Consensus-specific attributes               | ⚠️ Partial             | 0         | 1              | 4.2, 4.3, 4.4 |
| 4.6  | Transaction-consensus correlation           | ❌ Not done            | 0         | 2              | 4.2, Phase 3  |
| 4.7  | Build verification and testing              | ✅ Done                | 0         | 0              | 4.1-4.6       |
| 4.8  | Validation span enrichment (ext. dashboard) | ❌ Not done            | 0         | 2              | 4.4           |

**Parallel work**: Tasks 4.2, 4.3, and 4.4 can run in parallel after 4.1 is complete. Task 4.5 depends on all three. Task 4.6 depends on 4.2 and Phase 3. Task 4.8 depends on 4.4 (validation spans must exist).

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
- [x] Phase transitions visible (establish, close, accept — no separate open phase span)
- [ ] Proposals and validations traced — send only; receive/relay deferred to Phase 4b
- [x] Close time agreement tracked (per `avCT_CONSENSUS_PCT`)
- [x] No impact on consensus timing
- [ ] Transaction-consensus correlation (Task 4.6) — not implemented
- [ ] Validation span enrichment (Task 4.8) — not implemented

---

# Phase 4a: Establish-Phase Gap Fill & Cross-Node Correlation

> **Goal**: Fill tracing gaps in the consensus establish phase (disputes, convergence,
> threshold escalation, mode changes) and establish cross-node correlation using a
> deterministic shared trace ID derived from `previousLedger.id()`.
>
> **Approach**: Direct instrumentation in `Consensus.h` and `RCLConsensus.cpp`.
> All spans use `SpanGuard` factory methods (`span()`, `hashSpan()`, `linkedSpan()`)
> with `TraceCategory::Consensus` gating. Long-lived spans (round, establish) are
> stored as `std::optional<SpanGuard>` class members. Short-lived scoped spans
> (update_positions, check) are local variables. No macros are used — all tracing
> is via direct `SpanGuard` API calls. `SpanGuard` compiles to no-ops when
> telemetry is disabled.
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

The C++ API to query this at runtime is `Telemetry::getConsensusTraceStrategy()`,
which returns a `std::string const&` (`"deterministic"` or `"attribute"`).

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

## Task 4a.0: Prerequisites — Extend SpanGuard and Telemetry APIs ✅

**Objective**: Add missing API surface needed by later tasks.

**Status**: Done, but implemented differently than originally planned. The macro-based
approach (`XRPL_TRACE_CONSENSUS`, `XRPL_TRACE_ADD_EVENT`, `XRPL_TRACE_SET_ATTR`) was
**not used**. Instead, all consensus tracing uses `SpanGuard` factory methods and
direct method calls, which is cleaner and avoids macro control-flow issues.

**What was done**:

1. **`SpanGuard::addEvent()` with attributes** — implemented as planned:

   ```cpp
   using EventAttribute = std::pair<std::string_view, std::string_view>;

   void addEvent(std::string_view name,
       std::initializer_list<EventAttribute> attrs);
   ```

   Callers pass plain `string_view` pairs; the implementation converts internally.

   ```cpp
   // Actual usage in Consensus.h::updateOurPositions():
   span.addEvent(
       "dispute.resolve",
       {{cons_span::attr::txId, to_string(txId)},
        {cons_span::attr::disputeOurVote, dispute.getOurVote() ? "yes" : "no"}});
   ```

2. **Span link support** — implemented via `SpanGuard::linkedSpan()` static factory
   instead of a `Telemetry::startSpan()` overload:

   ```cpp
   static SpanGuard linkedSpan(
       std::string_view name, SpanContext const& linkTarget);
   ```

3. **No macros added** — `TracingInstrumentation.h` was not created. The `XRPL_TRACE_CONSENSUS`,
   `XRPL_TRACE_ADD_EVENT`, and `XRPL_TRACE_SET_ATTR` macros from the original plan were
   not implemented. All consensus tracing uses direct `SpanGuard` API:
   - `SpanGuard::span()` — create scoped spans
   - `SpanGuard::hashSpan()` — create spans with deterministic trace IDs
   - `SpanGuard::linkedSpan()` — create spans with follows-from links
   - `span.setAttribute()` — set attributes directly
   - `span.addEvent()` — add events directly

**Key modified files**:

- `include/xrpl/telemetry/SpanGuard.h` — `addEvent()` overload, `EventAttribute` type alias
- `src/libxrpl/telemetry/SpanGuard.cpp` — `addEvent()` implementation

---

## Task 4a.1: Adaptor `getTelemetry()` Method — NOT DONE (Not Needed)

**Objective**: Give `Consensus.h` access to the telemetry subsystem without
coupling the generic template to OTel headers.

**Status**: Not implemented as specified. The `getTelemetry()` adaptor method was
not needed because `SpanGuard::span()` is a static factory method that internally
checks telemetry state via the global `Telemetry` singleton. `Consensus.h` creates
spans by calling `SpanGuard::span(TraceCategory::Consensus, ...)` directly, without
needing adaptor access. Only `RCLConsensus::Adaptor` uses `app_.getTelemetry()`
directly (for `getConsensusTraceStrategy()` in `startRoundTracing()`).

**Key insight**: The `XRPL_TRACE_*` macro approach would have required
`adaptor_.getTelemetry()`. Since macros were not used, this task became unnecessary.

---

## Task 4a.2: Switchable Round Span with Deterministic Trace ID ✅

**Objective**: Create a `consensus.round` root span in `startRound()` that uses
the switchable correlation strategy. Store span context as a member for child
spans in `Consensus.h`.

**Status**: Done. Implemented in `Adaptor::startRoundTracing()`.

**What was done**:

- `RCLConsensus::Adaptor::startRoundTracing()` helper:
  - Reads `consensus_trace_strategy` via `app_.getTelemetry().getConsensusTraceStrategy()`
  - **Deterministic**: uses `SpanGuard::hashSpan()` with `prevLgr.id()` data
  - **Attribute**: uses `SpanGuard::span(TraceCategory::Consensus, seg::consensus, "round")`
  - Sets attributes: `ledger_id`, `ledger.seq`, `mode`, `trace_strategy`, `round_id`
  - Captures `roundSpanContext_` snapshot for cross-thread span linking
  - Saves `prevRoundContext_` from previous round for follows-from links

- **`SpanGuard::hashSpan()` factory**: encapsulates deterministic trace ID logic:

  ```cpp
  static SpanGuard hashSpan(
      TraceCategory cat, std::string_view name,
      std::uint8_t const* hashData, std::size_t hashSize);
  ```

  Derives `trace_id = hashData[0:16]` so all nodes in the same round share
  the same trace_id. Compiles to no-op when telemetry is disabled.

- `consensus_trace_strategy` config parsed in `TelemetryConfig.cpp`,
  stored in `Telemetry::Setup`, accessible via `Telemetry::getConsensusTraceStrategy()`

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp` — `startRoundTracing()` implementation
- `src/xrpld/app/consensus/ConsensusSpanNames.h` — **(new)** compile-time span name and attribute key constants
- `include/xrpl/telemetry/Telemetry.h` — `consensusTraceStrategy` in Setup, `getConsensusTraceStrategy()`
- `src/libxrpl/telemetry/TelemetryConfig.cpp` — parse new config option

---

## Task 4a.3: Span Members in `Consensus.h` ✅

**Objective**: Add span storage to the `Consensus` class so that spans created
in `startRound()` (adaptor) are accessible from `phaseEstablish()`,
`updateOurPositions()`, and `haveConsensus()` (template methods).

**Status**: Done with documented plan deviation.

**What was done**:

- `establishSpan_` added to `Consensus` private members (as planned):

  ```cpp
  std::optional<xrpl::telemetry::SpanGuard> establishSpan_;
  ```

- **Plan deviation**: `roundSpan_`, `prevRoundContext_`, and `roundSpanContext_`
  are stored in `RCLConsensus::Adaptor` (not `Consensus.h`) because the adaptor
  has access to telemetry config for the deterministic trace ID strategy.

- **No `#ifdef XRPL_ENABLE_TELEMETRY` guards**: Members use `std::optional<SpanGuard>`
  and `SpanContext` which have no-op implementations when telemetry is disabled,
  so `#ifdef` guards are unnecessary. The members are always present in the class
  layout but incur negligible overhead.

- Includes added unconditionally to `Consensus.h`:
  ```cpp
  #include <xrpl/telemetry/SpanGuard.h>
  #include <xrpld/app/consensus/ConsensusSpanNames.h>
  ```
  No `TracingInstrumentation.h` include (file doesn't exist; macros not used).

**Key modified files**:

- `src/xrpld/consensus/Consensus.h`
- `src/xrpld/app/consensus/RCLConsensus.h` (round span and context members)

---

## Task 4a.4: Instrument `phaseEstablish()` ✅

**Objective**: Create `consensus.establish` span wrapping the establish phase,
with attributes for convergence progress.

**Status**: Done. Implemented via three private helpers in `Consensus.h`.

**What was done**:

- `startEstablishTracing()` — creates `consensus.establish` span via
  `SpanGuard::span(TraceCategory::Consensus, seg::consensus, "establish")`.
  Called once at start of establish phase. No `#ifdef` guards needed —
  `SpanGuard::span()` returns a no-op guard when telemetry is disabled.

- `updateEstablishTracing()` — sets attributes on each `phaseEstablish()` call:
  - `xrpl.consensus.converge_percent` — `convergePercent_`
  - `xrpl.consensus.establish_count` — `establishCounter_`
  - `xrpl.consensus.proposers` — `currPeerPositions_.size()`

- `endEstablishTracing()` — calls `establishSpan_.reset()` on phase exit.

**Key modified files**:

- `src/xrpld/consensus/Consensus.h` — `phaseEstablish()` method + 3 helper methods

---

## Task 4a.5: Instrument `updateOurPositions()` — PARTIALLY DONE

**Objective**: Trace each position update cycle including dispute resolution
details.

**Status**: Partially done. Span and dispute events are created, but some planned
attributes and event fields are missing.

**What was done**:

- Creates `consensus.update_positions` scoped span via
  `SpanGuard::span(TraceCategory::Consensus, seg::consensus, "update_positions")`:

  ```cpp
  auto span = SpanGuard::span(TraceCategory::Consensus, seg::consensus, "update_positions");
  ```

- Attributes set:
  - `xrpl.consensus.converge_percent` — current convergence
  - `xrpl.consensus.proposers` — `currPeerPositions_.size()`
  - `xrpl.consensus.have_close_time_consensus` — close time consensus state
  - `xrpl.consensus.close_time_threshold` — `avCT_CONSENSUS_PCT`

- Dispute events recorded via direct `span.addEvent()` call:
  ```cpp
  span.addEvent(
      "dispute.resolve",
      {{cons_span::attr::txId, to_string(txId)},
       {cons_span::attr::disputeOurVote, dispute.getOurVote() ? "yes" : "no"}});
  ```

**Not implemented**:

- `xrpl.consensus.disputes_count` attribute — not set (individual events recorded instead)
- `xrpl.consensus.proposers_agreed` / `xrpl.consensus.proposers_total` attributes — not set
- `xrpl.dispute.yays` / `xrpl.dispute.nays` event fields — not included in `dispute.resolve`
  events despite `DisputedTx::getYays()` and `getNays()` accessors being added for this purpose

**Key modified files**:

- `src/xrpld/consensus/Consensus.h` — `updateOurPositions()` method
- `src/xrpld/consensus/DisputedTx.h` — added `getYays()` / `getNays()` (currently unused)

---

## Task 4a.6: Instrument `haveConsensus()` (Threshold & Convergence) — PARTIALLY DONE

**Objective**: Trace consensus checking including threshold escalation.

**Status**: Mostly done. The `consensus.check` span is created with most planned
attributes. The avalanche threshold is not recorded.

**What was done**:

- Creates `consensus.check` scoped span via
  `SpanGuard::span(TraceCategory::Consensus, seg::consensus, "check")`:

  ```cpp
  auto span = SpanGuard::span(TraceCategory::Consensus, seg::consensus, "check");
  ```

- Attributes set:
  - `xrpl.consensus.agree_count` — peers that agree with our position
  - `xrpl.consensus.disagree_count` — peers that disagree
  - `xrpl.consensus.converge_percent` — convergence percentage
  - `xrpl.consensus.have_close_time_consensus` — close time consensus state
  - `xrpl.consensus.threshold_percent` — set to `avCT_CONSENSUS_PCT` (75%)
  - `xrpl.consensus.result` — "yes", "no", or "moved_on"

**Not implemented**:

- `xrpl.consensus.avalanche_threshold` — the escalated weight from `getNeededWeight()`
  is not recorded. The attribute key constant exists in `ConsensusSpanNames.h`
  (`cons_span::attr::avalancheThreshold`) but is never used in the implementation.

**Key modified files**:

- `src/xrpld/consensus/Consensus.h` — `haveConsensus()` method

---

## Task 4a.7: Instrument Mode Changes ✅

**Objective**: Trace consensus mode transitions (proposing ↔ observing,
wrongLedger, switchedLedger).

**Status**: Done.

**What was done**:

- In `RCLConsensus::Adaptor::onModeChange()`, creates a scoped span via direct
  `SpanGuard::span()` call:

  ```cpp
  auto span = telemetry::SpanGuard::span(
      telemetry::TraceCategory::Consensus, telemetry::seg::consensus, "mode_change");
  span.setAttribute(cons_span::attr::modeOld, to_string(before).c_str());
  span.setAttribute(cons_span::attr::modeNew, to_string(after).c_str());
  ```

- `MonitoredMode::set()` in `Consensus.h` calls `adaptor_.onModeChange(before, after)`.

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp` — `onModeChange()`

---

## Task 4a.8: Reparent Existing Spans Under Round — PARTIALLY DONE

**Objective**: Make existing consensus spans (`consensus.accept`,
`consensus.accept.apply`, `consensus.validation.send`) children of the
`consensus.round` root span instead of being standalone.

**Status**: Partially done. `consensus.validation.send` has a span link to the
round. Other spans are created via `SpanGuard::span()` which creates standalone
spans — they are NOT automatically parented under the round span.

**What was done**:

- `consensus.validation.send` uses `SpanGuard::linkedSpan()` to create a
  follows-from link to `roundSpanContext_`. This is thread-safe because
  `roundSpanContext_` is a lightweight `SpanContext` snapshot captured on the
  consensus thread and read on the jtACCEPT worker thread.

**Not working as expected**:

- `consensus.accept` and `consensus.accept.apply` are created via
  `SpanGuard::span()` which starts standalone spans. They are NOT automatically
  parented under `consensus.round` because:
  - `doAccept()` runs on the jtACCEPT worker thread (not the consensus thread)
  - The round span's `Scope` is only active on the consensus thread
  - Automatic OTel thread-local context propagation does not cross threads

**Key modified files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp`

---

## Task 4a.9: Build Verification and Testing ✅

**Objective**: Verify all Phase 4a changes compile cleanly with telemetry ON
and OFF, and don't affect consensus timing.

**What to do**:

1. Build with `telemetry=ON` — verify no compilation errors
2. Build with `telemetry=OFF` — verify `SpanGuard` compiles to no-ops
3. Run existing consensus unit tests
4. Verify `SpanGuard` / `SpanContext` members have negligible overhead when disabled
5. Run `pccl` pre-commit checks

**Verification Checklist**:

- [x] Build succeeds with telemetry ON
- [x] Build succeeds with telemetry OFF
- [x] Existing consensus tests pass
- [x] `SpanGuard` no-op path verified (no `#ifdef` needed — disabled at runtime)
- [x] No new virtual calls in hot consensus paths
- [x] `pccl` passes

---

## Phase 4a Summary

| Task | Description                                      | Status                    | New Files | Modified Files | Depends On |
| ---- | ------------------------------------------------ | ------------------------- | --------- | -------------- | ---------- |
| 4a.0 | Prerequisites: extend SpanGuard & Telemetry APIs | ✅ Done (no macros)       | 0         | 2              | Phase 4    |
| 4a.1 | Adaptor `getTelemetry()` method                  | ⏭️ Skipped (not needed)   | 0         | 0              | Phase 4    |
| 4a.2 | Switchable round span with deterministic traceID | ✅ Done                   | 1         | 3              | 4a.0       |
| 4a.3 | Span members in `Consensus.h`                    | ✅ Done (with deviation)  | 0         | 2              | —          |
| 4a.4 | Instrument `phaseEstablish()`                    | ✅ Done                   | 0         | 1              | 4a.3       |
| 4a.5 | Instrument `updateOurPositions()`                | ⚠️ Partial                | 0         | 2              | 4a.0, 4a.3 |
| 4a.6 | Instrument `haveConsensus()` (thresholds)        | ⚠️ Partial (no avalanche) | 0         | 1              | 4a.3       |
| 4a.7 | Instrument mode changes                          | ✅ Done                   | 0         | 1              | —          |
| 4a.8 | Reparent existing spans under round              | ⚠️ Partial (link only)    | 0         | 1              | 4a.0, 4a.2 |
| 4a.9 | Build verification and testing                   | ✅ Done                   | 0         | 0              | 4a.0-4a.8  |

**Parallel work**: Tasks 4a.0 and 4a.1 can run in parallel. Tasks 4a.4, 4a.5, 4a.6, and 4a.7 can run in parallel after 4a.3 (and 4a.0 for 4a.5).

### New Spans (Phase 4a)

| Span Name                    | Location           | Key Attributes (actually set)                                                                                   |
| ---------------------------- | ------------------ | --------------------------------------------------------------------------------------------------------------- |
| `consensus.round`            | `RCLConsensus.cpp` | `round_id`, `ledger_id`, `ledger.seq`, `mode`, `trace_strategy`                                                 |
| `consensus.establish`        | `Consensus.h`      | `converge_percent`, `establish_count`, `proposers`                                                              |
| `consensus.update_positions` | `Consensus.h`      | `converge_percent`, `proposers`, `have_close_time_consensus`, `close_time_threshold`                            |
| `consensus.check`            | `Consensus.h`      | `agree_count`, `disagree_count`, `converge_percent`, `have_close_time_consensus`, `threshold_percent`, `result` |
| `consensus.mode_change`      | `RCLConsensus.cpp` | `mode.old`, `mode.new`                                                                                          |

### New Events (Phase 4a)

| Event Name        | Parent Span                  | Attributes (actually set) | Planned but not set    |
| ----------------- | ---------------------------- | ------------------------- | ---------------------- |
| `dispute.resolve` | `consensus.update_positions` | `tx_id`, `our_vote`       | `yays`, `nays` missing |

### New Attributes (Phase 4a)

```cpp
// Round-level (on consensus.round) — ALL IMPLEMENTED
"xrpl.consensus.round_id"              = int64    // Consensus round number
"xrpl.consensus.ledger_id"             = string   // previousLedger.id() hash
"xrpl.consensus.trace_strategy"        = string   // "deterministic" or "attribute"

// Establish-level — IMPLEMENTED
"xrpl.consensus.converge_percent"      = int64    // Convergence % (0-100+)
"xrpl.consensus.establish_count"       = int64    // Number of establish iterations
"xrpl.consensus.agree_count"           = int64    // Peers that agree (haveConsensus)
"xrpl.consensus.disagree_count"        = int64    // Peers that disagree
"xrpl.consensus.threshold_percent"     = int64    // Current threshold (avCT_CONSENSUS_PCT = 75%)
"xrpl.consensus.result"                = string   // "yes", "no", "moved_on"
"xrpl.consensus.have_close_time_consensus" = bool // Close time consensus reached
"xrpl.consensus.close_time_threshold"  = int64    // Close time voting threshold

// Establish-level — NOT IMPLEMENTED (constants defined but unused)
// "xrpl.consensus.disputes_count"     = int64    // Active disputes — not set
// "xrpl.consensus.proposers_agreed"   = int64    // Peers agreeing with us — not set
// "xrpl.consensus.proposers_total"    = int64    // Total peer positions — not set (not defined)
// "xrpl.consensus.avalanche_threshold" = int64   // Escalated weight — not set

// Mode change — ALL IMPLEMENTED
"xrpl.consensus.mode.old"              = string   // Previous mode
"xrpl.consensus.mode.new"              = string   // New mode
```

### Implementation Notes

- **No macros**: The planned `XRPL_TRACE_CONSENSUS`, `XRPL_TRACE_ADD_EVENT`, and
  `XRPL_TRACE_SET_ATTR` macros were not implemented. All consensus tracing uses
  `SpanGuard` factory methods (`span()`, `hashSpan()`, `linkedSpan()`) and direct
  method calls (`setAttribute()`, `addEvent()`). This avoids macro control-flow
  issues and is cleaner than the planned approach.
- **Separation of concerns**: All non-trivial telemetry code extracted to private
  helpers (`startRoundTracing`, `createValidationSpan`, `startEstablishTracing`,
  `updateEstablishTracing`, `endEstablishTracing`). Business logic methods contain
  single-line calls to these helpers.
- **Thread safety**: `createValidationSpan()` runs on the jtACCEPT worker thread.
  Instead of accessing `roundSpan_` across threads, a `roundSpanContext_` snapshot
  (lightweight `SpanContext` value type) is captured on the consensus thread in
  `startRoundTracing()` and read by `createValidationSpan()`. The job queue
  provides the happens-before guarantee.
- **No `#ifdef` guards**: Span members use `std::optional<SpanGuard>` and `SpanContext`
  which have no-op implementations when telemetry is disabled. No `#ifdef XRPL_ENABLE_TELEMETRY`
  guards needed around members or includes.
- **No `getTelemetry()` adaptor method**: `SpanGuard::span()` is a static factory that
  internally checks telemetry state, so `Consensus.h` doesn't need adaptor access
  for span creation. Only `RCLConsensus::Adaptor` accesses `app_.getTelemetry()` directly.
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
