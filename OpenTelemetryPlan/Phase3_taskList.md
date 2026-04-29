# Phase 3: Transaction Tracing Task List

> **Goal**: Trace the full transaction lifecycle from RPC submission through peer relay, including cross-node context propagation via Protocol Buffer extensions. This is the WALK phase that demonstrates true distributed tracing.
>
> **Scope**: Protocol Buffer `TraceContext` message, context serialization, PeerImp transaction instrumentation, NetworkOPs processing instrumentation, HashRouter visibility, and multi-node relay context propagation.
>
> **Branch**: `pratik/otel-phase3-tx-tracing` (from `pratik/otel-phase2-rpc-tracing`)

### Related Plan Documents

| Document                                                     | Relevance                                                                                        |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------ |
| [04-code-samples.md](./04-code-samples.md)                   | TraceContext protobuf (§4.4.1), PeerImp instrumentation (§4.5.1), context serialization (§4.4.2) |
| [01-architecture-analysis.md](./01-architecture-analysis.md) | Transaction flow (§1.3), key trace points (§1.6)                                                 |
| [06-implementation-phases.md](./06-implementation-phases.md) | Phase 3 tasks (§6.4), definition of done (§6.11.3)                                               |
| [02-design-decisions.md](./02-design-decisions.md)           | Context propagation design (§2.5), attribute schema (§2.4.3)                                     |

---

## Task 3.1: Define TraceContext Protocol Buffer Message

**Objective**: Add trace context fields to the P2P protocol messages so trace IDs can propagate across nodes.

**What to do**:

- Edit `include/xrpl/proto/xrpl.proto` (or `src/xrpld/proto/ripple.proto`, wherever the proto is):
  - Add `TraceContext` message definition:
    ```protobuf
    message TraceContext {
        bytes trace_id = 1;      // 16-byte trace identifier
        bytes span_id = 2;       // 8-byte span identifier
        uint32 trace_flags = 3;  // bit 0 = sampled
        string trace_state = 4;  // W3C tracestate value
    }
    ```
  - Add `optional TraceContext trace_context = 1001;` to:
    - `TMTransaction`
    - `TMProposeSet` (for Phase 4 use)
    - `TMValidation` (for Phase 4 use)
  - Use high field numbers (1001+) to avoid conflicts with existing fields

- Regenerate protobuf C++ code

**Key modified files**:

- `include/xrpl/proto/xrpl.proto` (or equivalent)

**Reference**:

- [04-code-samples.md §4.4.1](./04-code-samples.md) — TraceContext message definition
- [02-design-decisions.md §2.5.2](./02-design-decisions.md) — Protocol buffer context propagation design

---

## Task 3.2: Implement Protobuf Context Serialization

**Objective**: Create utilities to serialize/deserialize OTel trace context to/from protobuf `TraceContext` messages.

**What to do**:

- Create `include/xrpl/telemetry/TraceContextPropagator.h` (extend from Phase 2 if exists, or add protobuf methods):
  - Add protobuf-specific methods:
    - `static Context extractFromProtobuf(protocol::TraceContext const& proto)` — reconstruct OTel context from protobuf fields
    - `static void injectToProtobuf(Context const& ctx, protocol::TraceContext& proto)` — serialize current span context into protobuf fields
  - Both methods guard behind `#ifdef XRPL_ENABLE_TELEMETRY`

- Create/extend `src/libxrpl/telemetry/TraceContextPropagator.cpp`:
  - Implement extraction: read trace_id (16 bytes), span_id (8 bytes), trace_flags from protobuf, construct `SpanContext`, wrap in `Context`
  - Implement injection: get current span from context, serialize its TraceId, SpanId, and TraceFlags into protobuf fields

**Key new/modified files**:

- `include/xrpl/telemetry/TraceContextPropagator.h`
- `src/libxrpl/telemetry/TraceContextPropagator.cpp`

**Reference**:

