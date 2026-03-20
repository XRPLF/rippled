# Phase 5: Documentation & Deployment Task List

> **Goal**: Production readiness — Grafana dashboards, spanmetrics pipeline, operator runbook, alert definitions, and final integration testing. This phase ensures the telemetry system is useful and maintainable in production.
>
> **Scope**: Grafana dashboard definitions, OTel Collector spanmetrics connector, Prometheus integration, alert rules, operator documentation, and production-ready Docker Compose stack.
>
> **Branch**: `pratik/otel-phase5-docs-deployment` (from `pratik/otel-phase4-consensus-tracing`)

### Related Plan Documents

| Document                                                         | Relevance                                                                  |
| ---------------------------------------------------------------- | -------------------------------------------------------------------------- |
| [07-observability-backends.md](./07-observability-backends.md)   | Jaeger setup (§7.1), Grafana dashboards (§7.6), alerts (§7.6.3)            |
| [05-configuration-reference.md](./05-configuration-reference.md) | Collector config (§5.5), production config (§5.5.2), Docker Compose (§5.6) |
| [06-implementation-phases.md](./06-implementation-phases.md)     | Phase 5 tasks (§6.6), definition of done (§6.11.5)                         |

---

## Task 5.1: Add Spanmetrics Connector to OTel Collector

**Objective**: Derive RED metrics (Rate, Errors, Duration) from trace spans automatically, enabling Grafana time-series dashboards.

**What to do**:

- Edit `docker/telemetry/otel-collector-config.yaml`:
  - Add `spanmetrics` connector:
    ```yaml
    connectors:
      spanmetrics:
        histogram:
          explicit:
            buckets: [1ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1s, 5s]
        dimensions:
          - name: xrpl.rpc.command
          - name: xrpl.rpc.status
          - name: xrpl.consensus.phase
          - name: xrpl.tx.type
    ```
  - Add `prometheus` exporter:
    ```yaml
    exporters:
      prometheus:
        endpoint: 0.0.0.0:8889
    ```
  - Wire the pipeline:
    ```yaml
    service:
      pipelines:
        traces:
          receivers: [otlp]
          processors: [batch]
          exporters: [debug, otlp/jaeger, spanmetrics]
        metrics:
          receivers: [spanmetrics]
          exporters: [prometheus]
    ```

- Edit `docker/telemetry/docker-compose.yml`:
  - Expose port `8889` on the collector for Prometheus scraping
  - Add Prometheus service
  - Add Prometheus as Grafana datasource

**Key modified files**:

- `docker/telemetry/otel-collector-config.yaml`
- `docker/telemetry/docker-compose.yml`

**Key new files**:

- `docker/telemetry/prometheus.yml` (Prometheus scrape config)
- `docker/telemetry/grafana/provisioning/datasources/prometheus.yaml`

**Reference**:

- [POC_taskList.md §Next Steps](./POC_taskList.md) — Metrics pipeline for Grafana dashboards

---

## Task 5.2: Create Grafana Dashboards

**Objective**: Provide pre-built Grafana dashboards for RPC performance, transaction lifecycle, and consensus health.

**What to do**:

- Create `docker/telemetry/grafana/provisioning/dashboards/dashboards.yaml` (provisioning config)
- Create dashboard JSON files:
  1. **RPC Performance Dashboard** (`rpc-performance.json`):
     - RPC request latency (p50/p95/p99) by command — histogram panel
     - RPC throughput (requests/sec) by command — time series
     - RPC error rate by command — bar gauge
     - Top slowest RPC commands — table

  2. **Transaction Overview Dashboard** (`transaction-overview.json`):
     - Transaction processing rate — time series
     - Transaction latency distribution — histogram
     - Suppression rate (duplicates) — stat panel
     - Transaction processing path (sync vs async) — pie chart

  3. **Consensus Health Dashboard** (`consensus-health.json`):
     - Consensus round duration — time series
     - Phase duration breakdown (open/establish/accept) — stacked bar
     - Proposals sent/received per round — stat panel
     - Consensus mode distribution (proposing/observing) — pie chart

- Store dashboards in `docker/telemetry/grafana/dashboards/`

**Key new files**:

- `docker/telemetry/grafana/provisioning/dashboards/dashboards.yaml`
- `docker/telemetry/grafana/dashboards/rpc-performance.json`
- `docker/telemetry/grafana/dashboards/transaction-overview.json`
- `docker/telemetry/grafana/dashboards/consensus-health.json`

**Reference**:

- [07-observability-backends.md §7.6](./07-observability-backends.md) — Grafana dashboard specifications
- [01-architecture-analysis.md §1.8.3](./01-architecture-analysis.md) — Dashboard panel examples

---

## Task 5.3: Define Alert Rules

**Objective**: Create alert definitions for key telemetry anomalies.

**What to do**:

- Create `docker/telemetry/grafana/provisioning/alerting/alerts.yaml`:
  - **RPC Latency Alert**: p99 latency > 1s for any command over 5 minutes
  - **RPC Error Rate Alert**: Error rate > 5% for any command over 5 minutes
  - **Consensus Duration Alert**: Round duration > 10s (warn), > 30s (critical)
  - **Transaction Processing Alert**: Processing rate drops below threshold
  - **Telemetry Pipeline Health**: No spans received for > 2 minutes

**Key new files**:

- `docker/telemetry/grafana/provisioning/alerting/alerts.yaml`

**Reference**:

- [07-observability-backends.md §7.6.3](./07-observability-backends.md) — Alert rule definitions

---

## Task 5.4: Production Collector Configuration

**Objective**: Create a production-ready OTel Collector configuration with tail-based sampling and resource limits.

**What to do**:

- Create `docker/telemetry/otel-collector-config-production.yaml`:
  - Tail-based sampling policy:
    - Always sample errors and slow traces
    - 10% base sampling rate for normal traces
    - Always sample first trace for each unique RPC command
  - Resource limits:
    - Memory limiter processor (80% of available memory)
    - Queued retry for export failures
  - TLS configuration for production endpoints
  - Health check endpoint

**Key new files**:

- `docker/telemetry/otel-collector-config-production.yaml`

**Reference**:

- [05-configuration-reference.md §5.5.2](./05-configuration-reference.md) — Production collector config

---

## Task 5.5: Operator Runbook

**Objective**: Create operator documentation for managing the telemetry system in production.

**What to do**:

- Create `docs/telemetry-runbook.md`:
  - **Setup**: How to enable telemetry in rippled
  - **Configuration**: All config options with descriptions
  - **Collector Deployment**: Docker Compose vs. Kubernetes vs. bare metal
  - **Troubleshooting**: Common issues and resolutions
    - No traces appearing
    - High memory usage from telemetry
    - Collector connection failures
    - Sampling configuration tuning
  - **Performance Tuning**: Batch size, queue size, sampling ratio guidelines
  - **Upgrading**: How to upgrade OTel SDK and Collector versions

**Key new files**:

- `docs/telemetry-runbook.md`

---

## Task 5.6: Final Integration Testing

**Objective**: Validate the complete telemetry stack end-to-end.

**What to do**:

1. Start full Docker stack (Collector, Jaeger, Grafana, Prometheus)
2. Build rippled with `telemetry=ON`
3. Run in standalone mode with telemetry enabled
4. Generate RPC traffic and verify traces in Jaeger
5. Verify dashboards populate in Grafana
6. Verify alerts trigger correctly
7. Test telemetry OFF path (no regressions)
8. Run full test suite

**Verification Checklist**:

- [ ] Docker stack starts without errors
- [ ] Traces appear in Jaeger with correct hierarchy
- [ ] Grafana dashboards show metrics derived from spans
- [ ] Prometheus scrapes spanmetrics successfully
- [ ] Alerts can be triggered by simulated conditions
- [ ] Build succeeds with telemetry ON and OFF
- [ ] Full test suite passes

---

## Summary

| Task | Description                        | New Files | Modified Files | Depends On |
| ---- | ---------------------------------- | --------- | -------------- | ---------- |
| 5.1  | Spanmetrics connector + Prometheus | 2         | 2              | Phase 4    |
| 5.2  | Grafana dashboards                 | 4         | 0              | 5.1        |
| 5.3  | Alert definitions                  | 1         | 0              | 5.1        |
| 5.4  | Production collector config        | 1         | 0              | Phase 4    |
| 5.5  | Operator runbook                   | 1         | 0              | Phase 4    |
| 5.6  | Final integration testing          | 0         | 0              | 5.1-5.5    |

**Parallel work**: Tasks 5.1, 5.4, and 5.5 can run in parallel. Tasks 5.2 and 5.3 depend on 5.1. Task 5.6 depends on all others.

**Exit Criteria** (from [06-implementation-phases.md §6.11.5](./06-implementation-phases.md)):

- [ ] Dashboards deployed and showing data
- [ ] Alerts configured and tested
- [ ] Operator documentation complete
- [ ] Production collector config ready
- [ ] Full test suite passes
