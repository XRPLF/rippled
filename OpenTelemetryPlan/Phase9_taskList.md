# Phase 9: Internal Metric Instrumentation Gap Fill — Task List

> **Status**: Future Enhancement
>
> **Goal**: Instrument xrpld to emit ~50+ metrics that exist in `get_counts`/`server_info`/TxQ/PerfLog but currently lack time-series export via the OTel or beast::insight pipelines.
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

**Derived Prometheus metrics**: `xrpld_nodestore_reads_total`, `xrpld_nodestore_reads_hit`, `xrpld_nodestore_write_load`, etc.

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

**Derived Prometheus metrics**: `xrpld_cache_SLE_hit_rate`, `xrpld_cache_ledger_hit_rate`, `xrpld_cache_treenode_size`, etc.

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

**Derived Prometheus metrics**: `xrpld_txq_count`, `xrpld_txq_max_size`, `xrpld_txq_open_ledger_fee_level`, etc.

**Grafana dashboard**: New _Fee Market & TxQ_ dashboard (`xrpld-fee-market`).

---

## Task 9.4: PerfLog Per-RPC Method Metrics

**Objective**: Export per-RPC-method call counts and latency as OTel metrics.

**What to do**:

- Register OTel instruments for PerfLog RPC counters (from `PerfLogImp.cpp` line ~63):
  - Counter: `xrpld_rpc_method_started_total{method="<name>"}` — calls started
  - Counter: `xrpld_rpc_method_finished_total{method="<name>"}` — calls completed
  - Counter: `xrpld_rpc_method_errored_total{method="<name>"}` — calls errored
  - Histogram: `xrpld_rpc_method_duration_us{method="<name>"}` — execution time distribution

- Use OTel `Counter<int64_t>` and `Histogram<double>` instruments with `method` attribute label.

- Hook into the existing PerfLog callback mechanism rather than adding new instrumentation points.

**Key modified files**:

- `src/xrpld/perflog/detail/PerfLogImp.cpp` (add OTel instrument updates alongside existing JSON counters)
- `src/xrpld/telemetry/MetricsRegistry.cpp` (register instruments)

**Derived Prometheus metrics**: `xrpld_rpc_method_started_total{method="server_info"}`, `xrpld_rpc_method_duration_us_bucket{method="ledger"}`, etc.

**Grafana dashboard**: Add "Per-Method RPC Breakdown" panel group to _RPC Performance_ dashboard.

---

## Task 9.5: PerfLog Per-Job-Type Metrics

**Objective**: Export per-job-type queue and execution metrics.

**What to do**:

- Register OTel instruments for PerfLog job counters:
  - Counter: `xrpld_job_queued_total{job_type="<name>"}` — jobs queued
  - Counter: `xrpld_job_started_total{job_type="<name>"}` — jobs started
  - Counter: `xrpld_job_finished_total{job_type="<name>"}` — jobs completed
  - Histogram: `xrpld_job_queued_duration_us{job_type="<name>"}` — time spent waiting in queue
  - Histogram: `xrpld_job_running_duration_us{job_type="<name>"}` — execution time distribution

- Hook into PerfLog's existing job tracking alongside Task 9.4.

**Key modified files**:

- `src/xrpld/perflog/detail/PerfLogImp.cpp`
- `src/xrpld/telemetry/MetricsRegistry.cpp`

**Derived Prometheus metrics**: `xrpld_job_queued_total{job_type="ledgerData"}`, `xrpld_job_running_duration_us_bucket{job_type="transaction"}`, etc.

**Grafana dashboard**: New _Job Queue Analysis_ dashboard (`xrpld-job-queue`).

---

## Task 9.6: Counted Object Instance Metrics

**Objective**: Export live instance counts for key internal object types.

**What to do**:

- Register OTel `ObservableGauge` callbacks for `CountedObject<T>` instance counts:
  - `xrpld_object_count{type="Transaction"}` — live Transaction objects
  - `xrpld_object_count{type="Ledger"}` — live Ledger objects
  - `xrpld_object_count{type="NodeObject"}` — live NodeObject instances
  - `xrpld_object_count{type="STTx"}` — serialized transaction objects
  - `xrpld_object_count{type="STLedgerEntry"}` — serialized ledger entries
  - `xrpld_object_count{type="InboundLedger"}` — ledgers being fetched
  - `xrpld_object_count{type="Pathfinder"}` — active pathfinding computations
  - `xrpld_object_count{type="PathRequest"}` — active path requests
  - `xrpld_object_count{type="HashRouterEntry"}` — hash router entries