- [04-code-samples.md §4.4.2](./04-code-samples.md) — Full extract/inject implementation

---

## Task 3.3: Instrument PeerImp Transaction Handling

**Objective**: Add trace spans to the peer-level transaction receive and relay path.

**What to do**:

- Edit `src/xrpld/overlay/detail/PeerImp.cpp`:
  - In `onMessage(TMTransaction)` / `handleTransaction()`:
    - Extract parent trace context from incoming `TMTransaction::trace_context` field (if present)
    - Create `tx.receive` span as child of extracted context (or new root if none)
    - Set attributes: `xrpl.tx.hash`, `xrpl.peer.id`, `xrpl.tx.status`
    - On HashRouter suppression (duplicate): set `xrpl.tx.suppressed=true`, add `tx.duplicate` event
    - Wrap validation call with child span `tx.validate`
    - Wrap relay with `tx.relay` span
  - When relaying to peers:
    - Inject current trace context into outgoing `TMTransaction::trace_context`
    - Set `xrpl.tx.relay_count` attribute

- Use `SpanGuard::span(TraceCategory::Transactions, "tx", "receive")` factory
  (Phase 1c replaced macros with the SpanGuard factory pattern)

**Key modified files**:

- `src/xrpld/overlay/detail/PeerImp.cpp`

**Reference**:

- [04-code-samples.md §4.5.1](./04-code-samples.md) — Full PeerImp instrumentation example
- [01-architecture-analysis.md §1.3](./01-architecture-analysis.md) — Transaction flow diagram
- [01-architecture-analysis.md §1.6](./01-architecture-analysis.md) — tx.receive trace point

---

## Task 3.4: Instrument NetworkOPs Transaction Processing

**Objective**: Trace the transaction processing pipeline in NetworkOPs, covering both sync and async paths.

**What to do**:

- Edit `src/xrpld/app/misc/NetworkOPs.cpp`:
  - In `processTransaction()`:
    - Create `tx.process` span
    - Set attributes: `xrpl.tx.hash`, `xrpl.tx.type`, `xrpl.tx.local` (whether from RPC or peer)
    - Record whether sync or async path is taken

  - In `doTransactionAsync()`:
    - Capture parent context before queuing
    - Create `tx.queue` span with queue depth attribute
    - Add event when transaction is dequeued for processing

  - In `doTransactionSync()`:
    - Create `tx.process_sync` span
    - Record result (applied, queued, rejected)

**Key modified files**:

- `src/xrpld/app/misc/NetworkOPs.cpp`

**Reference**:

- [01-architecture-analysis.md §1.6](./01-architecture-analysis.md) — tx.validate and tx.process trace points
- [02-design-decisions.md §2.4.3](./02-design-decisions.md) — Transaction attribute schema

---

## Task 3.5: Instrument HashRouter for Dedup Visibility

**Objective**: Make transaction deduplication visible in traces by recording HashRouter decisions as span attributes/events.

**What to do**:

- Edit `src/xrpld/overlay/detail/PeerImp.cpp` (in handleTransaction):
  - After calling `HashRouter::shouldProcess()` or `addSuppressionPeer()`:
    - Record `xrpl.tx.suppressed` attribute (true/false)
    - Record `xrpl.tx.flags` showing current HashRouter state (SAVED, TRUSTED, etc.)
    - Add `tx.first_seen` or `tx.duplicate` event

- This is NOT a modification to HashRouter itself — just recording its decisions as span attributes in the existing PeerImp instrumentation from Task 3.3.

**Key modified files**:

- `src/xrpld/overlay/detail/PeerImp.cpp` (same changes as 3.3, logically grouped)

---

## Task 3.6: Context Propagation in Transaction Relay

**Objective**: Ensure trace context flows correctly when transactions are relayed between peers, creating linked spans across nodes.

**What to do**:

