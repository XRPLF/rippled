# Phase 2: RPC Tracing Completion Task List

> **Goal**: Complete RPC tracing coverage with unit tests, Grafana search filters, PathFind instrumentation, and config hardening. Build on the Phase 1c SpanGuard factory foundation to achieve production-quality RPC observability.
>
> **Scope**: Unit tests for core telemetry, Grafana Tempo search filters, PathFind RPC tracing, config validation (`std::clamp`).
>
> **Branch**: `pratik/otel-phase2-rpc-tracing` (from `pratik/otel-phase1c-rpc-integration`)

### Related Plan Documents

| Document                                                         | Relevance                                                                                                                        |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| [03-implementation-strategy.md](./03-implementation-strategy.md) | Code structure and instrumentation patterns (replaces the deleted `04-code-samples.md` §4.4.2 / §4.5.3, removed by `d6450631bf`) |
| [02-design-decisions.md](./02-design-decisions.md)               | W3C Trace Context (§2.5), span attributes (§2.4.2)                                                                               |
| [06-implementation-phases.md](./06-implementation-phases.md)     | Phase 2 tasks (§6.3), definition of done (§6.11.2)                                                                               |

---

## Task 2.1: W3C Trace Context HTTP Header Extraction

**Status**: DEFERRED → Phase 3

**Reason**: W3C context propagation (`traceparent`/`tracestate` headers) requires a consumer — in Phase 2, RPC spans are entirely local to the node. Phase 3 introduces cross-node transaction tracing via protobuf context propagation, which is the first use case for extracted trace context. Implementing it here without a consumer would be dead code.

**Implemented in**: `pratik/otel-phase3-tx-tracing` — `TraceContextPropagator.h/.cpp`

---

## Task 2.2: Per-Category Span Creation

**Status**: COMPLETE (superseded by Phase 1c design)

**Original plan**: Add `XRPL_TRACE_PEER` and `XRPL_TRACE_LEDGER` macros.

**Actual implementation**: Phase 1c replaced all tracing macros with the `SpanGuard::span(TraceCategory, prefix, name)` factory pattern. The `TraceCategory` enum (`Rpc`, `Transactions`, `Consensus`, `Peer`, `Ledger`) serves the same conditional-creation purpose without macros. No separate task needed — the factory already supports all categories.

---

## Task 2.3: Add shouldTraceLedger() to Telemetry Interface

**Objective**: The `Setup` struct has a `traceLedger` field but there's no corresponding virtual method. Add it for interface completeness.

**What to do**:

- Edit `include/xrpl/telemetry/Telemetry.h`:
  - Add `virtual bool shouldTraceLedger() const = 0;`

- Update all implementations:
  - `src/libxrpl/telemetry/Telemetry.cpp` (TelemetryImpl, NullTelemetryOtel)
  - `src/libxrpl/telemetry/NullTelemetry.cpp` (NullTelemetry)

**Key modified files**:

- `include/xrpl/telemetry/Telemetry.h`
- `src/libxrpl/telemetry/Telemetry.cpp`
- `src/libxrpl/telemetry/NullTelemetry.cpp`

---

## Task 2.4: Unit Tests for Core Telemetry Infrastructure

**Status**: COMPLETE

**Objective**: Add unit tests for the core telemetry abstractions to validate correctness and catch regressions.

**Implemented**:

- `src/tests/libxrpl/telemetry/TelemetryConfig.cpp`:
  - Test Setup defaults (all fields have correct initial values)
  - Test `makeTelemetrySetup` config parser (empty section, full section, edge cases)
  - Test `samplingRatio` clamping (values outside 0.0-1.0)

- `src/tests/libxrpl/telemetry/SpanGuardFactory.cpp`:
  - Test null guard methods are safe (setAttribute, setOk, setError, addEvent on null)
  - Test category span returns null when telemetry disabled
  - Test child/linked span null when no parent context
  - Test move construction transfers ownership
  - Test recordException safe on null guard
  - Test discard() safe on null guard

- `src/tests/libxrpl/telemetry/main.cpp` — GTest runner
- `src/tests/libxrpl/CMakeLists.txt` — test target with optional OTel linking

---

## Task 2.5: Enhance RPC Span Attributes

**Status**: DEFERRED (low priority)