- The `CountedObject` template already tracks these via atomic counters. The callback just reads the current counts.

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.cpp` (add counted object callbacks)
- `include/xrpl/basics/CountedObject.h` (may need static accessor for iteration)

**Derived Prometheus metrics**: `xrpld_object_count{type="Transaction"}`, `xrpld_object_count{type="NodeObject"}`, etc.

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

**Derived Prometheus metrics**: `xrpld_load_factor`, `xrpld_load_factor_fee_escalation`, etc.

**Grafana dashboard**: Add "Load Factor Breakdown" panel to _Fee Market & TxQ_ dashboard.

---

## Task 9.7a: push_metrics.py Parity — Missing Observable Gauges

**Objective**: Fill the remaining metric gaps between the external `push_metrics.py` script (in `ripplex-ansible`) and the internal OTel `MetricsRegistry` observable gauges. After this task, all metrics collected by `push_metrics.py` that CAN be collected internally are covered.

**What was done**:

- Extended existing `cacheHitRateGauge_` callback with `AL_size` (AcceptedLedger cache size)
- Extended existing `nodeStoreGauge_` callback with 4 new metrics from `getCountsJson()`:
  - `node_reads_duration_us` (JSON string — uses `std::stoll(asString())`)
  - `read_request_bundle` (native JSON int)
  - `read_threads_running` (native JSON int)
  - `read_threads_total` (native JSON int)
- Added new `xrpld_server_info` Int64ObservableGauge with 8 metrics:
  - `server_state` — operating mode as int (0=DISCONNECTED .. 4=FULL)
  - `uptime` — seconds since server start
  - `peers` — total peer count
  - `validated_ledger_seq` — validated ledger sequence (atomic read)
  - `ledger_current_index` — current open ledger sequence
  - `peer_disconnects_resources` — cumulative resource-related disconnects
  - `last_close_proposers` — from `getConsensusInfo()["previous_proposers"]`
  - `last_close_converge_time_ms` — from `getConsensusInfo()["previous_mseconds"]`
- Added new `xrpld_build_info` Int64ObservableGauge (info-style, value=1 with `version` label)
- Added new `xrpld_complete_ledgers` Int64ObservableGauge parsing comma-separated ranges into `{bound, index}` pairs
- Added new `xrpld_db_metrics` Int64ObservableGauge with 4 metrics:
  - `db_kb_total`, `db_kb_ledger`, `db_kb_transaction` (SQLite stat queries)
  - `historical_perminute` (historical ledger fetch rate)

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.h` (4 new gauge members, updated ASCII diagram)
- `src/xrpld/telemetry/MetricsRegistry.cpp` (4 new callback registrations, 2 callback extensions)

**Not implementable inside xrpld**:

- `connection_count_51233/51234` — OS-level port connection counts from external shell script (`get_connection.sh`)

**Derived Prometheus metrics**: `xrpld_server_info{metric="server_state"}`, `xrpld_build_info{version="2.4.0"}`, `xrpld_complete_ledgers{bound="start",index="0"}`, `xrpld_db_metrics{metric="db_kb_total"}`, etc.

**Grafana dashboard**: New panels added to _Node Health_ dashboard (`system-node-health.json`).

---

## Task 9.8: New Grafana Dashboards

**Objective**: Create Grafana dashboards for the new metric categories.

**What to do**:

- Create 2 new dashboards:
  1. **Fee Market & TxQ** (`xrpld-fee-market`) — TxQ depth/capacity, fee levels, load factor breakdown, fee escalation timeline
  2. **Job Queue Analysis** (`xrpld-job-queue`) — Per-job-type rates, queue wait times, execution times, job queue depth

- Update 2 existing dashboards:
  1. **Node Health** (`xrpld-statsd-node-health`) — Add NodeStore I/O panels, cache hit rate panels, object instance counts
  2. **RPC Performance** (`xrpld-rpc-perf`) — Add per-method RPC breakdown panels

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
  - Start xrpld with `[telemetry] enabled=1` and `[insight] server=otel`
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