- Verify the relay path injects trace context:
  - When `PeerImp` relays a transaction, the `TMTransaction` message should carry `trace_context`
  - When a remote peer receives it, the context is extracted and used as parent

- Test context propagation:
  - Manually verify with 2+ node setup that trace IDs match across nodes
  - Confirm parent-child span relationships are correct in Tempo

- Handle edge cases:
  - Missing trace context (older peers): create new root span
  - Corrupted trace context: log warning, create new root span
  - Sampled-out traces: respect trace flags

**Key modified files**:

- `src/xrpld/overlay/detail/PeerImp.cpp`
- `src/xrpld/overlay/detail/OverlayImpl.cpp` (if relay method needs context param)

**Reference**:

- [02-design-decisions.md §2.5](./02-design-decisions.md) — Context propagation design
- [04-code-samples.md §4.5.1](./04-code-samples.md) — Relay context injection pattern

---

## Task 3.7: Build Verification and Testing

**Objective**: Verify all Phase 3 changes compile and work correctly.

**What to do**:

1. Build with `telemetry=ON` — verify no compilation errors
2. Build with `telemetry=OFF` — verify no regressions
3. Run existing unit tests
4. Verify protobuf regeneration produces correct C++ code
5. Document any issues encountered

**Verification Checklist**:

- [ ] Protobuf changes generate valid C++
- [ ] Build succeeds with telemetry ON
- [ ] Build succeeds with telemetry OFF
- [ ] Existing tests pass
- [ ] No undefined symbols from new telemetry calls

---

