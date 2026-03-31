# Fix Telemetry Validation Checks

## Context

The CI pipeline infrastructure is fully operational (build + deploy + run). However,
the `validate_telemetry.py` validation suite fails 35 checks due to mismatches between
what the validation expects and what the telemetry stack actually produces. These fall
into 4 categories.

CI run: https://github.com/XRPLF/rippled/actions/runs/23026466191

---

## Category 1: StatsD Metrics — 0 Series (25 failures)

**Symptoms:**

```
[FAIL] metric.statsd_gauges.rippled_LedgerMaster_Validated_Ledger_Age: 0 series
[FAIL] metric.statsd_counters.rippled_rpc_requests: 0 series
[FAIL] metric.statsd_histograms.rippled_rpc_time: 0 series
[FAIL] metric.overlay_traffic.rippled_total_Bytes_In: 0 series
[FAIL] metric.phase9_nodestore.rippled_nodestore_reads_total: 0 series
... (25 total)
```

**Root Cause:** Two issues compounding:

1. **StatsD receiver is commented out** in `otel-collector-config.yaml` (lines 39-54).
   The collector config was updated to expect native OTLP metrics from beast::insight
   (comment: "StatsD UDP port removed — beast::insight now uses native OTLP"), but
   the validation harness configures xrpld nodes with `server=statsd`.

2. **Metric name mismatch:** The `expected_metrics.json` expects StatsD-style metric
   names (e.g., `rippled_LedgerMaster_Validated_Ledger_Age`). When using `server=otel`,
   beast::insight emits OTLP metrics which may have different names/structure.

**Fix Options (pick one):**

- **Option A (recommended):** Change the node config in `run-full-validation.sh` from
  `server=statsd` to `server=otel` (line 255), remove the `address=127.0.0.1:8125` line,
  then update `expected_metrics.json` with the actual OTLP metric names. This aligns with
  the collector config's OTLP-first design and avoids re-enabling the StatsD receiver.

- **Option B:** Uncomment the StatsD receiver in `otel-collector-config.yaml`, add
  `statsd` to the metrics pipeline receivers list, and keep node config as `server=statsd`.
  Simpler but goes against the migration to native OTLP.

**Investigation needed for Option A:**

- Run xrpld locally with `server=otel`, query Prometheus, and capture the actual OTLP
  metric names to update `expected_metrics.json`.

**Files to modify:**

- `docker/telemetry/workload/run-full-validation.sh` — change `[insight]` section
- `docker/telemetry/workload/expected_metrics.json` — update metric names for OTLP
- `docker/telemetry/workload/validate_telemetry.py` — may need metric query adjustments

---

## Category 2: Missing Spans — tx.process, tx.receive (2 failures)

**Symptoms:**

```
[FAIL] span.tx.process: tx.process: 0 traces (expected > 0)
[FAIL] span.tx.receive: tx.receive: 0 traces (expected > 0)
```

**Root Cause:** The span names exist in the code:

- `src/xrpld/app/misc/NetworkOPs.cpp:1228` — `XRPL_TRACE_TX("tx.process")`
- `src/xrpld/overlay/detail/PeerImp.cpp:1273` — `XRPL_TRACE_TX("tx.receive")`

Likely causes (investigate in order):

1. **Batch delay:** The 2-second batch delay (`batch_delay_ms=2000`) plus 30s propagation
   wait may not be enough if these spans are created late in the workload.
2. **Code path not triggered:** `tx.process` fires in `NetworkOPs::processTransaction()`.
   The tx_submitter submits via RPC `submit` command which calls this path. But if the
   transactions fail validation before reaching `processTransaction()`, no span is emitted.
3. **Span naming mismatch:** The validation queries Tempo for exact operation name
   `tx.process`. Verify Tempo stores the span with this exact name.

**Investigation:**

- Check the tx_submitter output in CI logs — are transactions actually succeeding?
- Query Tempo API locally for all span names to see what's actually emitted.

**Files to modify:**

- Possibly `docker/telemetry/workload/validate_telemetry.py` — adjust timing/queries
- Possibly `docker/telemetry/workload/run-full-validation.sh` — increase propagation wait

---

## Category 3: Span Hierarchy — rpc.request -> rpc.process (1 failure)

**Symptoms:**

```
[FAIL] span.hierarchy.rpc.request->rpc.process: rpc.process not found in rpc.request traces
```

**Root Cause:** The validator fetches traces containing `rpc.request` from Tempo and
checks if any child span is named `rpc.process`. Both spans are emitted (they pass
individual checks), but the parent-child relationship isn't established.

**Investigation:**

- Check `src/xrpld/rpc/detail/ServerHandler.cpp` — `rpc.request` (line 271) and
  `rpc.process` (line 573) are in the same file. Verify that `rpc.process` is created
  as a child of `rpc.request` (i.e., its parent context is set).
- The issue may be that `rpc.process` creates a new root span instead of linking to the
  `rpc.request` span context.

**Files to modify:**

- Possibly `src/xrpld/rpc/detail/ServerHandler.cpp` — fix span parenting
- OR `docker/telemetry/workload/validate_telemetry.py` — if hierarchy check logic is wrong

---

## Category 4: Dashboard 404s (5 failures)

**Symptoms:**

```
[FAIL] dashboard.rippled-statsd-node-health: HTTP 404
[FAIL] dashboard.rippled-statsd-network: HTTP 404
[FAIL] dashboard.rippled-statsd-rpc: HTTP 404
[FAIL] dashboard.rippled-statsd-overlay-detail: HTTP 404
[FAIL] dashboard.rippled-statsd-ledger-sync: HTTP 404
```

**Root Cause:** Dashboard UIDs were renamed from `rippled-statsd-*` to `rippled-system-*`
but `expected_metrics.json` still references the old names.

**Actual UIDs in `docker/telemetry/grafana/dashboards/`:**
| Expected (in expected_metrics.json) | Actual (in dashboard JSON) |
|-------------------------------------|-------------------------------|
| `rippled-statsd-node-health` | `rippled-system-node-health` |
| `rippled-statsd-network` | `rippled-system-network` |
| `rippled-statsd-rpc` | `rippled-system-rpc` |
| `rippled-statsd-overlay-detail` | `rippled-system-overlay-detail` |
| `rippled-statsd-ledger-sync` | `rippled-system-ledger-sync` |

**Fix:** Update the 5 UIDs in `expected_metrics.json` → `grafana_dashboards.uids[]`.

**Files to modify:**

- `docker/telemetry/workload/expected_metrics.json` — update dashboard UIDs

---

## Execution Order

1. **Category 4 (Dashboard UIDs)** — trivial rename, no investigation needed
2. **Category 1 (StatsD/OTLP metrics)** — requires investigation to choose Option A vs B
   and capture actual metric names
3. **Category 2 (Missing tx spans)** — requires investigation into transaction code paths
4. **Category 3 (Span hierarchy)** — requires investigation into span context propagation

## Branch

All changes go on: `pratik/otel-phase10-workload-validation`
Worktree: `/tmp/otel-phase10-iter`
