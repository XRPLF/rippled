<!-- cspell:ignore ISTOGRAM -->
<!-- The all-caps macro name XRPL_METRIC_HISTOGRAM_RECORD trips cspell's
     compound-word splitter, which emits the subword "ISTOGRAM"; ignore it here. -->

# Phase 9: Internal Metric Instrumentation Gap Fill — Task List

> **Status**: Complete for Tasks 9.1-9.13. Tasks 9.14-9.17 remain open by design
> (see each task for the blocker).
>
> **Goal**: Instrument xrpld to emit ~50+ metrics that exist in `get_counts`/`server_info`/TxQ/PerfLog but currently lack time-series export via the OTel or beast::insight pipelines.
>
> **Scope**: Hybrid approach — extend `beast::insight` for metrics near existing registrations, use OTel Metrics SDK `ObservableGauge` callbacks for new categories (TxQ, PerfLog, CountedObjects).
>
> **Branch**: `pratik/otel-phase9-metric-gap-fill` (from `pratik/otel-phase8-log-correlation`)
>
> **Depends on**: Phase 7 (native OTel metrics pipeline) and Phase 8 (log-trace correlation)

> **Note on metric names**: there is **no `xrpld_` prefix** on any emitted
> metric. `77f35c03db` removed it and lowercased names, and
> `OTelCollectorImp::formatName()`
> (`src/libxrpl/beast/insight/OTelCollector.cpp:855-874`) adds no prefix at all —
> it only lowercases the raw name and turns `.` and spaces into `_`. Earlier
> revisions of this task list spelled every metric `xrpld_<name>`; those spellings
> have been corrected in place to the emitted names, so the names below can be
> pasted into Prometheus as written. Instruments created in
> `src/xrpld/telemetry/MetricsRegistry.cpp` (35 of them) are the single source of
> truth. `MetricsRegistry.h`'s Doxygen used to disagree on three histogram names;
> those header comments were repaired in this change set (see Tasks 9.4 and 9.5),
> so header and `.cpp` now agree.
>
> **Two shapes do not simply lose the prefix**, so `xrpld_<name>` → `<name>` is
> not a blanket rule:
>
> - **Multiplexed observable gauges.** Most of the value names in these task
>   descriptions are a **`metric` label value** on a shared instrument, not a
>   standalone metric name — queue depth is `txq_metrics{metric="txq_count"}`, not
>   `txq_count`. The same applies to `nodestore_state`, `cache_metrics`,
>   `load_factor_metrics`, `server_info`, `db_metrics`, `validator_health`,
>   `peer_quality`, `state_tracking` and `ledger_economy`. Each task below names
>   its owning instrument.
> - **Unit-suffixed histograms** coming through `beast::insight`.
>   `OTelCollectorImp` appends the unit to the name, so the `ios_latency`
>   histogram is `ios_latency_milliseconds_bucket` in Prometheus — not
>   `ios_latency_bucket`. Instruments created directly on `MetricsRegistry` keep
>   their literal name (`job_queued_us_bucket`, `rpc_method_us_bucket`) because
>   the unit is already in the instrument name.

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

> **As shipped, this did _not_ go through `beast::insight`.** `Database.cpp` has
> no insight members. The metrics are a single `nodestore_state`
> `Int64ObservableGauge` on `MetricsRegistry`
> (`src/xrpld/telemetry/MetricsRegistry.cpp:957-965`) whose callback reads
> `Database`'s public accessors (`getFetchTotalCount()`, `getFetchHitCount()`,
> `getStoreCount()`, `getFetchDurationUs()`, `getStoreDurationUs()`, …) and
> multiplexes every value onto the `metric` label. Write-queue depth comes from
> the new `include/xrpl/nodestore/WriteStats.h`.

- Export the following as `nodestore_state{metric="…"}` label values:
  - Gauge: `node_reads_total` (cumulative read operations)
  - Gauge: `node_reads_hit` (fetches that found an object — not a cache hit; `fetchHitCount_` increments whatever served the fetch)
  - Gauge: `node_writes` (cumulative write operations)
  - Gauge: `node_written_bytes` (cumulative bytes written)
  - Gauge: `node_read_bytes` (cumulative bytes read)
  - Gauge: `node_reads_duration_us` (cumulative read time in microseconds)
  - Gauge: `write_load` (current write load score)
  - Gauge: `read_queue` (items in read queue)