## Task 9.11: Validator Health Dashboard (External Dashboard Parity)

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md) — dashboards for Phase 7 metrics inspired by the community [xrpl-validator-dashboard](https://github.com/realgrapedrop/xrpl-validator-dashboard).
>
> **Upstream**: Phase 7 Tasks 7.9-7.16 (metrics must be emitting).
> **Downstream**: Phase 10 (dashboard load checks), Phase 11 (alert rules reference these panels).

**Objective**: Create a Grafana dashboard for validation agreement, amendment/UNL health, and state tracking.

**Dashboard**: `xrpld-validator-health.json`

| Panel                      | Type       | PromQL                                                         |
| -------------------------- | ---------- | -------------------------------------------------------------- |
| Agreement % (1h)           | stat       | `xrpld_validation_agreement{metric="agreement_pct_1h"}`        |
| Agreement % (24h)          | stat       | `xrpld_validation_agreement{metric="agreement_pct_24h"}`       |
| Agreements vs Missed (1h)  | bargauge   | `agreements_1h` and `missed_1h` side by side                   |
| Agreements vs Missed (24h) | bargauge   | `agreements_24h` and `missed_24h` side by side                 |
| Validation Rate            | stat       | `rate(xrpld_validations_sent_total[5m]) * 60`                  |
| Validations Checked Rate   | stat       | `rate(xrpld_validations_checked_total[5m]) * 60`               |
| Amendment Blocked          | stat       | `xrpld_validator_health{metric="amendment_blocked"}`           |
| UNL Expiry (days)          | stat       | `xrpld_validator_health{metric="unl_expiry_days"}`             |
| Validation Quorum          | stat       | `xrpld_validator_health{metric="validation_quorum"}`           |
| State Value Timeline       | timeseries | `xrpld_state_tracking{metric="state_value"}`                   |
| Time in Current State      | stat       | `xrpld_state_tracking{metric="time_in_current_state_seconds"}` |
| State Changes Rate         | stat       | `rate(xrpld_state_changes_total[1h])`                          |
| Ledgers Closed Rate        | stat       | `rate(xrpld_ledgers_closed_total[5m]) * 60`                    |

**Dashboard conventions**: `$node` template variable for `exported_instance` filtering, dark theme, matching existing panel sizes and color schemes.

**Key new files**: `docker/telemetry/grafana/dashboards/rippled-validator-health.json`

**Exit Criteria**:

- [ ] All 13 panels render with non-zero data during normal operation
- [ ] `$node` filter works correctly for multi-node deployments
- [ ] Amendment blocked and UNL expiry panels use color thresholds (red=blocked/expiring)

---

## Task 9.12: Peer Quality Dashboard (External Dashboard Parity)

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md)

**Objective**: Create a Grafana dashboard for peer health aggregates.

**Dashboard**: `xrpld-peer-quality.json`

| Panel                  | Type       | PromQL                                                         |
| ---------------------- | ---------- | -------------------------------------------------------------- |
| P90 Peer Latency       | timeseries | `xrpld_peer_quality{metric="peer_latency_p90_ms"}`             |
| Insane/Diverged Peers  | stat       | `xrpld_peer_quality{metric="peers_insane_count"}`              |
| Higher Version Peers % | stat       | `xrpld_peer_quality{metric="peers_higher_version_pct"}`        |
| Upgrade Recommended    | stat       | `xrpld_peer_quality{metric="upgrade_recommended"}`             |
| Resource Disconnects   | timeseries | `xrpld_Overlay_Peer_Disconnects_Charges`                       |
| Inbound vs Outbound    | bargauge   | `xrpld_Peer_Finder_Active_Inbound_Peers`, `..._Outbound_Peers` |

**Key new files**: `docker/telemetry/grafana/dashboards/rippled-peer-quality.json`

**Exit Criteria**:

- [ ] All 6 panels render correctly
- [ ] P90 latency panel shows trend over time
- [ ] Upgrade recommended panel uses color threshold (red=1, green=0)

---

## Task 9.13: Ledger Economy Dashboard Panels (External Dashboard Parity)

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md)

**Objective**: Add "Ledger Economy" row to the existing `system-node-health.json` dashboard.

| Panel                | Type       | PromQL                                              |
| -------------------- | ---------- | --------------------------------------------------- |
| Base Fee (drops)     | stat       | `xrpld_ledger_economy{metric="base_fee_xrp"}`       |
| Reserve Base (drops) | stat       | `xrpld_ledger_economy{metric="reserve_base_xrp"}`   |
| Reserve Inc (drops)  | stat       | `xrpld_ledger_economy{metric="reserve_inc_xrp"}`    |
| Ledger Age           | stat       | `xrpld_ledger_economy{metric="ledger_age_seconds"}` |
| Transaction Rate     | timeseries | `xrpld_ledger_economy{metric="transaction_rate"}`   |

**Key modified files**: `docker/telemetry/grafana/dashboards/system-node-health.json`

**Exit Criteria**:

- [ ] 5 new panels render correctly in existing dashboard
- [ ] Fee values match `server_info` RPC output
- [ ] Transaction rate shows smooth trend (not spiky)

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
- [ ] Validator Health dashboard renders all 13 panels
- [ ] Peer Quality dashboard renders all 6 panels
- [ ] Ledger Economy panels added to system-node-health dashboard