**Reason**: The high-value attributes (`command`, `version`, `role`, `status`) are already set by Phase 1c. The remaining HTTP transport-level attributes (`http.method`, `net.peer.ip`, `http.status_code`) provide limited additional insight since:

- `http.method` is always POST for JSON-RPC
- `net.peer.ip` is debug-level info available in logs
- `duration_ms` is redundant with span duration (OTel captures start/end time natively)

These can be added later if dashboard queries specifically need them. The node health attributes (Task 2.8) provide far more operational value and were prioritized instead.

---

## Task 2.6: Build Verification and Performance Baseline

**Objective**: Verify the build succeeds with and without telemetry, and establish a performance baseline.

**What to do**:

1. Build with `telemetry=ON` and verify no compilation errors
2. Build with `telemetry=OFF` and verify no regressions
3. Run existing unit tests to verify no breakage
4. Document any build issues in lessons.md

**Verification Checklist**:

- [ ] `conan install . --build=missing -o telemetry=True` succeeds
- [ ] `cmake --preset default -Dtelemetry=ON` configures correctly
- [ ] Build succeeds with telemetry ON
- [ ] Build succeeds with telemetry OFF
- [ ] Existing tests pass with telemetry ON
- [ ] Existing tests pass with telemetry OFF

---

## Task 2.8: RPC Span Attribute Enrichment — Node Health Context

**Status**: DROPPED.

Node health (`amendment_blocked`, `server_state`) is not part of the telemetry surface. Operators consume the same data via the existing `server_info` / `server_state` RPC commands, so duplicating it on traces adds storage and cardinality cost without new value. The OTel C++ SDK 1.18.0 also does not support runtime updates to the resource, ruling out resource-level emission of these dynamic-by-nature flags.

---

## Task 2.9: PathFind RPC Instrumentation

**Status**: COMPLETE

**Objective**: Trace the path_find and ripple_path_find RPC handlers to capture request latency and computation cost.

**Spans added**:

- `pathfind.request` — wraps `doPathFind()` and `doRipplePathFind()` RPC handlers
- `pathfind.compute` — wraps `PathRequest::doUpdate()` (`pathfind_fast` attr)
- `pathfind.update_all` — wraps `PathRequestManager::updateAll()` on ledger close (`pathfind_ledger_index`, `pathfind_num_requests` attrs; emitted only when active subscriptions exist)
- `pathfind.discover` — wraps the entire per-source-asset loop in `PathRequest::findPaths()` (`pathfind_search_level`, `pathfind_num_paths` attrs). One span per RPC call instead of N (one per source asset). Trade-off: per-asset breakdown is lost; storage and cardinality bounded.

**Attribute namespacing**: All pathfind attributes use the `pathfind_*` underscore form per the Phase 1c naming-spec rule 5.

**New file**: `src/xrpld/rpc/detail/PathFindSpanNames.h`

**Modified files**:

- `src/xrpld/rpc/handlers/orderbook/PathFind.cpp`
- `src/xrpld/rpc/handlers/orderbook/RipplePathFind.cpp`
- `src/xrpld/rpc/detail/PathRequest.cpp`
- `src/xrpld/rpc/detail/PathRequestManager.cpp`
- `src/xrpld/rpc/detail/Pathfinder.cpp`

---

## Task 2.10: RPC and PathFind Span Attribute Gap Fill

**Status**: COMPLETE

**Objective**: Wire up workflow-identifying attributes that enable filtering and grouping traces by request characteristics without drilling into child spans.

**Attributes added**:

| Span                | Attribute                    | Type   | Source                            |
| ------------------- | ---------------------------- | ------ | --------------------------------- |
| `rpc.http_request`  | `request_payload_size`       | int64  | `request.body().size()`           |
| `rpc.process`       | `is_batch`                   | bool   | `method == "batch"` check         |
| `rpc.process`       | `batch_size`                 | int64  | `params.size()` (only when batch) |
| `rpc.ws_message`    | `command`                    | string | `jv[command]` or `jv[method]`     |
| `rpc.command.*`     | `load_type`                  | string | `context.loadType.label()`        |
| `pathfind.compute`  | `pathfind_dest_currency`     | string | `to_string(saDstAmount_.asset())` |
| `pathfind.discover` | `pathfind_num_source_assets` | int64  | `sourceAssets.size()`             |

