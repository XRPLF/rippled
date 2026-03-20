# Phase 9: Internal Metric Instrumentation Gap Fill — Task List

> **Status**: Future Enhancement
>
> **Goal**: Instrument rippled to emit ~50+ metrics that exist in `get_counts`/`server_info`/TxQ/PerfLog but currently lack time-series export via the OTel or beast::insight pipelines.
>
> **Scope**: Hybrid approach — extend `beast::insight` for metrics near existing registrations, use OTel Metrics SDK `ObservableGauge` callbacks for new categories (TxQ, PerfLog, CountedObjects).
>
> **Branch**: `pratik/otel-phase9-metric-gap-fill` (from `pratik/otel-phase8-log-correlation`)
>
> **Depends on**: Phase 7 (native OTel metrics pipeline) and Phase 8 (log-trace correlation)

### Related Plan Documents

| Document                                                             | Relevance                                                      |
| -------------------------------------------------------------------- | -------------------------------------------------------------- |
| [06-implementation-phases.md](./06-implementation-phases.md)         | Phase 9 plan: motivation, architecture, exit criteria (§6.8.2) |
| [09-data-collection-reference.md](./09-data-collection-reference.md) | Current metric inventory + future metrics section              |
| [Phase7_taskList.md](./Phase7_taskList.md)                           | Prerequisite — OTel Metrics SDK and `OTelCollector` class      |
| [Phase8_taskList.md](./Phase8_taskList.md)                           | Prerequisite — log-trace correlation                           |

### Third-Party Consumer Context

These metrics serve multiple external consumer categories identified during research:

| Consumer Category         | Key Metrics They Need                                           |
| ------------------------- | --------------------------------------------------------------- |
| **Exchanges**             | Fee escalation levels, TxQ depth, settlement latency            |
| **Payment Processors**    | Load factors, io_latency, transaction throughput                |
| **Analytics Providers**   | NodeStore I/O, cache hit rates, counted objects                 |
| **Validators/Operators**  | Per-job execution times, PerfLog RPC counters, consensus timing |
| **Academic Researchers**  | Consensus performance time-series, fee market dynamics          |
| **Institutional Custody** | Server health scores, reserve calculations, node availability   |

---

## Task 9.1: NodeStore I/O Metrics

**Objective**: Export node store read/write performance as time-series metrics.

**What to do**:

- In `src/libxrpl/nodestore/Database.cpp`, extend existing `beast::insight` registrations to add:
  - Gauge: `node_reads_total` (cumulative read operations)
  - Gauge: `node_reads_hit` (cache-served reads)
  - Gauge: `node_writes` (cumulative write operations)
  - Gauge: `node_written_bytes` (cumulative bytes written)
  - Gauge: `node_read_bytes` (cumulative bytes read)
  - Gauge: `node_reads_duration_us` (cumulative read time in microseconds)
  - Gauge: `write_load` (current write load score)
  - Gauge: `read_queue` (items in read queue)

- These values are already computed in `Database::getCountsJson()` (line ~236). Wire the same counters to `beast::insight` hooks.

**Key modified files**:

- `src/libxrpl/nodestore/Database.cpp`
- `src/libxrpl/nodestore/Database.h` (add insight members)

**Derived Prometheus metrics**: `rippled_nodestore_reads_total`, `rippled_nodestore_reads_hit`, `rippled_nodestore_write_load`, etc.

**Grafana dashboard**: Add "NodeStore I/O" panel group to _Node Health_ dashboard.

---

## Task 9.2: Cache Hit Rate Metrics

**Objective**: Export SHAMap and ledger cache performance as time-series gauges.

**What to do**:

- Register OTel `ObservableGauge` callbacks (via Phase 7's `OTelCollector`) for:
  - `SLE_hit_rate` — SLE cache hit rate (0.0–1.0)
  - `ledger_hit_rate` — Ledger object cache hit rate
  - `AL_hit_rate` — AcceptedLedger cache hit rate
  - `treenode_cache_size` — SHAMap TreeNode cache size (entries)
  - `treenode_track_size` — Tracked tree nodes
  - `fullbelow_size` — FullBelow cache size

- The callback should read from the same sources as `GetCounts.cpp` handler (line ~43).

- Create a centralized `MetricsRegistry` class that holds all OTel async gauge registrations, polled at 10-second intervals by the `PeriodicMetricReader`.

**Key modified files**:

- New: `src/xrpld/telemetry/MetricsRegistry.h` / `.cpp`
- `src/xrpld/rpc/handlers/GetCounts.cpp` (extract shared access methods)
- `src/xrpld/app/main/Application.cpp` (register MetricsRegistry at startup)

**Derived Prometheus metrics**: `rippled_cache_SLE_hit_rate`, `rippled_cache_ledger_hit_rate`, `rippled_cache_treenode_size`, etc.

---

## Task 9.3: Transaction Queue (TxQ) Metrics

**Objective**: Export TxQ depth, capacity, and fee escalation levels as time-series.

**What to do**:

- Register OTel `ObservableGauge` callbacks for TxQ state (from `TxQ.h` line ~143):
  - `txq_count` — Current transactions in queue
  - `txq_max_size` — Maximum queue capacity
  - `txq_in_ledger` — Transactions in current open ledger
  - `txq_per_ledger` — Expected transactions per ledger
  - `txq_reference_fee_level` — Reference fee level
  - `txq_min_processing_fee_level` — Minimum fee to get processed
  - `txq_med_fee_level` — Median fee level in queue
  - `txq_open_ledger_fee_level` — Open ledger fee escalation level

- Add to the `MetricsRegistry` (Task 9.2).

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.cpp` (add TxQ callbacks)
- `src/xrpld/app/tx/detail/TxQ.h` (expose metrics accessor if needed)

**Derived Prometheus metrics**: `rippled_txq_count`, `rippled_txq_max_size`, `rippled_txq_open_ledger_fee_level`, etc.

**Grafana dashboard**: New _Fee Market & TxQ_ dashboard (`rippled-fee-market`).

---

## Task 9.4: PerfLog Per-RPC Method Metrics

**Objective**: Export per-RPC-method call counts and latency as OTel metrics.

**What to do**:

- Register OTel instruments for PerfLog RPC counters (from `PerfLogImp.cpp` line ~63):
  - Counter: `rpc_method_started_total{method="<name>"}` — calls started
  - Counter: `rpc_method_finished_total{method="<name>"}` — calls completed
  - Counter: `rpc_method_errored_total{method="<name>"}` — calls errored
  - Histogram: `rpc_method_duration_us{method="<name>"}` — execution time distribution

- Use OTel `Counter<int64_t>` and `Histogram<double>` instruments with `method` attribute label.

- Hook into the existing PerfLog callback mechanism rather than adding new instrumentation points.

**Key modified files**:

- `src/xrpld/perflog/detail/PerfLogImp.cpp` (add OTel instrument updates alongside existing JSON counters)
- `src/xrpld/telemetry/MetricsRegistry.cpp` (register instruments)

**Derived Prometheus metrics**: `rippled_rpc_method_started_total{method="server_info"}`, `rippled_rpc_method_duration_us_bucket{method="ledger"}`, etc.

**Grafana dashboard**: Add "Per-Method RPC Breakdown" panel group to _RPC Performance_ dashboard.

---

## Task 9.5: PerfLog Per-Job-Type Metrics

**Objective**: Export per-job-type queue and execution metrics.

**What to do**:

- Register OTel instruments for PerfLog job counters:
  - Counter: `job_queued_total{job_type="<name>"}` — jobs queued
  - Counter: `job_started_total{job_type="<name>"}` — jobs started
  - Counter: `job_finished_total{job_type="<name>"}` — jobs completed
  - Histogram: `job_queued_duration_us{job_type="<name>"}` — time spent waiting in queue
  - Histogram: `job_running_duration_us{job_type="<name>"}` — execution time distribution

- Hook into PerfLog's existing job tracking alongside Task 9.4.

**Key modified files**:

- `src/xrpld/perflog/detail/PerfLogImp.cpp`
- `src/xrpld/telemetry/MetricsRegistry.cpp`

**Derived Prometheus metrics**: `rippled_job_queued_total{job_type="ledgerData"}`, `rippled_job_running_duration_us_bucket{job_type="transaction"}`, etc.

**Grafana dashboard**: New _Job Queue Analysis_ dashboard (`rippled-job-queue`).

---

## Task 9.6: Counted Object Instance Metrics

**Objective**: Export live instance counts for key internal object types.

**What to do**:

- Register OTel `ObservableGauge` callbacks for `CountedObject<T>` instance counts:
  - `object_count{type="Transaction"}` — live Transaction objects
  - `object_count{type="Ledger"}` — live Ledger objects
  - `object_count{type="NodeObject"}` — live NodeObject instances
  - `object_count{type="STTx"}` — serialized transaction objects
  - `object_count{type="STLedgerEntry"}` — serialized ledger entries
  - `object_count{type="InboundLedger"}` — ledgers being fetched
  - `object_count{type="Pathfinder"}` — active pathfinding computations
  - `object_count{type="PathRequest"}` — active path requests
  - `object_count{type="HashRouterEntry"}` — hash router entries

- The `CountedObject` template already tracks these via atomic counters. The callback just reads the current counts.

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.cpp` (add counted object callbacks)
- `include/xrpl/basics/CountedObject.h` (may need static accessor for iteration)

**Derived Prometheus metrics**: `rippled_object_count{type="Transaction"}`, `rippled_object_count{type="NodeObject"}`, etc.

**Grafana dashboard**: Add "Object Instance Counts" panel to _Node Health_ dashboard.

---

## Task 9.7: Fee Escalation & Load Factor Metrics

**Objective**: Export the full load factor breakdown as time-series.

**What to do**:

- Register OTel `ObservableGauge` callbacks for load factors (from `NetworkOPs.cpp` line ~2694):
  - `load_factor` — combined transaction cost multiplier
  - `load_factor_server` — server + cluster + network contribution
  - `load_factor_local` — local server load only
  - `load_factor_net` — network-wide load estimate
  - `load_factor_cluster` — cluster peer load
  - `load_factor_fee_escalation` — open ledger fee escalation
  - `load_factor_fee_queue` — queue entry fee level

- These overlap with some existing StatsD metrics but provide finer granularity (individual factor breakdown vs. combined value).

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.cpp`
- `src/xrpld/app/misc/NetworkOPs.cpp` (expose load factor accessors if needed)

**Derived Prometheus metrics**: `rippled_load_factor`, `rippled_load_factor_fee_escalation`, etc.

**Grafana dashboard**: Add "Load Factor Breakdown" panel to _Fee Market & TxQ_ dashboard.

---

## Task 9.8: New Grafana Dashboards

**Objective**: Create Grafana dashboards for the new metric categories.

**What to do**:

- Create 2 new dashboards:
  1. **Fee Market & TxQ** (`rippled-fee-market`) — TxQ depth/capacity, fee levels, load factor breakdown, fee escalation timeline
  2. **Job Queue Analysis** (`rippled-job-queue`) — Per-job-type rates, queue wait times, execution times, job queue depth

- Update 2 existing dashboards:
  1. **Node Health** (`rippled-statsd-node-health`) — Add NodeStore I/O panels, cache hit rate panels, object instance counts
  2. **RPC Performance** (`rippled-rpc-perf`) — Add per-method RPC breakdown panels

**Key modified files**:

- New: `docker/telemetry/grafana/dashboards/rippled-fee-market.json`
- New: `docker/telemetry/grafana/dashboards/rippled-job-queue.json`
- `docker/telemetry/grafana/dashboards/rippled-statsd-node-health.json`
- `docker/telemetry/grafana/dashboards/rippled-rpc-perf.json`

---

## Task 9.9: Update Documentation

**Objective**: Update telemetry reference docs with all new metrics.

**What to do**:

- Update `OpenTelemetryPlan/09-data-collection-reference.md`:
  - Add new section for OTel SDK-exported metrics (NodeStore, cache, TxQ, PerfLog, CountedObjects, load factors)
  - Update Grafana dashboard reference table (add 2 new dashboards)
  - Add Prometheus query examples for new metrics

- Update `docs/telemetry-runbook.md`:
  - Add alerting rules for new metrics (NodeStore write_load, TxQ capacity, cache hit rate degradation)
  - Add troubleshooting entries for new metric categories

**Key modified files**:

- `OpenTelemetryPlan/09-data-collection-reference.md`
- `docs/telemetry-runbook.md`

---

## Task 9.10: Integration Tests

**Objective**: Verify all new metrics appear in Prometheus after a test workload.

**What to do**:

- Extend the existing telemetry integration test:
  - Start rippled with `[telemetry] enabled=1` and `[insight] server=otel`
  - Submit a batch of RPC calls and transactions
  - Query Prometheus for each new metric family
  - Assert non-zero values for: NodeStore reads, cache hit rates, TxQ count, PerfLog RPC counters, object counts, load factors

- Add unit tests for the `MetricsRegistry` class:
  - Verify callback registration and deregistration
  - Verify metric values match `get_counts` JSON output
  - Verify graceful behavior when telemetry is disabled

**Key modified files**:

- `src/test/telemetry/MetricsRegistry_test.cpp` (new)
- Existing integration test script (extend assertions)

---

## Exit Criteria

- [ ] All ~50 new metrics visible in Prometheus via OTLP pipeline
- [ ] `MetricsRegistry` class registers/deregisters cleanly with OTel SDK
- [ ] Async gauge callbacks execute at 10s intervals without performance impact
- [ ] 2 new Grafana dashboards operational (Fee Market, Job Queue)
- [ ] 2 existing dashboards updated with new panel groups
- [ ] Integration test validates all new metric families are non-zero
- [ ] No performance regression (< 0.5% CPU overhead from new callbacks)
- [ ] Documentation updated with full new metric inventory
