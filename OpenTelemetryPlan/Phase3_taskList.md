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

- Edit `include/xrpl/proto/xrpl.proto` (or `src/ripple/proto/ripple.proto`, wherever the proto is):
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

- Include `TracingInstrumentation.h` and use `XRPL_TRACE_TX` macro

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
  - Confirm parent-child span relationships are correct in Jaeger

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

**Parallel work**: Tasks 3.1 and 3.4 can start in parallel. Task 3.2 depends on 3.1. Tasks 3.3 and 3.5 depend on 3.2. Task 3.6 depends on 3.3 and 3.5.

**Exit Criteria** (from [06-implementation-phases.md §6.11.3](./06-implementation-phases.md)):

- [ ] Transaction traces span across nodes
- [ ] Trace context in Protocol Buffer messages
- [ ] HashRouter deduplication visible in traces
- [ ] <5% overhead on transaction throughput