## Task 3.8: Transaction Span Peer Version Attribute

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md) — adds peer version context inspired by the community [xrpl-validator-dashboard](https://github.com/realgrapedrop/xrpl-validator-dashboard).
>
> **Upstream**: Phase 2 (RPC span infrastructure must exist).
> **Downstream**: Phase 10 (validation checks for this attribute).

**Objective**: Add the relaying peer's xrpld version to `tx.receive` spans so operators can correlate transaction issues with peer version mismatches during network upgrades.

**What to do**:

- Edit `src/xrpld/overlay/detail/PeerImp.cpp`:
  - In the `tx.receive` span block (after existing `xrpl.peer.id` setAttribute call):
    - Add `xrpl.peer.version` (string) — from `this->getVersion()`
    - Only set if `getVersion()` returns a non-empty string (avoid empty-string attributes)

**New span attribute**:

| Attribute           | Type   | Source               | Example         |
| ------------------- | ------ | -------------------- | --------------- |
| `xrpl.peer.version` | string | `peer->getVersion()` | `"xrpld-2.4.0"` |

**Rationale**: Transaction relay is where version mismatches cause subtle serialization or validation bugs. Tracing "this tx came from a v2.3.0 peer" helps diagnose compatibility issues. The community dashboard tracks peer versions externally; this brings version awareness into the trace itself.

**Key modified files**:

- `src/xrpld/overlay/detail/PeerImp.cpp`

**Exit Criteria**:

- [ ] `tx.receive` spans carry `xrpl.peer.version` attribute with a non-empty version string
- [ ] Attribute is omitted (not set to empty string) when `getVersion()` returns empty
- [ ] Attribute visible in Jaeger span detail view

---

## Task 3.9: Deterministic Transaction Trace ID

> **Upstream**: Task 3.2 (protobuf serialization), Task 3.3 (PeerImp span exists).
> **Downstream**: Phase 10 (workload validation can query by tx hash directly).
> **Pattern**: Mirrors the consensus deterministic trace ID in Phase 4a
> (`createDeterministicContext` in `RCLConsensus.cpp`), adapted for transactions.

**Objective**: Derive the trace_id for transaction spans deterministically from the
transaction hash so that all nodes handling the same transaction independently produce
spans under the same trace_id — regardless of whether protobuf context propagation
succeeds.

**Why**: The current approach creates spans with random trace_ids and relies entirely
on protobuf `TraceContext` propagation to link them. If any hop in the relay chain
drops the context (older peers, message corruption, mixed-version networks), the trace
splits and downstream spans become impossible to find. With deterministic trace_ids,
correlation is guaranteed because every node derives the same trace_id from the same
`txID`.

**Approach — deterministic trace_id + protobuf span_id propagation**:

1. Derive `trace_id = txHash[0:16]` (first 16 bytes of the 32-byte transaction hash).
2. Generate a random 8-byte `span_id` per node (each node's span is unique within
   the shared trace).
3. Create the span under this deterministic context as parent.
4. **Additionally**, if protobuf `TraceContext` is present in the incoming
   `TMTransaction` message, extract the sender's `span_id` and use it as the span's
   parent — this preserves parent-child ordering in the trace tree.
5. If protobuf context is absent (older peer, first hop), the span still has the
   correct deterministic `trace_id` — it appears as a sibling root in the same trace
   rather than being lost.

This gives the best of both worlds: guaranteed cross-node correlation via deterministic
`trace_id`, plus parent-child relay ordering via protobuf `span_id` when available.

**What to do**:

- Create `createDeterministicTxContext(uint256 const& txHash)` utility function:
  - Location: shared header or file-local in `PeerImp.cpp` and `NetworkOPs.cpp`
    (or a shared telemetry utility if both need it).
  - Pattern: identical to `createDeterministicContext(uint256 const& ledgerId)` in
    `RCLConsensus.cpp` — take `txHash[0:16]` as trace_id, random span_id via
    `default_prng()`, sampled flag set, `remote=false`.
  - Guard behind `#ifdef XRPL_ENABLE_TELEMETRY`.

  ```cpp
  opentelemetry::context::Context
  createDeterministicTxContext(uint256 const& txHash)
  {
      namespace trace = opentelemetry::trace;

      // First 16 bytes of the 32-byte tx hash as trace ID.
      trace::TraceId traceId(
          opentelemetry::nostd::span<uint8_t const, 16>(txHash.data(), 16));

      // Random span_id so each node's span is unique within the trace.
      uint8_t spanIdBytes[8];
      auto const rval = default_prng()();
      std::memcpy(spanIdBytes, &rval, sizeof(spanIdBytes));
      trace::SpanId spanId(
          opentelemetry::nostd::span<uint8_t const, 8>(spanIdBytes, 8));

      trace::SpanContext syntheticCtx(
          traceId, spanId, trace::TraceFlags(1), /* remote = */ false);

      return opentelemetry::context::Context{}.SetValue(
          trace::kSpanKey,
          opentelemetry::nostd::shared_ptr<trace::Span>(
              new trace::DefaultSpan(syntheticCtx)));
  }
  ```

- Edit `src/xrpld/overlay/detail/PeerImp.cpp` — restructure `handleTransaction()`:
  - **Move span creation after deserialization** (txID must be known first):
    1. Deserialize `STTx` and get `txID` (existing code at line ~1382).
    2. Create deterministic parent context: `auto detCtx = createDeterministicTxContext(txID)`.
    3. If `m->has_trace_context()`: extract protobuf context via `extractFromProtobuf()`,
       **combine** with deterministic trace_id — use the protobuf span_id as parent
       to preserve relay ordering, but override trace_id with the deterministic one.
    4. If no protobuf context: create span under `detCtx` directly.
    5. Set all existing attributes (`hash`, `peerId`, `peerVersion`, `suppressed`, etc.).

  - **Combining deterministic trace_id with protobuf parent span_id**:
    When both are available, construct a synthetic `SpanContext` with:
    - `trace_id` = `txHash[0:16]` (deterministic)
    - `span_id` = extracted from protobuf (sender's span_id → becomes parent)
    - `trace_flags` = from protobuf
    - `remote` = true (came from another node)

    ```cpp
    // Pseudo-code for the combined context:
    auto detTraceId = trace::TraceId(txHash.data(), 16);
    auto remoteSpanId = /* from extractFromProtobuf */;
    auto remoteFlags = /* from extractFromProtobuf */;

    trace::SpanContext combinedCtx(
        detTraceId, remoteSpanId, remoteFlags, /* remote = */ true);
    // Use as parent context for the new span.
    ```

- Edit `src/xrpld/app/misc/NetworkOPs.cpp` — update `processTransaction()`:
  - `transaction->getID()` is already available at the top of the function.
  - Create deterministic parent context from `txID`.
  - Create `tx.process` span under this context.
  - No protobuf context to extract here (NetworkOPs is intra-node), so
    deterministic context alone is sufficient.

- Add `tx_trace_strategy` attribute to spans:
  - Add `inline constexpr auto traceStrategy = join(xrplTx, makeStr("trace_strategy"));`
    to `TxSpanNames.h`.
  - Set on each tx span: `span.setAttribute(tx_span::attr::traceStrategy, "deterministic")`.

**Key new/modified files**:

- `src/xrpld/overlay/detail/PeerImp.cpp` — restructured span creation
- `src/xrpld/app/misc/NetworkOPs.cpp` — deterministic context for tx.process
- `src/xrpld/app/misc/TxSpanNames.h` — new `traceStrategy` attribute constant
- New or shared utility for `createDeterministicTxContext()` (location TBD: could be
  a shared header like `include/xrpl/telemetry/DeterministicContext.h`, or file-local
  if only used in two places)

**Interaction with existing tasks**:

- **Task 3.3 (PeerImp instrumentation)**: The span creation in `handleTransaction()`
  must be restructured — the span currently starts before `txID` is known. This task
  moves it after deserialization.
- **Task 3.6 (Relay context propagation)**: Protobuf injection at the relay site
  remains the same — `injectToProtobuf()` serializes the current span's `span_id`.
  The receiver extracts it and combines with the deterministic `trace_id`.
- **Phase 4a (Consensus deterministic trace ID)**: This task follows the same pattern.
  Consider extracting a shared utility (e.g., `createDeterministicContext(uint256)`)
  that both consensus and transaction tracing use.

**Exit Criteria**:

- [ ] `tx.receive` and `tx.process` spans have deterministic trace_id = `txHash[0:16]`
- [ ] All nodes handling the same transaction produce spans under the same trace_id
- [ ] Protobuf `span_id` propagation still works when available (parent-child ordering)
- [ ] Missing protobuf context (old peer) degrades gracefully to sibling spans, not lost traces
- [ ] `xrpl.tx.trace_strategy` attribute set to `"deterministic"` on all tx spans
- [ ] Trace queryable by tx hash (truncate hash → trace_id → direct lookup in Tempo)

**Deliverables implemented (not in original plan)**:

- **`SpanGuard::txSpan()` factory method** (`include/xrpl/telemetry/SpanGuard.h`):
  Two overloads for creating transaction spans with deterministic trace IDs:
  - `txSpan(category, group, name, txHash)` — standalone span (deterministic
    trace_id from `txHash[0:16]`, no parent span_id).
  - `txSpan(category, group, name, txHash, parentCtx)` — child span (deterministic
    trace_id combined with protobuf-extracted parent span_id for relay ordering).

- **`TxTracing.h` helper functions** (`src/xrpld/overlay/detail/TxTracing.h`):
  File-local helpers that wrap `SpanGuard::txSpan()` for the two main PeerImp call
  sites:
  - `txReceiveSpan(txHash, parentCtx)` — creates `tx.receive` span with
    deterministic trace_id and optional protobuf parent context.
  - `txProcessSpan(txHash)` — creates `tx.process` span with deterministic
    trace_id only (no protobuf parent, used intra-node).
  - **Note**: `TxTracing.h` includes `xrpl.pb.h` unconditionally (outside
    `#ifdef XRPL_ENABLE_TELEMETRY`) because `protocol::TMTransaction` appears in
    the function signatures regardless of telemetry build mode.

---

## Task 3.10: TxQ Instrumentation

**Status**: COMPLETE

**Objective**: Trace the transaction queue lifecycle — enqueue decisions, direct apply, batch clear, ledger-close accept loop, per-tx apply, and cleanup.

**Spans added**:

- `txq.enqueue` — wraps `TxQ::apply()` with tx_hash attribute
- `txq.apply_direct` — wraps `TxQ::tryDirectApply()` fast-path
- `txq.batch_clear` — wraps `TxQ::tryClearAccountQueueUpThruTx()`
- `txq.accept` — wraps `TxQ::accept()` ledger-close dequeue with queue_size attr
- `txq.accept_tx` — per-tx span inside accept loop with tx_hash, ter_code,
  retries_remaining attributes
- `txq.cleanup` — wraps `TxQ::processClosedLedger()` with ledger_seq attribute

**New file**: `src/xrpld/app/misc/detail/TxQSpanNames.h`

**Modified file**: `src/xrpld/app/misc/detail/TxQ.cpp`

---

## Summary

| Task | Description                         | New Files | Modified Files | Depends On |
| ---- | ----------------------------------- | --------- | -------------- | ---------- |
| 3.1  | TraceContext protobuf message       | 0         | 1              | Phase 2    |
| 3.2  | Protobuf context serialization      | 1-2       | 0              | 3.1        |
| 3.3  | PeerImp transaction instrumentation | 0         | 1              | 3.2        |
| 3.4  | NetworkOPs transaction processing   | 0         | 1              | Phase 2    |
| 3.5  | HashRouter dedup visibility         | 0         | 1              | 3.3        |
| 3.6  | Relay context propagation           | 0         | 1-2            | 3.3, 3.5   |
| 3.7  | Build verification and testing      | 0         | 0              | 3.1-3.6    |
| 3.8  | TX span peer version attribute      | 0         | 1              | 3.3        |
| 3.9  | Deterministic transaction trace ID  | 0-1       | 3              | 3.2, 3.3   |
| 3.10 | TxQ instrumentation (6 spans)       | 1         | 1              | 3.4        |

**Parallel work**: Tasks 3.1 and 3.4 can start in parallel. Task 3.2 depends on 3.1. Tasks 3.3 and 3.5 depend on 3.2. Task 3.6 depends on 3.3 and 3.5. Task 3.8 depends on 3.3 (span must exist). Task 3.9 depends on 3.2 and 3.3. Task 3.10 depends on 3.4 (tx.process span must exist).

**Exit Criteria** (from [06-implementation-phases.md §6.11.3](./06-implementation-phases.md)):

- [ ] Transaction traces span across nodes
- [ ] Trace context in Protocol Buffer messages
- [ ] HashRouter deduplication visible in traces
- [ ] <5% overhead on transaction throughput
- [ ] Deterministic trace_id: same trace_id for same tx across all nodes
- [ ] Protobuf span_id propagation preserves parent-child ordering when available

---

## Known Issues / Future Work

### Propagation utilities not yet wired into P2P flow

`extractFromProtobuf()` and `injectToProtobuf()` in `TraceContextPropagator.h`
are implemented and tested but not called from production code. To enable
cross-node distributed traces:

- Call `injectToProtobuf()` in `PeerImp` when sending `TMTransaction` /
  `TMProposeSet` messages
- Call `extractFromProtobuf()` in the corresponding message handlers to
  reconstruct the parent span context, then pass it to `startSpan()` as the
  parent

This was deferred to validate single-node tracing performance first.

### Unused trace_state proto field

The `TraceContext.trace_state` field (field 4) in `xrpl.proto` is reserved for
W3C `tracestate` vendor-specific key-value pairs but is not read or written by
`TraceContextPropagator`. Wire it when cross-vendor trace propagation is needed.
No wire cost since proto `optional` fields are zero-cost when absent.