- These values are already computed in `Database::getCountsJson()`. The gauge
  callback reads the same counters through `Database`'s public accessors.

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.cpp` (the `nodestore_state` gauge)
- `include/xrpl/nodestore/Database.h` (accessors; **not** `src/libxrpl/nodestore/Database.h`, which does not exist)
- `include/xrpl/nodestore/WriteStats.h` (new — write-queue depth snapshot)

**Derived Prometheus metrics**: `nodestore_state{metric="node_reads_total"}`,
`nodestore_state{metric="node_reads_hit"}`, `nodestore_state{metric="write_load"}`,
etc. There is **no** `xrpld_` prefix — `OTelCollectorImp::formatName()` adds none.

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

- The callback reads from the same sources as the `GetCounts` handler
  (`src/xrpld/rpc/handlers/admin/status/GetCounts.cpp` — **not**
  `src/xrpld/rpc/handlers/GetCounts.cpp`).

- Create a centralized `MetricsRegistry` class that holds all OTel async gauge registrations, polled at 10-second intervals by the `PeriodicMetricReader`.

**Key modified files**:

- New: `src/xrpld/telemetry/MetricsRegistry.h` / `.cpp`
- New: `src/xrpld/telemetry/MetricMacros.h` (the `XRPL_METRIC_*` call-site macros)
- `src/xrpld/rpc/handlers/admin/status/GetCounts.cpp` (extract shared access methods)
- `src/xrpld/app/main/Application.cpp` (register MetricsRegistry at startup)

**Derived Prometheus metrics**: `cache_metrics{metric="SLE_hit_rate"}`,
`cache_metrics{metric="ledger_hit_rate"}`, `cache_metrics{metric="treenode_cache_size"}`,
etc. Label values are **case-sensitive** (`SLE_hit_rate`, `AL_size`, `AL_hit_rate`).

---

## Task 9.3: Transaction Queue (TxQ) Metrics

**Objective**: Export TxQ depth, capacity, and fee escalation levels as time-series.

**What to do**:

- Register OTel `ObservableGauge` callbacks for TxQ state (from
  `src/xrpld/app/misc/TxQ.h` — **not** `src/xrpld/app/tx/detail/TxQ.h`):
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
- `src/xrpld/app/misc/TxQ.h` (expose metrics accessor if needed)

**Derived Prometheus metrics**: `txq_metrics{metric="txq_count"}`,
`txq_metrics{metric="txq_max_size"}`, `txq_metrics{metric="txq_open_ledger_fee_level"}`, etc.
There is one instrument, `txq_metrics` (`MetricsRegistry.cpp:705`); each value above
is a `metric` label value, not a metric name of its own.

**Grafana dashboard**: New _Fee Market & TxQ_ dashboard (`fee-market`).

---

## Task 9.4: PerfLog Per-RPC Method Metrics

**Objective**: Export per-RPC-method call counts and latency as OTel metrics.

**What to do**:

- Register OTel instruments for PerfLog RPC counters (from `PerfLogImp.cpp`):
  - Counter: `rpc_method_started_total{method="<name>"}` — calls started
  - Counter: `rpc_method_finished_total{method="<name>"}` — calls completed
  - Counter: `rpc_method_errored_total{method="<name>"}` — calls errored
  - Histogram: `rpc_method_us{method="<name>"}` — execution time distribution

- Use OTel `Counter<uint64_t>` and `Histogram<double>` instruments with the
  `method` attribute label. The RPC instruments carry **only** `method`
  (`MetricsRegistry.cpp:436-475`) — the `handler` label belongs to the job
  instruments (Task 9.5), not these.

> **Naming**: the instrument is `rpc_method_us` — declared as
> `kRpcMethodDurationUs` at `MetricsRegistry.cpp:96` and used both to register the
> explicit-bucket view and to create the instrument. `MetricsRegistry.h`'s Doxygen
> comment used to read `rpc_method_duration_us`; **that was fixed in this change**
> (`MetricsRegistry.h:789`), so header and `.cpp` now agree and there is no
> caveat left. The prefix `xrpld_` in the original spec is not emitted by anything.
>
> Same for the job histograms in Task 9.5: `job_queued_us` / `job_running_us`.

- Hook into the existing PerfLog callback mechanism rather than adding new instrumentation points.

**Key modified files**:

- `src/xrpld/perflog/detail/PerfLogImp.cpp` (add OTel instrument updates alongside existing JSON counters)
- `src/xrpld/telemetry/MetricsRegistry.cpp` (register instruments)

**Derived Prometheus metrics**: `rpc_method_started_total{method="server_info"}`, `rpc_method_us_bucket{method="ledger"}`, etc.

**Grafana dashboard**: Add "Per-Method RPC Breakdown" panel group to _RPC Performance_ dashboard.

---

## Task 9.5: PerfLog Per-Job-Type Metrics

**Objective**: Export per-job-type queue and execution metrics.

**What to do**:

- Register OTel instruments for PerfLog job counters. All five carry **two**
  labels — `job_type` and `handler` — so producers sharing a job type stay
  distinguishable (`MetricsRegistry.h:794-818`, recorded at
  `MetricsRegistry.cpp:498,518,527,548,553`). `handler` is the sanitised
  `addJob` name; `sanitiseHandler()` folds dynamic names into a bounded domain
  of exactly 44 values, so cardinality stays fixed.
  - Counter: `job_queued_total{job_type="<name>",handler="<name>"}` — jobs queued
  - Counter: `job_started_total{job_type="<name>",handler="<name>"}` — jobs started
  - Counter: `job_finished_total{job_type="<name>",handler="<name>"}` — jobs completed
  - Histogram: `job_queued_us{job_type="<name>",handler="<name>"}` — time spent waiting in queue
  - Histogram: `job_running_us{job_type="<name>",handler="<name>"}` — execution time distribution

> **Naming**: the instruments are `job_queued_us` / `job_running_us`
> (`kJobQueuedDurationUs` / `kJobRunningDurationUs`, `MetricsRegistry.cpp:94-95`).
> `MetricsRegistry.h`'s Doxygen comments used to read
> `job_queued_duration_us` / `job_running_duration_us`; **both were fixed in this
> change** (`MetricsRegistry.h:810,815`), so there is no header/`.cpp` divergence
> left to work around.

- Hook into PerfLog's existing job tracking alongside Task 9.4.

**Key modified files**:

- `src/xrpld/perflog/detail/PerfLogImp.cpp`
- `src/xrpld/telemetry/MetricsRegistry.cpp`

**Derived Prometheus metrics**: `job_queued_total{job_type="ledgerData",handler="ProcessLData"}`, `job_running_us_bucket{job_type="transaction",handler="…"}`, etc.

**Grafana dashboard**: New _Job Queue Analysis_ dashboard (`job-queue`).

---

## Task 9.6: Counted Object Instance Metrics

**Objective**: Export live instance counts for key internal object types.

**What to do**:

- Register OTel `ObservableGauge` callbacks for `CountedObject<T>` instance counts:
  - `object_count{type="xrpl::Transaction"}` — live Transaction objects
  - `object_count{type="xrpl::Ledger"}` — live Ledger objects
  - `object_count{type="xrpl::NodeObject"}` — live NodeObject instances
  - `object_count{type="xrpl::STTx"}` — serialized transaction objects
  - `object_count{type="xrpl::STLedgerEntry"}` — serialized ledger entries
  - `object_count{type="xrpl::InboundLedger"}` — ledgers being fetched
  - `object_count{type="xrpl::Pathfinder"}` — active pathfinding computations
  - `object_count{type="xrpl::PathRequest"}` — active path requests
  - `object_count{type="xrpl::HashRouter::Entry"}` — hash router entries (the type is
    `HashRouter::Entry`; there is no `HashRouterEntry` type)

- The `CountedObject` template already tracks these via atomic counters. The callback just reads the current counts.

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.cpp` (add counted object callbacks)
- `include/xrpl/basics/CountedObject.h` (may need static accessor for iteration)

**Derived Prometheus metrics**: `object_count{type="xrpl::Transaction"}`, `object_count{type="xrpl::NodeObject"}`, etc.
The `type` label value is `beast::typeName<Object>()` — the fully-qualified
demangled C++ type name (`CountedObject.h:109`), not a short word.

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

