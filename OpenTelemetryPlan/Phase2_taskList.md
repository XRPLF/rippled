# Phase 2: RPC Tracing Completion Task List

> **Goal**: Complete full RPC tracing coverage with W3C Trace Context propagation, unit tests, and performance validation. Build on the POC foundation to achieve production-quality RPC observability.
>
> **Scope**: W3C header extraction, TraceContext propagation utilities, unit tests for core telemetry, integration tests for RPC tracing, and performance benchmarks.
>
> **Branch**: `pratik/otel-phase2-rpc-tracing` (from `pratik/OpenTelemetry_and_DistributedTracing_planning`)

### Related Plan Documents

| Document                                                     | Relevance                                                     |
| ------------------------------------------------------------ | ------------------------------------------------------------- |
| [04-code-samples.md](./04-code-samples.md)                   | TraceContextPropagator (§4.4.2), RPC instrumentation (§4.5.3) |
| [02-design-decisions.md](./02-design-decisions.md)           | W3C Trace Context (§2.5), span attributes (§2.4.2)            |
| [06-implementation-phases.md](./06-implementation-phases.md) | Phase 2 tasks (§6.3), definition of done (§6.11.2)            |

---

## Task 2.1: Implement W3C Trace Context HTTP Header Extraction

**Objective**: Extract `traceparent` and `tracestate` headers from incoming HTTP RPC requests so external callers can propagate their trace context into rippled.

**What to do**:

- Create `include/xrpl/telemetry/TraceContextPropagator.h`:
  - `extractFromHeaders(headerGetter)` - extract W3C traceparent/tracestate from HTTP headers
  - `injectToHeaders(ctx, headerSetter)` - inject trace context into response headers
  - Use OTel's `TextMapPropagator` with `W3CTraceContextPropagator` for standards compliance
  - Only compiled when `XRPL_ENABLE_TELEMETRY` is defined

- Create `src/libxrpl/telemetry/TraceContextPropagator.cpp`:
  - Implement a simple `TextMapCarrier` adapter for HTTP headers
  - Use `opentelemetry::context::propagation::GlobalTextMapPropagator` for extraction/injection
  - Register the W3C propagator in `TelemetryImpl::start()`

- Modify `src/xrpld/rpc/detail/ServerHandler.cpp`:
  - In the HTTP request handler, extract parent context from headers before creating span
  - Pass extracted context to `startSpan()` as parent
  - Inject trace context into response headers

**Key new files**:

- `include/xrpl/telemetry/TraceContextPropagator.h`
- `src/libxrpl/telemetry/TraceContextPropagator.cpp`

**Key modified files**:

- `src/xrpld/rpc/detail/ServerHandler.cpp`
- `src/libxrpl/telemetry/Telemetry.cpp` (register W3C propagator)

**Reference**:

- [04-code-samples.md §4.4.2](./04-code-samples.md) — TraceContextPropagator with extractFromHeaders/injectToHeaders
- [02-design-decisions.md §2.5](./02-design-decisions.md) — W3C Trace Context propagation design

---

## Task 2.2: Add XRPL_TRACE_PEER Macro

**Objective**: Add the missing peer-tracing macro for future Phase 3 use and ensure macro completeness.

**What to do**:

- Edit `src/xrpld/telemetry/TracingInstrumentation.h`:
  - Add `XRPL_TRACE_PEER(_tel_obj_, _span_name_)` macro that checks `shouldTracePeer()`
  - Add `XRPL_TRACE_LEDGER(_tel_obj_, _span_name_)` macro (for future ledger tracing)
  - Ensure disabled variants expand to `((void)0)`

**Key modified file**:

- `src/xrpld/telemetry/TracingInstrumentation.h`

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

**Objective**: Add unit tests for the core telemetry abstractions to validate correctness and catch regressions.

**What to do**:

- Create `src/test/telemetry/Telemetry_test.cpp`:
  - Test NullTelemetry: verify all methods return expected no-op values
  - Test Setup defaults: verify all Setup fields have correct defaults
  - Test setup_Telemetry config parser: verify parsing of [telemetry] section
  - Test enabled/disabled factory paths
  - Test shouldTrace\* methods respect config flags

- Create `src/test/telemetry/SpanGuard_test.cpp`:
  - Test SpanGuard RAII lifecycle (span ends on destruction)
  - Test move constructor works correctly
  - Test setAttribute, setOk, setStatus, addEvent, recordException
  - Test context() returns valid context

