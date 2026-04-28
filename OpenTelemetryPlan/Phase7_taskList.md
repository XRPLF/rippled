# Phase 7: Native OTel Metrics Migration — Task List

> **Goal**: Replace `StatsDCollector` with a native OpenTelemetry Metrics SDK implementation behind the existing `beast::insight::Collector` interface, eliminating the StatsD UDP dependency.
>
> **Scope**: New `OTelCollectorImpl` class, `CollectorManager` config change, OTel Collector pipeline update, Grafana dashboard metric name migration, integration tests.
>
> **Branch**: `pratik/otel-phase7-native-metrics` (from `pratik/otel-phase6-statsd`)

### Related Plan Documents

| Document                                                             | Relevance                                                       |
| -------------------------------------------------------------------- | --------------------------------------------------------------- |
| [06-implementation-phases.md](./06-implementation-phases.md)         | Phase 7 plan: motivation, architecture, exit criteria (§6.8)    |
| [02-design-decisions.md](./02-design-decisions.md)                   | Collector interface design, beast::insight coexistence strategy |
| [05-configuration-reference.md](./05-configuration-reference.md)     | `[insight]` and `[telemetry]` config sections                   |
| [09-data-collection-reference.md](./09-data-collection-reference.md) | Complete metric inventory that must be preserved                |

---

## Task 7.1: Add OTel Metrics SDK to Build Dependencies

**Objective**: Enable the OTel C++ Metrics SDK components in the build system.

**What to do**:

- Edit `conanfile.py`:
  - Add OTel metrics SDK components to the dependency list when `telemetry=True`
  - Components needed: `opentelemetry-cpp::metrics`, `opentelemetry-cpp::otlp_http_metric_exporter`

- Edit `CMakeLists.txt` (telemetry section):
  - Link `opentelemetry::metrics` and `opentelemetry::otlp_http_metric_exporter` targets

**Key modified files**:

- `conanfile.py`
- `CMakeLists.txt` (or the relevant telemetry cmake target)

**Reference**: [05-configuration-reference.md §5.3](./05-configuration-reference.md) — CMake integration

---

## Task 7.2: Implement OTelCollector Class

**Objective**: Create the core `OTelCollector` implementation that maps beast::insight instruments to OTel Metrics SDK instruments.

**What to do**:

- Create `include/xrpl/beast/insight/OTelCollector.h`:
  - Public factory: `static std::shared_ptr<OTelCollector> New(std::string const& endpoint, std::string const& prefix, beast::Journal journal)`
  - Derives from `StatsDCollector` (or directly from `Collector` — TBD based on shared code)

- Create `src/libxrpl/beast/insight/OTelCollector.cpp` (~400-500 lines):
  - **OTelCounterImpl**: Wraps `opentelemetry::metrics::Counter<int64_t>`. `increment(amount)` calls `counter->Add(amount)`.
  - **OTelGaugeImpl**: Uses `opentelemetry::metrics::ObservableGauge<uint64_t>` with an async callback. `set(value)` stores value atomically; callback reads it during collection.
  - **OTelMeterImpl**: Wraps `opentelemetry::metrics::Counter<uint64_t>`. `increment(amount)` calls `counter->Add(amount)`. Semantically identical to Counter but unsigned.
  - **OTelEventImpl**: Wraps `opentelemetry::metrics::Histogram<double>`. `notify(duration)` calls `histogram->Record(duration.count())`. Uses explicit bucket boundaries matching SpanMetrics: [1, 5, 10, 25, 50, 100, 250, 500, 1000, 5000] ms.
  - **OTelHookImpl**: Stores handler function. Called during periodic metric collection (same 1s pattern via PeriodicMetricReader).
  - **OTelCollectorImp**: Main class.
    - Creates `MeterProvider` with `PeriodicMetricReader` (1s export interval)
    - Creates `OtlpHttpMetricExporter` pointing to `[telemetry]` endpoint
    - Sets resource attributes (service.name, service.instance.id) matching trace exporter
    - Implements all `make_*()` factory methods
    - Prefixes metric names with `[insight] prefix=` value

- Guard all OTel SDK includes with `#ifdef XRPL_ENABLE_TELEMETRY` to compile to `NullCollector` equivalents when telemetry disabled.

**Key new files**:

- `include/xrpl/beast/insight/OTelCollector.h`
- `src/libxrpl/beast/insight/OTelCollector.cpp`

**Key patterns to follow**:

- Match `StatsDCollector.cpp` structure: private impl classes, intrusive list for metrics, strand-based thread safety
- Match existing telemetry code style from `src/libxrpl/telemetry/Telemetry.cpp`
- Use RAII for MeterProvider lifecycle (shutdown on destructor)

**Reference**: [04-code-samples.md](./04-code-samples.md) — code style and patterns

---

## Task 7.3: Update CollectorManager

**Objective**: Add `server=otel` config option to route metric creation to the new OTel backend.

**What to do**:

- Edit `src/xrpld/app/main/CollectorManager.cpp`:
  - In the constructor, add a third branch after `server == "statsd"`:
    ```cpp
    else if (server == "otel")
    {
        // Read endpoint from [telemetry] section
        auto const endpoint = get(telemetryParams, "endpoint",
            "http://localhost:4318/v1/metrics");
        std::string const& prefix(get(params, "prefix"));
        m_collector = beast::insight::OTelCollector::New(
            endpoint, prefix, journal);
    }
    ```
  - This requires access to the `[telemetry]` config section — may need to pass it as a parameter or read from Application config.

- Edit `src/xrpld/app/main/CollectorManager.h`:
  - Add `#include <xrpl/beast/insight/OTelCollector.h>`

**Key modified files**:

- `src/xrpld/app/main/CollectorManager.cpp`
- `src/xrpld/app/main/CollectorManager.h`

---

## Task 7.4: Update OTel Collector Configuration

**Objective**: Add a metrics pipeline to the OTLP receiver and remove the StatsD receiver dependency.

**What to do**:

- Edit `docker/telemetry/otel-collector-config.yaml`:
  - Remove `statsd` receiver (no longer needed when `server=otel`)
  - Add metrics pipeline under `service.pipelines`:
    ```yaml
    metrics:
      receivers: [otlp, spanmetrics]
      processors: [batch]
      exporters: [prometheus]
    ```
  - The OTLP receiver already listens on :4318 — it just needs to be added to the metrics pipeline receivers.
  - Keep `spanmetrics` connector in the metrics pipeline so span-derived RED metrics continue working.

- Edit `docker/telemetry/docker-compose.yml`:
  - Remove UDP :8125 port mapping from otel-collector service
  - Update rippled service config: change `[insight] server=statsd` to `server=otel`

**Key modified files**:

- `docker/telemetry/otel-collector-config.yaml`
- `docker/telemetry/docker-compose.yml`

**Note**: Keep a commented-out `statsd` receiver block for operators who need backward compatibility.

---

## Task 7.5: Preserve Metric Names in Prometheus

**Objective**: Ensure existing Grafana dashboards continue working with identical metric names.

**What to do**:

- In `OTelCollector.cpp`, construct OTel instrument names to match existing Prometheus metric names:
  - beast::insight `make_gauge("LedgerMaster", "Validated_Ledger_Age")` → OTel instrument name: `rippled_LedgerMaster_Validated_Ledger_Age`
  - The prefix + group + name concatenation must produce the same string as `StatsDCollector`'s format
  - Use underscores as separators (matching StatsD convention)

- Verify in integration test that key Prometheus queries still return data:
  - `rippled_LedgerMaster_Validated_Ledger_Age`
  - `rippled_Peer_Finder_Active_Inbound_Peers`
  - `rippled_rpc_requests`

**Key consideration**: OTel Prometheus exporter may normalize metric names differently than StatsD receiver. Test this early (Task 7.2) and adjust naming strategy if needed. The OTel SDK's Prometheus exporter adds `_total` suffix to counters and converts dots to underscores — match existing conventions.

---

## Task 7.6: Update Grafana Dashboards

**Objective**: Update the 3 StatsD dashboards if any metric names change due to OTLP export format differences.

**What to do**:

- If Task 7.5 confirms metric names are preserved exactly, no dashboard changes needed.
- If OTLP export produces different names (e.g., `_total` suffix on counters), update:
  - `docker/telemetry/grafana/dashboards/statsd-node-health.json`
  - `docker/telemetry/grafana/dashboards/statsd-network-traffic.json`
  - `docker/telemetry/grafana/dashboards/statsd-rpc-pathfinding.json`