**Derived Prometheus metrics**: `load_factor_metrics{metric="load_factor"}`,
`load_factor_metrics{metric="load_factor_fee_escalation"}`, etc. There is one
instrument, `load_factor_metrics` (`MetricsRegistry.cpp:785`); every value listed
above is a `metric` label value, not a metric name of its own.

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
- Added new `server_info` Int64ObservableGauge with 8 metrics:
  - `server_state` — operating mode as int (0=DISCONNECTED .. 4=FULL)
  - `uptime` — seconds since server start
  - `peers` — total peer count
  - `validated_ledger_seq` — validated ledger sequence (atomic read)
  - `ledger_current_index` — current open ledger sequence
  - `peer_disconnects_resources` — cumulative resource-related disconnects
  - `last_close_proposers` — from `getConsensusInfo()["previous_proposers"]`
  - `last_close_converge_time_ms` — from `getConsensusInfo()["previous_mseconds"]`
- Added new `build_info` Int64ObservableGauge (info-style, value=1 with `version` label)
- Added new `complete_ledgers` Int64ObservableGauge parsing comma-separated ranges into `{bound, index}` pairs
- Added new `db_metrics` Int64ObservableGauge with 4 metrics:
  - `db_kb_total`, `db_kb_ledger`, `db_kb_transaction` (SQLite stat queries)
  - `historical_perminute` (historical ledger fetch rate)

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.h` (4 new gauge members, updated ASCII diagram)
- `src/xrpld/telemetry/MetricsRegistry.cpp` (4 new callback registrations, 2 callback extensions)

**Not implementable inside xrpld**:

- `connection_count_51233/51234` — OS-level port connection counts from external shell script (`get_connection.sh`)

**Derived Prometheus metrics**: `server_info{metric="server_state"}`, `build_info{version="2.4.0"}`, `complete_ledgers{bound="start",index="0"}`, `db_metrics{metric="db_kb_total"}`, etc.

**Grafana dashboard**: New panels added to _Node Health_ dashboard (`node-health.json`).

---

## Task 9.8: New Grafana Dashboards

**Objective**: Create Grafana dashboards for the new metric categories.

**What to do**:

- Create 2 new dashboards:
  1. **Fee Market & TxQ** (`fee-market`) — TxQ depth/capacity, fee levels, load factor breakdown, fee escalation timeline
  2. **Job Queue Analysis** (`job-queue`) — Per-job-type rates, queue wait times, execution times, job queue depth

- Update 2 existing dashboards:
  1. **Node Health** (`node-health`) — Add NodeStore I/O panels, cache hit rate panels, object instance counts
  2. **RPC Performance** (`rpc-performance`) — Add per-method RPC breakdown panels

> Tasks 9.11-9.13 add two more new dashboards (`validator-health`,
> `peer-quality`), so Phase 9's total is **4 new + 2 updated**.

**Key modified files** (filenames and uids after the `rippled-*` → bare rename
in `145b1469d6` and `25868f2740` — the `rippled-*.json` paths no longer exist):

- New: `docker/telemetry/grafana/dashboards/fee-market.json` (uid `fee-market`)
- New: `docker/telemetry/grafana/dashboards/job-queue.json` (uid `job-queue`)
- `docker/telemetry/grafana/dashboards/node-health.json` (uid `node-health`)
- `docker/telemetry/grafana/dashboards/rpc-performance.json` (uid `rpc-performance`)

---

## Task 9.9: Update Documentation

**Objective**: Update telemetry reference docs with all new metrics.

**What to do**:

- Update `OpenTelemetryPlan/09-data-collection-reference.md`: ✅ done
  - Add new section for OTel SDK-exported metrics (NodeStore, cache, TxQ, PerfLog, CountedObjects, load factors) — §5b + "Phase 9: OTel SDK-Exported Metrics (MetricsRegistry)"
  - Update Grafana dashboard reference table (add 4 new dashboards) — "New Grafana Dashboards (Phase 9)" / "Updated Grafana Dashboards (Phase 9)"
  - Add Prometheus query examples for new metrics

- Update `docs/telemetry-runbook.md`:
  - ✅ Alerting section covering the provisioned rules and how to wire a receiver
  - ✅ Troubleshooting entries for new metric categories
  - ❌ **Still open**: dashboard guides for **six** dashboards — `fee-market`,
    `job-queue`, `ledger-data-sync`, `overlay-traffic-detail`, `peer-quality` and
    `validator-health`. The runbook's dashboard reference records the gap
    verbatim: "Nine dashboards have a reference section below. `fee-market`,
    `job-queue`, `ledger-data-sync`, `overlay-traffic-detail`, `peer-quality`, and
    `validator-health` are provisioned but not yet documented here — their panel
    descriptions carry the same six-heading reference format, so open the panel
    info icon in Grafana until a section is written." (15 provisioned − 6
    undocumented = 9 documented.) Also still open: the Validation Agreement
    explainer (8s grace / 5m late repair)

- Provision Grafana alert rules (`docker/telemetry/grafana/provisioning/alerting/`) — **as shipped**:
  - **13 rules in 5 groups**: `xrpld-consensus` (`LedgerHistoryMismatch`,
    `LedgerCloseStalled`, `ValidatedLedgerStale`), `xrpld-validator`
    (`ValidationsMissed`, `ValidationsNotChecked`), `xrpld-jobqueue`
    (`JobQueueTxOverflow`, `JobQueueLatencyHigh`, `NodeStoreIOLatencyHigh`),
    `xrpld-node-state` (`NodeStateFlapping`, `NodeNotFull`), `xrpld-overlay`
    (`ManifestJobQueueConvoy`, `ManifestFloodInbound`, `PeerResourceDisconnects`)
  - **2 contact points** — `xrpld-default` (Slack) and `xrpld-critical`
    (Slack + email) — and a **nested** notification policy: root →
    `xrpld-default`, child route `severity = critical` → `xrpld-critical`.
    Auto-loaded via the existing `provisioning/` mount (no docker-compose change)
  - 3 rules are `severity: critical`, 10 are `severity: warning`
  - Alerting operator docs (per-alert meaning, tuning, receiver wiring) now live in the Alerting section of `docs/telemetry-runbook.md`

**Key modified files**:

- `OpenTelemetryPlan/09-data-collection-reference.md`
- `docs/telemetry-runbook.md`
- `docker/telemetry/grafana/provisioning/alerting/{rules,contactpoints,policies}.yaml` (new)
- `docs/telemetry-runbook.md` (Alerting section added)

---

## Task 9.10: Integration Tests

**Objective**: Verify all new metrics appear in Prometheus after a test workload.

**What to do**:

- ❌ **Not done on this branch**: extend the telemetry integration test to
  start xrpld with `[telemetry] enabled=1` / `[insight] server=otel`, drive RPC
  and transaction load, query Prometheus for each new metric family and assert
  non-zero values. The end-to-end metric assertions live in the **Phase 10**
  harness (`docker/telemetry/workload/expected_metrics.json`), not here.

- ✅ **Done**: unit tests for the `MetricsRegistry` class —
  `src/tests/libxrpl/telemetry/MetricsRegistry.cpp` (**18** GTest cases —
  `grep -cE '\bTEST(_F|_P)?\s*\(' src/tests/libxrpl/telemetry/MetricsRegistry.cpp`
  = 18, and the four bullets below sum to 4 + 3 + 5 + 6 = 18):
  - Callback registration / deregistration and shutdown ordering —
    `async_gauges_start_after_start_is_safe`,
    `async_gauges_before_start_does_not_break_start`,
    `async_gauges_respect_the_compile_time_guard`, `destructor_calls_stop`
  - Graceful behaviour when telemetry is disabled — `disabled_construction`,
    `disabled_start_stop`, `disabled_recording_methods`
  - Label sanitisation and mean scaling — `MetricsRegistrySanitiseHandler` (5
    cases, incl. `output_domain_is_exactly_44_values`) and
    `MetricsRegistryScaledMean` (6 cases)
  - ❌ Not covered: asserting metric values match `get_counts` JSON output —
    that needs a live `Application`, so it is left to the Phase 10 harness

**Key files**:

- `src/tests/libxrpl/telemetry/MetricsRegistry.cpp` (new). The originally
  planned `src/test/telemetry/MetricsRegistry_test.cpp` was **never created** —
  Phase 9 tests are GTest under `src/tests/libxrpl/`, per project convention.
- `src/tests/libxrpl/telemetry/MetricMacros.cpp`, `GetMeter.cpp` (new — cover
  the `XRPL_METRIC_*` macros and meter lookup)

---

## Task 9.11: Validator Health Dashboard (External Dashboard Parity)

> **Source**: [External Dashboard Parity](./06-implementation-phases.md#appendix-external-dashboard-parity) — dashboards for Phase 7 metrics inspired by the community [xrpl-validator-dashboard](https://github.com/realgrapedrop/xrpl-validator-dashboard).
>
> **Upstream**: Phase 7 Tasks 7.9-7.16 (metrics must be emitting).
> **Downstream**: Phase 10 (dashboard load checks), Phase 11 (alert rules reference these panels).

**Objective**: Create a Grafana dashboard for validation agreement, amendment/UNL health, and state tracking.

**Dashboard**: `validator-health.json`

| Panel                      | Type       | PromQL                                                   |
| -------------------------- | ---------- | -------------------------------------------------------- |
| Agreement % (1h)           | stat       | `validation_agreement{metric="agreement_pct_1h"}`        |
| Agreement % (24h)          | stat       | `validation_agreement{metric="agreement_pct_24h"}`       |
| Agreements vs Missed (1h)  | bargauge   | `agreements_1h` and `missed_1h` side by side             |
| Agreements vs Missed (24h) | bargauge   | `agreements_24h` and `missed_24h` side by side           |
| Validation Rate            | stat       | `rate(validations_sent_total[5m]) * 60`                  |
| Validations Checked Rate   | stat       | `rate(validations_checked_total[5m]) * 60`               |
| Amendment Blocked          | stat       | `validator_health{metric="amendment_blocked"}`           |
| UNL Expiry (days)          | stat       | `validator_health{metric="unl_expiry_days"}`             |
| Validation Quorum          | stat       | `validator_health{metric="validation_quorum"}`           |
| State Value Timeline       | timeseries | `state_tracking{metric="state_value"}`                   |
| Time in Current State      | stat       | `state_tracking{metric="time_in_current_state_seconds"}` |
| State Changes Rate         | stat       | `rate(state_changes_total[1h])`                          |
| Ledgers Closed Rate        | stat       | `rate(ledgers_closed_total[5m]) * 60`                    |

**Dashboard conventions**: `$node` template variable for `service_instance_id` filtering, dark theme, matching existing panel sizes and color schemes.

**Key new files**: `docker/telemetry/grafana/dashboards/validator-health.json`
(uid `validator-health`). The name reached its current form in **two** renames:
`rippled-validator-health.json` → `xrpld-validator-health.json` (`145b1469d6`,
the `rippled-` → `xrpld-` pass), then `xrpld-validator-health.json` →
`validator-health.json` (`25868f2740`, which dropped the `xrpld-` prefix).

**Exit Criteria**:

- [x] Dashboard ships **17** panels (4 more than the 13 planned above) across 3
      rows — Validation Agreement, Validation Rates, Server State & Consensus
- [ ] All panels render with non-zero data during normal operation — needs a live
      stack; the Phase 10 harness asserts the dashboard _loads_, not that panels
      are non-empty
- [x] `$node` filter works correctly for multi-node deployments — `node`
      template variable present (filters on `service_instance_id`), alongside
      `service_name`, `deployment_environment`, `xrpl_network_type`,
      `xrpl_work_item`, `xrpl_branch`, `xrpl_node_role`
- [x] Amendment blocked and UNL expiry panels use color thresholds
      (red=blocked/expiring) — 11 `thresholds` blocks in the dashboard JSON

---

## Task 9.12: Peer Quality Dashboard (External Dashboard Parity)

> **Source**: [External Dashboard Parity](./06-implementation-phases.md#appendix-external-dashboard-parity)

**Objective**: Create a Grafana dashboard for peer health aggregates.

**Dashboard**: `peer-quality.json`

| Panel                  | Type       | PromQL                                                                  |
| ---------------------- | ---------- | ----------------------------------------------------------------------- |
| P90 Peer Latency       | timeseries | `peer_quality{metric="peer_latency_p90_ms"}`                            |
| Insane/Diverged Peers  | stat       | `peer_quality{metric="peers_insane_count"}`                             |
| Higher Version Peers % | stat       | `peer_quality{metric="peers_higher_version_pct"}`                       |
| Upgrade Recommended    | stat       | `peer_quality{metric="upgrade_recommended"}`                            |
| Resource Disconnects   | timeseries | `server_info{metric="peer_disconnects_resources"}`                      |
| Inbound vs Outbound    | bargauge   | `peer_finder_active_inbound_peers`, `peer_finder_active_outbound_peers` |

> `overlay_peer_disconnects_charges` (the name in the original spec) is **not a
> real instrument** — nothing registers it. The shipped panel reads
> `server_info{metric="peer_disconnects_resources"}` instead. Peer-finder gauge
> names are lowercase: `GroupImp::makeName()` + `OTelCollectorImp::formatName()`
> turn the `"Peer_Finder"` group into `peer_finder_<name>` with no prefix.

**Key new files**: `docker/telemetry/grafana/dashboards/peer-quality.json`
(uid `peer-quality`). Two renames, same as Task 9.11:
`rippled-peer-quality.json` → `xrpld-peer-quality.json` (`145b1469d6`), then
`xrpld-peer-quality.json` → `peer-quality.json` (`25868f2740`).

**Exit Criteria**:

- [x] All 6 panels present — P90 Peer Latency, Insane/Diverged Peers, Higher
      Version Peers %, Upgrade Recommended, Inbound vs Outbound Peers, Resource
      Disconnects — across 3 rows, with the `$node` template variable
- [ ] All 6 panels render with data — needs a live stack
- [x] P90 latency panel is a `timeseries` (shows trend over time)
- [x] Upgrade recommended panel uses color threshold (red=1, green=0) — 5
      `thresholds` blocks in the dashboard JSON

---

## Task 9.13: Ledger Economy Dashboard Panels (External Dashboard Parity)

> **Source**: [External Dashboard Parity](./06-implementation-phases.md#appendix-external-dashboard-parity)

**Objective**: Add "Ledger Economy" row to the existing `node-health.json` dashboard.

| Panel                | Type       | PromQL                                        |
| -------------------- | ---------- | --------------------------------------------- |
| Base Fee (drops)     | stat       | `ledger_economy{metric="base_fee_xrp"}`       |
| Reserve Base (drops) | stat       | `ledger_economy{metric="reserve_base_xrp"}`   |
| Reserve Inc (drops)  | stat       | `ledger_economy{metric="reserve_inc_xrp"}`    |
| Ledger Age           | stat       | `ledger_economy{metric="ledger_age_seconds"}` |
| Transaction Rate     | timeseries | `ledger_economy{metric="transaction_rate"}`   |

**Key modified files**: `docker/telemetry/grafana/dashboards/node-health.json`

**Exit Criteria**:

- [x] 5 new panels present in the existing dashboard — a "Ledger Economy" row
      with 5 `ledger_economy` queries is on `node-health.json`
- [ ] Fee values match `server_info` RPC output — needs a live comparison
- [ ] Transaction rate shows smooth trend (not spiky) — needs a live run

---

## Task 9.14: Overlay Traffic Accounting Defects (Documentation Only)

> **Status**: DOCUMENTED, NOT FIXED. Reference: [09 §6.0-§6.2](./09-data-collection-reference.md#6-known-issues)

**Objective**: Record four pre-existing overlay traffic-accounting defects so
dashboard readers are not misled. All four originate in `develop`-owned overlay
files, so **no code fix lands on this branch**.

| #   | Defect                                 | Effect                                                                                    | Fix location (NOT this branch)                   |
| --- | -------------------------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------ |
| 1   | `mtCLUSTER` missing from `kTypeLookup` | `overhead_cluster_*` always zero; 8 panels flatline; cluster traffic counted as `unknown` | `TrafficCount.cpp:11-27`                         |
| 2   | Stale `Total` header comment           | Claims uncategorized traffic is excluded; it is included                                  | `TrafficCount.h:28-31`                           |
| 3   | `SquelchIgnored` reported with size 0  | `squelch_ignored_bytes_*` always zero, inconsistent with `SquelchSuppressed`              | `OverlayImpl.cpp:1460,1489` (+ signature change) |
| 4   | In/out byte-basis asymmetry            | `_bytes_in` vs `_bytes_out` not comparable under compression                              | `PeerImp.cpp:1079` vs `:313`                     |

**Why deferred**: Defect 3 requires widening the two
`OverlayImpl::updateSlotAndSquelch` overloads — a public signature change on
shared overlay code. Defects 1 and 4 need `TrafficCount.cpp` and `PeerImp.cpp`
edits that are not telemetry-owned. Routing them through the telemetry chain
would hide overlay changes from overlay reviewers and couple them to a 12-PR
merge timeline.

> **Constraint narrowed.** The blanket "no telemetry change may touch
> `TrafficCount.{h,cpp}`" no longer holds for the header: the telemetry chain
> already edits `TrafficCount.h` — Phase 6's `77f35c03db` fixed the
> `Category::GetFetchPack` label from `"getobject_Fetch Pack_get"` to
> `"getobject_Fetch_Pack_get"` at `TrafficCount.h:285`, the sole difference from
> `develop`. Defect 2 (the stale `Total` header comment, `TrafficCount.h:28-31`)
> is therefore **unblocked** and can land here. Defects **1, 3 and 4** stay
> blocked: defect 1 needs `TrafficCount.cpp`'s `kTypeLookup`, defect 3 needs the
> `OverlayImpl` signature change, and defect 4 needs `PeerImp.cpp:1079` vs `:313`
> to agree on a byte basis (compressed vs uncompressed) — a change to overlay
> accounting semantics, not telemetry.

**Key modified files**: `OpenTelemetryPlan/09-data-collection-reference.md` only.

**Exit Criteria**:

- [x] Each defect documented with file:line evidence in `09` §6
- [x] `overhead_cluster_*` documented as "no data", not "no cluster traffic"
- [ ] Defect 2 (stale `Total` header comment, `TrafficCount.h:28-31`) fixed on
      this branch — it is **unblocked** (the chain already edits
      `TrafficCount.h`) but the comment is still uncorrected
- [ ] Follow-up overlay-owned branch raised for the three still-blocked code
      fixes (defects 1, 3, 4)
- [ ] Re-baseline any threshold keyed on `unknown_bytes_in` when defect 1 lands

---

## Task 9.15: Peer Keepalive and Discovery Instrumentation

> **Status**: NOT IMPLEMENTED. The instruments themselves are still to be
> written; the _permission_ question is settled. Reference:
> [09 §6.3](./09-data-collection-reference.md#63-peer-keepalive-and-discovery-traffic-gaps-not-implemented)
>
> **Blocker cleared.** This task used to be held "awaiting a decision on whether
> `XRPL_METRIC_*` call sites may be added to
> `src/xrpld/overlay/detail/PeerImp.cpp` from this branch". That decision is
> de facto **yes** — `PeerImp.cpp` already carries **7** such call sites on this
> branch (`:2723`, `:2741`, `:2925`, `:2928`, `:2931`, `:2947`, `:2954`, of which
> three are `XRPL_METRIC_HISTOGRAM_RECORD` — `:2925`, `:2928`, `:2931` — and four
> are labelled counters — `:2723`, `:2741`, `:2947`, `:2954`). Note that
> `grep -c XRPL_METRIC src/xrpld/overlay/detail/PeerImp.cpp` returns 8: the eighth
> hit is the `cspell:ignore` explanation comment at `PeerImp.cpp:2`, not a call
> site. What remains is the implementation work below, not an approval.

**Objective**: Make peer keepalive and peer-discovery health observable. Today
`mtPING`, `mtSTATUS_CHANGE` and `mtENDPOINTS` are byte counters only.

| Proposed metric                 | Type      | Labels                           | Record site                                         |
| ------------------------------- | --------- | -------------------------------- | --------------------------------------------------- |
| `peer_ping_rtt_ms`              | Histogram | none (see note)                  | `PeerImp.cpp:1150-1163`, where the EWMA is computed |
| `peer_ping_timeouts_total`      | Counter   | `reason="timeout"\|"bad_cookie"` | `PeerImp.cpp:762` and `:1146`                       |
| `peer_endpoints_received_total` | Counter   | `result="accepted"\|"malformed"` | `PeerImp.cpp:1265-1270`                             |

**Design notes / open questions**:

- A histogram needs an explicit bucket view: the SDK default tops out at 10000,
  and these are milliseconds. Follow the µs-ladder precedent in
  `MetricsRegistry.cpp` (see [09 § GetObject Request Path](./09-data-collection-reference.md#getobject-request-path-synchronous-countershistograms)).
- `peer_id` as a label is unbounded cardinality — rejected. A bounded
  `peer_role`-style label is the alternative if per-peer attribution is needed.
- Splitting `mtPING` out of `Category::Base` is a `TrafficCount.cpp` change and
  therefore still blocked with Task 9.14 defect 1. (The `.h` half of that
  constraint no longer applies — see Task 9.14.)
- Per the runbook's "Adding a New Metric" contract, `_total` is reserved for
  monotonic counters; a histogram takes no suffix.

**Key files (if approved)**: `src/xrpld/overlay/detail/PeerImp.cpp`,
`09-data-collection-reference.md`, `docs/telemetry-runbook.md` § Metric Reference,
`docker/telemetry/grafana/dashboards/peer-quality.json`, and
`docker/telemetry/workload/expected_metrics.json` (**Phase 10 branch**).

**Exit Criteria**:

- [x] Decision recorded on editing `PeerImp.cpp` from the telemetry chain — yes;
      7 `XRPL_METRIC_*` call sites already ship in `PeerImp.cpp`
- [ ] Three instruments emitting, with an explicit histogram bucket view
- [ ] Rows added to `09` §5b, runbook § Metric Reference, and `expected_metrics.json`
- [ ] Peer Quality dashboard panels follow the Task 9.12 conventions (`$node`, Title Case, legend dimensions)
- [ ] `check_otel_naming.py` passes (Rules D and E cover the new labels)

---

## Task 9.16: PeerFinder Slot and Cache Metrics

> **Status**: NOT IMPLEMENTED. Reference: [09 §6.5](./09-data-collection-reference.md#65-peerfinder-slot-and-cache-metrics-not-implemented)

**Objective**: Export the PeerFinder slot counts and discovery-cache sizes.
Only 2 of ~17 available readings are exported today.

**What to do**: Extend the existing `Stats` struct in
`src/libxrpl/peerfinder/PeerfinderManager.cpp:227-236` with gauges for the
`Counts` accessors listed in [09 §6.5](./09-data-collection-reference.md#65-peerfinder-slot-and-cache-metrics-not-implemented)
(slot caps and frees, attempt counts, handshake pipeline depth, fixed-peer state,
network reachability), plus `Livecache::size()` and `Bootcache::size()`.

**Pipeline constraint**: `PeerfinderManager.cpp` is in `libxrpl`, which **cannot**
use the `XRPL_METRIC_*` macros. These must go through `beast::insight` —
arrow **B**, not **C**. Naming follows `GroupImp::makeName()` +
`OTelCollectorImp::formatName()`, so the `"Peer_Finder"` group yields
`peer_finder_<name>` lowercased.

**Known obstacle**: `Livecache` and `Bootcache` hold no collector reference, so
their sizes must either be read through the existing `Manager` hook or have a
collector plumbed in.

**Exit Criteria**:

- [ ] Slot caps exported so utilization (`active / max`) is computable
- [ ] Both cache sizes exported
- [ ] "Inbound vs Outbound" panel on `peer-quality` extended to show utilization %
- [ ] Rows added to `09` §2.1, runbook § Metric Reference, `expected_metrics.json` (Phase 10)

---

## Task 9.17: Peer Span Coverage (Deferred to Phase 11)

> **Status**: NOT IMPLEMENTED — design only, pending approval. Reference:
> [09 §6.4](./09-data-collection-reference.md#64-peer-span-coverage-gap-not-implemented)
> and [02 §2.3.2](./02-design-decisions.md#232-complete-span-catalog)

**Objective**: Close the gap between the `02` §2.3.2 span catalog and what
actually emits. `peer.connect`, `peer.disconnect`, `peer.message.send` and
`peer.message.receive` were catalogued from the start and never built; 11 of 13
protocol message families have no spans.

**Scope warning**: This is larger than Tasks 9.14-9.16 combined and changes the
span-family inventory asserted in `09` §1.1 (**41** emitted families) and in
`docker/telemetry/workload/expected_spans.json` (**40** catalogued — `rpc.ws_upgrade`
has no entry). `trace_peer` is also **on by default** and already flagged as
high-volume, so adding per-message spans has a volume cost that needs measuring
before commitment.

**Exit Criteria**:

- [x] `02` §2.3.2 marked Live / Not built / Renamed against the real inventory
- [ ] User approval to proceed with span implementation
- [ ] Volume impact measured under `trace_peer=1` before any span is added

---

## Exit Criteria

- [ ] All ~50 new metrics visible in Prometheus via OTLP pipeline — every
      instrument is registered in `MetricsRegistry.cpp`, but end-to-end
      visibility is asserted only by the Phase 10 harness
- [x] `MetricsRegistry` class registers/deregisters cleanly with OTel SDK —
      `src/tests/libxrpl/telemetry/MetricsRegistry.cpp`
      (`async_gauges_start_after_start_is_safe`,
      `async_gauges_before_start_does_not_break_start`,
      `async_gauges_respect_the_compile_time_guard`, `destructor_calls_stop`)
- [x] Async gauge callbacks execute at 10s intervals —
      `MetricsRegistry.cpp:289`, `readerOpts.export_interval_millis = 10000`.
      (The "without performance impact" half is unmeasured — see below.)
- [x] 4 new Grafana dashboards operational (Fee Market, Job Queue, Validator
      Health, Peer Quality) — all four JSONs are under
      `docker/telemetry/grafana/dashboards/`
- [x] 2 existing dashboards updated with new panel groups — `node-health`
      (NodeStore I/O, Caches, Server Info, Complete Ledgers & DB, Ledger
      Economy, Job Queue Concurrency Limits rows) and `rpc-performance`
      (per-method section)
- [ ] Integration test validates all new metric families are non-zero — not on
      this branch; lives in the Phase 10 harness (`expected_metrics.json`)
- [ ] No performance regression (< 0.5% CPU overhead from new callbacks) — not
      measured; needs the Phase 10 benchmark suite
- [x] Documentation updated with full new metric inventory —
      `09-data-collection-reference.md` §5b + "Phase 9: OTel SDK-Exported
      Metrics (MetricsRegistry)" + "Phase 7+: External Dashboard Parity Metrics"
- [x] Validator Health dashboard ships (17 panels, 4 more than the 13 planned)
- [x] Peer Quality dashboard ships (6 panels)
- [x] Ledger Economy panels added to node-health dashboard (5 panels in a
      "Ledger Economy" row)
- [x] Provisioned Grafana alerting: 13 rules / 5 groups, 2 contact points,
      nested notification policy
- [ ] Tasks 9.14-9.17 closed — **open by design**: 9.14 documented-not-fixed
      (defects 1, 3 and 4 still blocked; defect 2 unblocked but not yet fixed),
      9.15 and 9.16 not implemented, 9.17 deferred pending approval and volume
      measurement

---

## Appendix: Alerting Design

> Design for the provisioned Grafana alert rules (Task 9.9a). Previously a standalone spec; merged here so the phase plan is self-contained.

**Date:** 2026-07-06
**Branch:** `pratik/otel-phase9-metric-gap-fill` (PR #6513)
**Status:** Approved

### Purpose

Phase 9 exports ~68 internal xrpld metrics and ships Grafana dashboards for
them. This adds the missing operator-facing piece: **provisioned Grafana alert
rules** that fire on the health-critical metrics phase 9 introduces. The
phase-9 task list already lists "alerting rules" as a phase-9 deliverable
(Task 9.9), so this closes that gap.

Scope is deliberately narrow — the three subsystems whose failure is
node-fatal: **consensus/ledger health, validator health, job queue**. RPC/API
health is explicitly out of scope.

### Why phase 9 (not phase 11)

Every metric these alerts fire on is _born_ in phase 9
(`ledger_history_mismatch_total`, `ledgers_closed_total`,
`validation_missed_total`, `validations_checked_total`,
`jq_trans_overflow_total`, `job_queued_us_bucket` — the histogram instrument is
`job_queued_us` (`MetricsRegistry.cpp:94`), so the Prometheus bucket series is
`job_queued_us_bucket`, not `job_queued_duration_us_bucket`). Alerts
belong with the metrics they watch, and this is where the dependency lives.

### Delivery

Provisioned YAML, version-controlled — matching the existing datasource /
dashboard provisioning pattern. No docker-compose change: the Grafana service
already mounts `./grafana/provisioning:/etc/grafana/provisioning:ro`, and
Grafana auto-loads `provisioning/alerting/*.yaml`.

New files under `docker/telemetry/grafana/provisioning/alerting/`:

| File                 | Purpose                                                                                                                                                                                                                      |
| -------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `contactpoints.yaml` | **Two** contact points: `xrpld-default` (Slack) and `xrpld-critical` (Slack + email).                                                                                                                                        |
| `policies.yaml`      | **Nested** notification policy: root route → `xrpld-default`; child route matching `severity = critical` → `xrpld-critical` (`repeat_interval: 1h` vs the root's `4h`). Both grouped by `alertname` + `service_instance_id`. |
| `rules.yaml`         | **13** alert rules across **5** groups (below).                                                                                                                                                                              |

Plus the Alerting section of `docs/telemetry-runbook.md` — operator runbook:
what each alert means, likely causes, and how to point the contact point at a
real receiver.

### Alert rules

All rules target Prometheus datasource `uid: prometheus`. Each rule uses the
Grafana rule shape: query (A) → reduce (B, last value) → threshold (C). All
`rate()`/`histogram_quantile()` expressions aggregate with
`sum by (service_instance_id)` (or `+ le`) so **each node alerts independently**.
Alert rules run headless, so they cannot use the dashboards' `$node` template
variables — they match all series and group by `service_instance_id` instead.

All 5 groups evaluate at `interval: 1m`. Metric names carry **no** `xrpld_`
prefix — `OTelCollectorImp::formatName()` adds none.

The **Threshold** column is the rule's refId `C` evaluator, read straight from
`rules.yaml` — it is the firing condition, so it is load-bearing, not decoration.

| Group              | Alert                   | Expression (refId A)                                                                                      | Threshold (refId C)                               | `for` | severity |
| ------------------ | ----------------------- | --------------------------------------------------------------------------------------------------------- | ------------------------------------------------- | ----- | -------- |
| `xrpld-consensus`  | LedgerHistoryMismatch   | `sum by (service_instance_id) (increase(ledger_history_mismatch_total[15m]))`                             | `gt [0]`                                          | 2m    | critical |
| `xrpld-consensus`  | LedgerCloseStalled      | `rate(ledgers_closed_total)` decayed to ≈0                                                                | `lt [0.001]`                                      | 3m    | critical |
| `xrpld-consensus`  | ValidatedLedgerStale    | `max by (service_instance_id) (ledgermaster_validated_ledger_age < 1209600)`                              | `gt [60]` (seconds)                               | 5m    | critical |
| `xrpld-validator`  | ValidationsMissed       | miss **ratio**, gated on send activity — see the expression below the table                               | `gt [0.1]`                                        | 15m   | warning  |
| `xrpld-validator`  | ValidationsNotChecked   | `rate(validations_checked_total)` ≈0                                                                      | `lt [0.001]`                                      | 5m    | warning  |
| `xrpld-jobqueue`   | JobQueueTxOverflow      | `sum by (service_instance_id) (increase(jq_trans_overflow_total[15m]))`                                   | `gt [0]`                                          | 2m    | warning  |
| `xrpld-jobqueue`   | JobQueueLatencyHigh     | `histogram_quantile(0.99, sum by (le, service_instance_id) (rate(job_queued_us_bucket[5m])))`             | `gt [1000000]` (µs = 1s)                          | 5m    | warning  |
| `xrpld-jobqueue`   | NodeStoreIOLatencyHigh  | `histogram_quantile(0.95, sum by (le, service_instance_id) (rate(ios_latency_milliseconds_bucket[10m])))` | `gt [1000]` (ms)                                  | 10m   | warning  |
| `xrpld-node-state` | NodeStateFlapping       | state-transition rate over the node-state series                                                          | `gt [3]` (transitions)                            | 15m   | warning  |
| `xrpld-node-state` | NodeNotFull             | operating mode below FULL                                                                                 | `lt [4]` (FULL = 4)                               | 15m   | warning  |
| `xrpld-overlay`    | ManifestJobQueueConvoy  | `sum by (service_instance_id) (jobq_manifest_waiting)`                                                    | `gt [3]` (waiting jobs)                           | 10m   | warning  |
| `xrpld-overlay`    | ManifestFloodInbound    | inbound manifest byte rate                                                                                | `gt [524288]` (B/s = 512 **KiB**/s, not 512 kB/s) | 10m   | warning  |
| `xrpld-overlay`    | PeerResourceDisconnects | `sum by (service_instance_id) (increase(server_info{metric="peer_disconnects_resources"}[30m]))`          | `gt [5]`                                          | 5m    | warning  |