- Add test files to CMake build

**Key new files**:

- `src/test/telemetry/Telemetry_test.cpp`
- `src/test/telemetry/SpanGuard_test.cpp`

**Reference**:

- [06-implementation-phases.md §6.11.1](./06-implementation-phases.md) — Phase 1 exit criteria (unit tests passing)

---

## Task 2.5: Enhance RPC Span Attributes

**Objective**: Add additional attributes to RPC spans per the semantic conventions defined in the plan.

**What to do**:

- Edit `src/xrpld/rpc/detail/ServerHandler.cpp`:
  - Add `http.method` attribute for HTTP requests
  - Add `http.status_code` attribute for responses
  - Add `net.peer.ip` attribute for client IP (if available)

- Edit `src/xrpld/rpc/detail/RPCHandler.cpp`:
  - Add `xrpl.rpc.duration_ms` attribute on completion
  - Add error message attribute on failure: `xrpl.rpc.error_message`

**Key modified files**:

- `src/xrpld/rpc/detail/ServerHandler.cpp`
- `src/xrpld/rpc/detail/RPCHandler.cpp`

**Reference**:

- [02-design-decisions.md §2.4.2](./02-design-decisions.md) — RPC attribute schema

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

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md) — adds node-level health context inspired by the community [xrpl-validator-dashboard](https://github.com/realgrapedrop/xrpl-validator-dashboard).
>
> **Downstream**: Phase 7 (MetricsRegistry uses these attributes for alerting context), Phase 10 (validation checks for these attributes).

**Objective**: Add node-level health state to every `rpc.command.*` span so operators can correlate RPC behavior with node state in Tempo.

**What to do**:

- Edit `src/xrpld/rpc/detail/RPCHandler.cpp`:
  - In the `rpc.command.*` span creation block (after existing `setAttribute` calls for `xrpl.rpc.command`, `xrpl.rpc.version`, etc.):
    - Add `xrpl.node.amendment_blocked` (bool) — from `context.app.getOPs().isAmendmentBlocked()`
    - Add `xrpl.node.server_state` (string) — from `context.app.getOPs().strOperatingMode()`

**New span attributes**:

| Attribute                     | Type   | Source                                      | Example  |
| ----------------------------- | ------ | ------------------------------------------- | -------- |
| `xrpl.node.amendment_blocked` | bool   | `context.app.getOPs().isAmendmentBlocked()` | `true`   |
| `xrpl.node.server_state`      | string | `context.app.getOPs().strOperatingMode()`   | `"full"` |

**Rationale**: When a node is amendment-blocked or in a degraded state, every RPC response is suspect. Tagging spans with this state enables Tempo TraceQL queries like:

```
{name=~"rpc.command.*"} | xrpl.node.amendment_blocked = true
```

This surfaces all RPCs served during a blocked period — critical for post-incident analysis.

**Key modified files**:

- `src/xrpld/rpc/detail/RPCHandler.cpp`

**Exit Criteria**:

- [ ] `rpc.command.server_info` spans carry `xrpl.node.amendment_blocked` and `xrpl.node.server_state` attributes
- [ ] No measurable latency impact (attribute values are cached atomics, not computed per-call)
- [ ] Attributes appear in Tempo trace detail view

---

## Summary

| Task | Description                                 | New Files | Modified Files | Depends On |
| ---- | ------------------------------------------- | --------- | -------------- | ---------- |
| 2.1  | W3C Trace Context header extraction         | 2         | 2              | POC        |
| 2.2  | Add XRPL_TRACE_PEER/LEDGER macros           | 0         | 1              | POC        |
| 2.3  | Add shouldTraceLedger() interface method    | 0         | 3              | POC        |
| 2.4  | Unit tests for core telemetry               | 2         | 1              | POC        |
| 2.5  | Enhanced RPC span attributes                | 0         | 2              | POC        |
| 2.6  | Build verification and performance baseline | 0         | 0              | 2.1-2.5    |
| 2.8  | RPC span attribute enrichment (node health) | 0         | 1              | 2.5        |

**Parallel work**: Tasks 2.1, 2.2, 2.3 can run in parallel. Task 2.4 depends on 2.3. Task 2.5 can run in parallel with 2.4. Task 2.6 depends on all others. Task 2.8 depends on 2.5 (existing span creation must be in place).

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
