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

**Parallel work**: Tasks 7.4 and 7.5 can run in parallel after 7.2/7.3 complete. Task 7.6 depends on 7.5's findings. Tasks 7.7 and 7.8 can run in parallel after 7.6.

**Exit Criteria** (from [06-implementation-phases.md §6.8](./06-implementation-phases.md)):

- [ ] All 255+ metrics visible in Prometheus via OTLP pipeline (no StatsD receiver)
- [ ] `server=otel` is the default in development docker-compose
- [ ] `server=statsd` still works as a fallback
- [ ] Existing Grafana dashboards display data correctly
- [ ] Integration test passes with OTLP-only metrics pipeline
- [ ] No performance regression vs StatsD baseline (< 1% CPU overhead)
- [ ] Deferred Task 6.1 (`|m` wire format) no longer relevant — Meter mapped to OTel Counter