**`ValidationsMissed` is a gated ratio, not `rate(...) > 0`.** The raw-rate shape
is the pre-fix version and it fires on **every non-validating node**:
`ValidationTracker` counts a miss whenever `weValidated && networkValidated` is
not both true, and a non-validator never sets `weValidated`, so its measured
ratio is exactly **1.0**. No threshold can separate "not a validator" from
"validator disagreeing", hence the `and on (...)` activity gate. The shipped
expression is:

- numerator: `sum by (service_instance_id) (rate(validation_missed_total[15m]))`
- denominator: `clamp_min(` that same numerator `+ sum by (service_instance_id) (rate(validation_agreements_total[15m])), 1e-9)`
- gate: `and on (service_instance_id) (sum by (service_instance_id) (rate(validations_sent_total[15m])) > 0)`
- evaluator: `gt [0.1]` — i.e. >10% disagreement among nodes that do validate

3 rules are `severity: critical`, 10 are `severity: warning`.

Each rule carries labels `severity` and `category`
and annotations `summary` + `description` (with `{{ $labels.service_instance_id }}`
and `{{ $values.B.Value }}` interpolation).

#### Threshold rationale

- **LedgerCloseStalled `< 0.001` for 3m**: healthy nodes close a ledger every
  ~3-5s; a 5m rate decaying to ~0 means the node is stuck. The epsilon (not
  exact `0`) avoids float rate-noise suppressing the alert.