- Rename dashboard titles from "StatsD" to "System Metrics" or similar (since they're no longer StatsD-sourced).

**Key modified files**:

- `docker/telemetry/grafana/dashboards/statsd-*.json` (3 files, conditionally)

---

## Task 7.7: Update Integration Tests

**Objective**: Verify the full OTLP metrics pipeline end-to-end.

**What to do**:

- Edit `docker/telemetry/integration-test.sh`:
  - Update test config to use `[insight] server=otel`
  - Verify metrics arrive in Prometheus via OTLP (not StatsD)
  - Add check that StatsD receiver is no longer required
  - Preserve all existing metric presence checks

**Key modified files**:

- `docker/telemetry/integration-test.sh`

---

## Task 7.8: Update Documentation

**Objective**: Update all plan docs, runbook, and reference docs to reflect the migration.

**What to do**:

- Edit `docs/telemetry-runbook.md`:
  - Update `[insight]` config examples to show `server=otel`
  - Update troubleshooting section (no more StatsD UDP debugging)

- Edit `OpenTelemetryPlan/09-data-collection-reference.md`:
  - Update Data Flow Overview diagram (remove StatsD receiver)
  - Update Section 2 header from "StatsD Metrics" to "System Metrics (OTel native)"
  - Update config examples

- Edit `OpenTelemetryPlan/05-configuration-reference.md`:
  - Add `server=otel` option to `[insight]` section docs

- Edit `docker/telemetry/TESTING.md`:
  - Update setup instructions to use `server=otel`

**Key modified files**:

- `docs/telemetry-runbook.md`
- `OpenTelemetryPlan/09-data-collection-reference.md`
- `OpenTelemetryPlan/05-configuration-reference.md`
- `docker/telemetry/TESTING.md`

---

## Task 7.9: ValidationTracker — Validation Agreement Computation

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md) — the most valuable metric from the community [xrpl-validator-dashboard](https://github.com/realgrapedrop/xrpl-validator-dashboard).
>
> **Upstream**: Phase 4 Task 4.8 (validation span attributes provide ledger hash context).
> **Downstream**: Phase 9 (Validator Health dashboard), Phase 10 (validation checks), Phase 11 (agreement alert rules).

**Objective**: Implement a stateful class that tracks whether our validator's validations agree with network consensus, maintaining rolling 1h and 24h windows with an 8-second grace period and 5-minute late repair window.

**Architecture**:

```
consensus.validation.send ────> ValidationTracker ────> MetricsRegistry
(records our validation         (reconciles after       (exports agreement
 for ledger X)                   8s grace period)        gauges every 10s)

ledger.validate ──────────────> ValidationTracker
(records which ledger           (marks ledger X as
 network validated)              agreed or missed)
```

**What to do**:

- Create `src/xrpld/telemetry/ValidationTracker.h`:
  - `recordOurValidation(ledgerHash, ledgerSeq)` — called when we send a validation
  - `recordNetworkValidation(ledgerHash, seq)` — called when a ledger is fully validated
  - `reconcile()` — called periodically; reconciles pending ledger events after 8s grace period
  - Getters: `agreementPct1h()`, `agreementPct24h()`, `agreements1h()`, `missed1h()`, `agreements24h()`, `missed24h()`, `totalAgreements()`, `totalMissed()`, `totalValidationsSent()`, `totalValidationsChecked()`
  - Thread-safety: atomics for counters, mutex for window deques

- Create `src/xrpld/telemetry/detail/ValidationTracker.cpp`:
  - Reconciliation logic: after 8s grace period, check if `weValidated && networkValidated && sameHash` → agreement; else missed
  - Late repair: if a late validation arrives within 5 minutes, correct a false-positive miss
  - Sliding window: `std::deque<WindowEvent>` evicts entries older than 1h/24h on each reconciliation pass
  - Ring buffer of 1000 `LedgerEvent` structs for pending reconciliation

- Add recording hooks (modifying Phase 4 code from Phase 7 branch):
  - `RCLConsensus.cpp` `validate()`: call `tracker.recordOurValidation()`
  - `LedgerMaster.cpp` fully-validated path: call `tracker.recordNetworkValidation()`

**Key data structures**:

```cpp
struct LedgerEvent {
    uint256 ledgerHash;
    LedgerIndex seq;
    TimePoint closeTime;
    bool weValidated = false;
    bool networkValidated = false;
    bool reconciled = false;
    bool agreed = false;
};

struct WindowEvent {
    TimePoint time;
    bool agreed;
};
```

**Key new files**:

- `src/xrpld/telemetry/ValidationTracker.h`
- `src/xrpld/telemetry/detail/ValidationTracker.cpp`

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.h` (add ValidationTracker member)
- `src/xrpld/telemetry/MetricsRegistry.cpp` (add gauge callback reading from tracker)
- `src/xrpld/app/consensus/RCLConsensus.cpp` (add recording hooks)
- `src/xrpld/app/ledger/detail/LedgerMaster.cpp` (add recording hook)

**Exit Criteria**:

- [ ] ValidationTracker correctly tracks agreement with 8s grace period
- [ ] 5-minute late repair corrects false-positive misses
- [ ] Thread-safe (atomics + mutex for window deques)
- [ ] Rolling windows correctly evict stale entries
- [ ] Unit tests: normal agreement, missed validation, late repair, window eviction

---

## Task 7.10: Validator Health Observable Gauges

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md)

**Objective**: Export amendment blocked, UNL health, and quorum data as a native OTel observable gauge.

**What to do**:

- In `MetricsRegistry.cpp` `registerAsyncGauges()`, add:

```cpp
validatorHealthGauge_ = meter_->CreateDoubleObservableGauge(
    "rippled_validator_health", "Validator health indicators");
```

**Gauge label values**:

| Label `metric=`     | Type   | Source                                            |
| ------------------- | ------ | ------------------------------------------------- |
| `amendment_blocked` | int64  | `app_.getOPs().isAmendmentBlocked()` → 0/1        |
| `unl_blocked`       | int64  | `app_.getOPs().isUNLBlocked()` → 0/1              |
| `unl_expiry_days`   | double | `app_.validators().expires()` → days until expiry |
| `validation_quorum` | int64  | `app_.validators().quorum()`                      |

### Sub-task 7.10a: Per-Validator Validation Count (Flag Ledger Window)

**Objective**: Track how many ledgers each UNL validator has validated over
the last 256 consecutive ledgers (one flag ledger window). This is the key
UNL participation metric — validators consistently below threshold may be
candidates for removal from the UNL.

**What to do**:

- Add a new observable gauge:

```cpp
validatorParticipationGauge_ = meter_->CreateInt64ObservableGauge(
    "rippled_validator_participation",
    "Per-validator validation count over the last 256 ledgers");
```

- The callback queries `app_.getValidations()` to get the trusted
  validation set for each of the last 256 ledger hashes (from
  `LedgerMaster::getValidatedLedger()` walking backwards). For each
  validator public key in the UNL, count how many of those 256 ledgers
  have a matching validation.

- **Label dimensions**:
  - `validator` — base58-encoded validator master public key
  - `exported_instance` — this node's identity (standard)

- **Emission**: every flag ledger (256 ledgers, ~15 minutes) or on a
  10-second async gauge callback with cached results (recompute only
  at flag ledger boundaries).

- **Data source**: `RCLValidations::getTrustedForLedger(hash, seq)` returns
  `std::vector<std::shared_ptr<STValidation>>` with `getSignerPublic()`
  for each. The UNL list is from `app_.getValidators().getTrustedMasterKeys()`.

- **Dashboard panel**: Add a table panel to the Validator Health dashboard
  showing `rippled_validator_participation` grouped by `validator` label,
  with a threshold color (green >= 240, yellow >= 200, red < 200).

**Key modified files**: `src/xrpld/telemetry/MetricsRegistry.h/.cpp`

**Exit Criteria**:

- [ ] Gauge emits one time series per UNL validator
- [ ] Values range 0-256 and update at flag ledger boundaries
- [ ] Grafana table panel shows per-validator participation
- [ ] Validators below 75% participation are highlighted in red

---

**Key modified files**: `src/xrpld/telemetry/MetricsRegistry.h/.cpp`

**Exit Criteria**:

- [ ] All 4 base label values emitted every 10s
- [ ] `unl_expiry_days` is negative when expired, positive when active
- [ ] Per-validator participation gauge emits at flag ledger boundaries
- [ ] Values visible in Prometheus

---

## Task 7.11: Peer Quality Observable Gauges

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md)

**Objective**: Export peer health aggregates (latency P90, insane peers, version awareness) as a native OTel observable gauge.

**What to do**:

- In `MetricsRegistry.cpp` `registerAsyncGauges()`, add a callback that iterates `app_.overlay().foreach(...)` to:
  - Collect per-peer latency values, sort, compute P90
  - Count peers with `tracking_ == diverged` (insane)
  - Compare peer `getVersion()` to own version for upgrade awareness

**Gauge label values**:

| Label `metric=`            | Type   | Source                                |
| -------------------------- | ------ | ------------------------------------- |
| `peer_latency_p90_ms`      | double | P90 from sorted peer latencies        |
| `peers_insane_count`       | int64  | Peers with diverged tracking status   |
| `peers_higher_version_pct` | double | % of peers on newer rippled version   |
| `upgrade_recommended`      | int64  | 1 if `peers_higher_version_pct > 60%` |

**Implementation note**: The callback runs every 10s on the metrics reader thread. Iterating ~50-200 peers is acceptable overhead.

**Key modified files**: `src/xrpld/telemetry/MetricsRegistry.h/.cpp`

**Exit Criteria**:

- [ ] P90 latency computed correctly
- [ ] Insane count matches `peers` RPC output
- [ ] Version comparison handles format variations (e.g., "rippled-2.4.0-rc1")

---

## Task 7.12: Ledger Economy Observable Gauges

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md)

**Objective**: Export fee, reserve, ledger age, and transaction rate as a native OTel observable gauge.

**Gauge label values**:

| Label `metric=`      | Type   | Source                                              |
| -------------------- | ------ | --------------------------------------------------- |
| `base_fee_xrp`       | double | Base fee from validated ledger fee settings (drops) |
| `reserve_base_xrp`   | double | Account reserve from validated ledger (drops)       |
| `reserve_inc_xrp`    | double | Owner reserve increment (drops)                     |
| `ledger_age_seconds` | double | `now - lastValidatedCloseTime`                      |
| `transaction_rate`   | double | Derived: tx count delta / time delta (smoothed)     |

**Key modified files**: `src/xrpld/telemetry/MetricsRegistry.h/.cpp`

**Exit Criteria**:

- [ ] Fee values match `server_info` RPC output
- [ ] `ledger_age_seconds` increases monotonically between ledger closes
- [ ] `transaction_rate` is smoothed (rolling average)

---

## Task 7.13: State Tracking Observable Gauges

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md)

**Objective**: Export extended state value (0-6 encoding combining OperatingMode + ConsensusMode) and time-in-current-state.

**Gauge label values**:

| Label `metric=`                 | Type   | Source                                          |
| ------------------------------- | ------ | ----------------------------------------------- |
| `state_value`                   | int64  | 0-6 encoding (see spec for mapping)             |
| `time_in_current_state_seconds` | double | `now - lastModeChangeTime` from StateAccounting |

**State value encoding**: 0=disconnected, 1=connected, 2=syncing, 3=tracking, 4=full, 5=validating (full + validating), 6=proposing (full + proposing).

**Key modified files**: `src/xrpld/telemetry/MetricsRegistry.h/.cpp`

**Exit Criteria**:

- [ ] `state_value` correctly combines OperatingMode and ConsensusMode
- [ ] `time_in_current_state_seconds` resets on mode change

---

## Task 7.14: Storage Detail and Sync Info Gauges

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md)

**Objective**: Export NuDB-specific storage size and initial sync duration.

**Gauge label values**:

| Gauge Name               | Label `metric=`                 | Type   | Source                        |
| ------------------------ | ------------------------------- | ------ | ----------------------------- |
| `rippled_storage_detail` | `nudb_bytes`                    | int64  | NuDB backend file size        |
| `rippled_sync_info`      | `initial_sync_duration_seconds` | double | Time from start to first FULL |

**Key modified files**: `src/xrpld/telemetry/MetricsRegistry.h/.cpp`

**Exit Criteria**:

- [ ] NuDB file size reported in bytes (0 if NuDB not configured)
- [ ] Sync duration captured once and remains stable after reaching FULL

---

## Task 7.15: New Synchronous Counters

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md)

**Objective**: Add 7 new event counters incremented at their respective instrumentation sites.

| Counter Name                          | Increment Site                   | Source File           |
| ------------------------------------- | -------------------------------- | --------------------- |
| `rippled_ledgers_closed_total`        | `onAccept()` in consensus        | RCLConsensus.cpp      |
| `rippled_validations_sent_total`      | `validate()` in consensus        | RCLConsensus.cpp      |
| `rippled_validations_checked_total`   | Network validation received      | LedgerMaster.cpp      |
| `rippled_validation_agreements_total` | ValidationTracker reconciliation | ValidationTracker.cpp |
| `rippled_validation_missed_total`     | ValidationTracker reconciliation | ValidationTracker.cpp |
| `rippled_state_changes_total`         | `setMode()` in NetworkOPs        | NetworkOPs.cpp        |
| `rippled_jq_trans_overflow_total`     | Job queue overflow path          | JobQueue.cpp          |

**Key modified files**: `src/xrpld/telemetry/MetricsRegistry.h/.cpp` (declarations), plus recording sites in RCLConsensus.cpp, LedgerMaster.cpp, NetworkOPs.cpp, JobQueue.cpp

**Exit Criteria**:

- [ ] All 7 counters monotonically increase during normal operation
- [ ] Counter values match expected rates (e.g., ledgers_closed ≈ 1 per 3-5s)

---

## Task 7.16: Validation Agreement Observable Gauge

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md)

**Objective**: Export rolling window agreement stats from `ValidationTracker` (Task 7.9).

**Gauge label values**:

| Gauge Name                     | Label `metric=`     | Type   | Source                      |
| ------------------------------ | ------------------- | ------ | --------------------------- |
| `rippled_validation_agreement` | `agreement_pct_1h`  | double | `tracker.agreementPct1h()`  |
|                                | `agreements_1h`     | int64  | `tracker.agreements1h()`    |
|                                | `missed_1h`         | int64  | `tracker.missed1h()`        |
|                                | `agreement_pct_24h` | double | `tracker.agreementPct24h()` |
|                                | `agreements_24h`    | int64  | `tracker.agreements24h()`   |
|                                | `missed_24h`        | int64  | `tracker.missed24h()`       |

**Key modified files**: `src/xrpld/telemetry/MetricsRegistry.cpp`

**Exit Criteria**:

- [ ] Agreement percentages in range [0.0, 100.0]
- [ ] Window stats stabilize after 1h/24h of operation

---

## Summary Table

| Task | Description                            | New Files | Modified Files | Depends On |
| ---- | -------------------------------------- | --------- | -------------- | ---------- |
| 7.1  | Add OTel Metrics SDK to build deps     | 0         | 2              | —          |
| 7.2  | Implement OTelCollector class          | 2         | 0              | 7.1        |
| 7.3  | Update CollectorManager config routing | 0         | 2              | 7.2        |
| 7.4  | Update OTel Collector YAML and Docker  | 0         | 2              | 7.3        |
| 7.5  | Preserve metric names in Prometheus    | 0         | 1              | 7.2        |
| 7.6  | Update Grafana dashboards (if needed)  | 0         | 3              | 7.5        |
| 7.7  | Update integration tests               | 0         | 1              | 7.4        |
| 7.8  | Update documentation                   | 0         | 4              | 7.6        |
| 7.9  | ValidationTracker (agreement tracking) | 2         | 4              | 7.2, P4.8  |
| 7.10 | Validator health observable gauges     | 0         | 2              | 7.2        |
| 7.11 | Peer quality observable gauges         | 0         | 2              | 7.2        |
| 7.12 | Ledger economy observable gauges       | 0         | 2              | 7.2        |
| 7.13 | State tracking observable gauges       | 0         | 2              | 7.2        |
| 7.14 | Storage detail and sync info gauges    | 0         | 2              | 7.2        |
| 7.15 | New synchronous counters               | 0         | 6              | 7.2        |
| 7.16 | Validation agreement observable gauge  | 0         | 1              | 7.9        |

**Parallel work**: Tasks 7.4 and 7.5 can run in parallel after 7.2/7.3 complete. Task 7.6 depends on 7.5's findings. Tasks 7.7 and 7.8 can run in parallel after 7.6. Tasks 7.10-7.14 can all run in parallel after 7.2. Task 7.15 depends on 7.2. Task 7.16 depends on 7.9. Task 7.9 depends on 7.2 and Phase 4 Task 4.8.

**Exit Criteria** (from [06-implementation-phases.md §6.8](./06-implementation-phases.md)):

- [ ] All 255+ metrics visible in Prometheus via OTLP pipeline (no StatsD receiver)
- [ ] `server=otel` is the default in development docker-compose
- [ ] `server=statsd` still works as a fallback
- [ ] Existing Grafana dashboards display data correctly
- [ ] Integration test passes with OTLP-only metrics pipeline
- [ ] No performance regression vs StatsD baseline (< 1% CPU overhead)
- [ ] Deferred Task 6.1 (`|m` wire format) no longer relevant — Meter mapped to OTel Counter
- [ ] ValidationTracker agreement % stabilizes after 1h under normal consensus
- [ ] All new gauges and counters visible in Prometheus with non-zero values