_Note: `pathfind_dest_amount` was removed — the destination amount is a financial value excluded by the privacy policy (design §2.4.4)._

**New attr keys**: `RpcSpanNames.h` (`isBatch`, `batchSize`, `loadType`), `PathFindSpanNames.h` (`destCurrency`, `numSourceAssets`).

**Modified files**:

- `src/xrpld/rpc/detail/RpcSpanNames.h`
- `src/xrpld/rpc/detail/PathFindSpanNames.h`
- `src/xrpld/rpc/detail/ServerHandler.cpp`
- `src/xrpld/rpc/detail/RPCHandler.cpp`
- `src/xrpld/rpc/detail/PathRequest.cpp`

---

## Summary

| Task | Description                                 | Status              | Notes                                                     |
| ---- | ------------------------------------------- | ------------------- | --------------------------------------------------------- |
| 2.1  | W3C Trace Context header extraction         | Deferred → Phase 3  | No consumer in Phase 2; needs cross-node tracing          |
| 2.2  | Per-category span creation                  | Complete (Phase 1c) | Superseded by TraceCategory enum + SpanGuard              |
| 2.3  | Add shouldTraceLedger() interface method    | Complete (Phase 1c) | Delivered in Phase 1c base branch                         |
| 2.4  | Unit tests for core telemetry               | Complete            | TelemetryConfig + SpanGuardFactory tests                  |
| 2.5  | Enhanced RPC span attributes (HTTP-level)   | Deferred            | Low value; span duration covers timing natively           |
| 2.6  | Build verification and performance baseline | Complete            | Verified in CI on Phase 1c                                |
| 2.7  | Grafana Tempo search filters                | Complete            | rpc-command, rpc-status, rpc-role filters                 |
| 2.8  | RPC span attribute enrichment (node health) | Dropped             | Available via `server_info`/`server_state` RPC            |
| 2.9  | PathFind RPC instrumentation                | Complete            | request, compute, update_all, discover                    |
| 2.10 | RPC/PathFind span attribute gap fill        | Complete            | Batch detection, payload size, load cost, pathfind params |

**Delivered in this branch**: Tasks 2.4, 2.7, 2.9, 2.10.
**Deferred with rationale**: Tasks 2.1 (→Phase 3), 2.5 (low priority).
**Dropped**: Task 2.8 (node health not duplicated on traces).
**Superseded**: Task 2.2 (Phase 1c SpanGuard factory covers this).

---

## Known Issues / Future Work

### Thread safety of TelemetryImpl::stop() vs startSpan()

`TelemetryImpl::stop()` resets `sdkProvider_` (a `std::shared_ptr`) without
synchronization. `getTracer()` reads the same member from RPC handler threads.
This is a data race if any thread calls `startSpan()` concurrently with `stop()`.

**Current mitigation**: `Application::stop()` shuts down `serverHandler_`,
`overlay_`, and `jobQueue_` before calling `telemetry_->stop()`, so no callers
remain. See comments in `Telemetry.cpp:stop()` and `Application.cpp`.

**TODO**: Add an `std::atomic<bool> stopped_` flag checked in `getTracer()` to
make this robust against future shutdown order changes.

### Macro incompatibility: XRPL_TRACE_SPAN vs XRPL_TRACE_SET_ATTR

`XRPL_TRACE_SPAN` and `XRPL_TRACE_SPAN_KIND` declare `_xrpl_guard_` as a bare
`SpanGuard`, but `XRPL_TRACE_SET_ATTR` and `XRPL_TRACE_EXCEPTION` call
`_xrpl_guard_.has_value()` which requires `std::optional<SpanGuard>`. Using
`XRPL_TRACE_SPAN` followed by `XRPL_TRACE_SET_ATTR` in the same scope would
fail to compile.

**Current mitigation**: No call site currently uses `XRPL_TRACE_SPAN` — all
production code uses the conditional macros (`XRPL_TRACE_RPC`, `XRPL_TRACE_TX`,
etc.) which correctly wrap the guard in `std::optional`.

**TODO**: Either make `XRPL_TRACE_SPAN`/`XRPL_TRACE_SPAN_KIND` also wrap in
`std::optional`, or document that `XRPL_TRACE_SET_ATTR` is only compatible with
the conditional macros.