- **JobQueueLatencyHigh 1s p99**: `gt [1000000]` µs = 1s. A default starting
  point, easy to tune — jobs queued >1s at p99 indicate the node is saturated.
- **ValidationsMissed `> 0.1` on a gated ratio**, not `> 0` on a raw rate: the
  raw rate is permanently nonzero (ratio 1.0) on non-validators, so a `> 0` rule
  pages on every non-validating node in the fleet. See the note above the
  rationale list.
- **ManifestFloodInbound 524288 B/s**: an earlier 50 kB/s threshold produced ~41
  sustained 5-minute samples on healthy nodes; 512 KiB/s clears normal
  manifest-exchange peaks.
- Remaining `gt [0]` rules (`LedgerHistoryMismatch`, `JobQueueTxOverflow`) sit on
  true error counters where any sustained nonzero rate is actionable.

### Non-goals / YAGNI

- No per-alert silencing schedules, no mute timings.
- No RPC/API or fee-market alerts (dashboards cover those visually). Overlay
  alerts _were_ added during implementation — the `xrpld-overlay` group carries
  three (manifest convoy, manifest flood, peer resource disconnects).
- Two contact points and a two-level policy tree shipped; deeper routing
  (Discord, PagerDuty, per-team splits) is left to the operator.

### Verification

1. `yamllint` (or `python -c yaml.safe_load`) on all three YAML files.
2. `docker compose -f docker/telemetry/docker-compose.yml config -q` still parses.
3. Optional live check: start stack, `GET /api/v1/provisioning/alert-rules`
   returns the 13 rules; Grafana logs show no provisioning errors.
4. Code-review pass (subagent) against phase conventions before commit.
