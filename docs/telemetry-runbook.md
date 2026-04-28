# xrpld Telemetry Operator Runbook

## Overview

xrpld supports OpenTelemetry distributed tracing to provide visibility into RPC requests, transaction processing, and consensus rounds.

## Quick Start

### 1. Start the observability stack

```bash
docker compose -f docker/telemetry/docker-compose.yml up -d
```

This starts:

- **OTel Collector** on ports 4317 (gRPC) and 4318 (HTTP)
- **Jaeger** UI on http://localhost:16686
- **Prometheus** on http://localhost:9090
- **Grafana** on http://localhost:3000

### 2. Enable telemetry in xrpld

Add to your `xrpld.cfg`:

```ini
[telemetry]
enabled=1
endpoint=http://localhost:4318/v1/traces
```

### 3. Build with telemetry support

```bash
conan install . --build=missing -o telemetry=True
cmake --preset default -Dtelemetry=ON
cmake --build --preset default
```

## Configuration Reference

| Option               | Default                           | Description                               |
| -------------------- | --------------------------------- | ----------------------------------------- |
| `enabled`            | `0`                               | Master switch for telemetry               |
| `endpoint`           | `http://localhost:4318/v1/traces` | OTLP/HTTP endpoint                        |
| `exporter`           | `otlp_http`                       | Exporter type                             |
| `sampling_ratio`     | `1.0`                             | Head-based sampling ratio (0.0–1.0)       |
| `trace_rpc`          | `1`                               | Enable RPC request tracing                |
| `trace_transactions` | `1`                               | Enable transaction tracing                |
| `trace_consensus`    | `1`                               | Enable consensus tracing                  |
| `trace_peer`         | `0`                               | Enable peer message tracing (high volume) |
| `trace_ledger`       | `1`                               | Enable ledger tracing                     |
| `batch_size`         | `512`                             | Max spans per batch export                |
| `batch_delay_ms`     | `5000`                            | Delay between batch exports               |
| `max_queue_size`     | `2048`                            | Max spans queued before dropping          |
| `use_tls`            | `0`                               | Use TLS for exporter connection           |
| `tls_ca_cert`        | (empty)                           | Path to CA certificate bundle             |

## Span Reference

All spans instrumented in xrpld, grouped by subsystem:

### RPC Spans (Phase 2)

| Span Name            | Source File           | Attributes                                              | Description                                        |
| -------------------- | --------------------- | ------------------------------------------------------- | -------------------------------------------------- |
| `rpc.request`        | ServerHandler.cpp:271 | —                                                       | Top-level HTTP RPC request                         |
| `rpc.process`        | ServerHandler.cpp:573 | —                                                       | RPC processing (child of rpc.request)              |
| `rpc.ws_message`     | ServerHandler.cpp:384 | —                                                       | WebSocket RPC message                              |
| `rpc.command.<name>` | RPCHandler.cpp:161    | `xrpl.rpc.command`, `xrpl.rpc.version`, `xrpl.rpc.role` | Per-command span (e.g., `rpc.command.server_info`) |

### Transaction Spans (Phase 3)

| Span Name    | Source File         | Attributes                                      | Description                           |
| ------------ | ------------------- | ----------------------------------------------- | ------------------------------------- |
| `tx.process` | NetworkOPs.cpp:1227 | `xrpl.tx.hash`, `xrpl.tx.local`, `xrpl.tx.path` | Transaction submission and processing |
| `tx.receive` | PeerImp.cpp:1273    | `xrpl.peer.id`                                  | Transaction received from peer relay  |

### Consensus Spans (Phase 4)

| Span Name                   | Source File          | Attributes                                                                                                                    | Description                                |
| --------------------------- | -------------------- | ----------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| `consensus.proposal.send`   | RCLConsensus.cpp:177 | `xrpl.consensus.round`                                                                                                        | Consensus proposal broadcast               |
| `consensus.ledger_close`    | RCLConsensus.cpp:282 | `xrpl.consensus.ledger.seq`, `xrpl.consensus.mode`                                                                            | Ledger close event                         |
| `consensus.accept`          | RCLConsensus.cpp:395 | `xrpl.consensus.proposers`, `xrpl.consensus.round_time_ms`                                                                    | Ledger accepted by consensus               |
| `consensus.validation.send` | RCLConsensus.cpp:753 | `xrpl.consensus.ledger.seq`, `xrpl.consensus.proposing`                                                                       | Validation sent after accept               |
| `consensus.accept.apply`    | RCLConsensus.cpp:453 | `xrpl.consensus.close_time`, `close_time_correct`, `close_resolution_ms`, `state`, `proposing`, `round_time_ms`, `ledger.seq` | Ledger application with close time details |

#### Close Time Queries (Tempo TraceQL)

```
# Find rounds where validators disagreed on close time
{name="consensus.accept.apply"} | xrpl.consensus.close_time_correct = false

# Find consensus failures (moved_on)
{name="consensus.accept.apply"} | xrpl.consensus.state = "moved_on"

# Find slow ledger applications (>5s)
{name="consensus.accept.apply"} | duration > 5s

# Find specific ledger's consensus details
{name="consensus.accept.apply"} | xrpl.consensus.ledger.seq = 92345678
```

## Prometheus Metrics (Spanmetrics)

The OTel Collector's spanmetrics connector automatically derives RED (Rate, Errors, Duration) metrics from every span. No custom metrics code is needed in xrpld.

### Generated Metric Names

| Prometheus Metric                                  | Type      | Description                  |
| -------------------------------------------------- | --------- | ---------------------------- |
| `traces_span_metrics_calls_total`                  | Counter   | Total span invocations       |
| `traces_span_metrics_duration_milliseconds_bucket` | Histogram | Latency distribution buckets |
| `traces_span_metrics_duration_milliseconds_count`  | Histogram | Latency observation count    |
| `traces_span_metrics_duration_milliseconds_sum`    | Histogram | Cumulative latency           |

### Metric Labels

Every metric carries these standard labels:

| Label          | Source             | Example                                  |
| -------------- | ------------------ | ---------------------------------------- |
| `span_name`    | Span name          | `rpc.command.server_info`                |
| `status_code`  | Span status        | `STATUS_CODE_UNSET`, `STATUS_CODE_ERROR` |
| `service_name` | Resource attribute | `xrpld`                                  |
| `span_kind`    | Span kind          | `SPAN_KIND_INTERNAL`                     |

Additionally, span attributes configured as dimensions in the collector become metric labels (dots → underscores):

| Span Attribute        | Metric Label          | Applies To                     |
| --------------------- | --------------------- | ------------------------------ |
| `xrpl.rpc.command`    | `xrpl_rpc_command`    | `rpc.command.*` spans          |
| `xrpl.rpc.status`     | `xrpl_rpc_status`     | `rpc.command.*` spans          |
| `xrpl.consensus.mode` | `xrpl_consensus_mode` | `consensus.ledger_close` spans |
| `xrpl.tx.local`       | `xrpl_tx_local`       | `tx.process` spans             |

### Histogram Buckets

Configured in `otel-collector-config.yaml`:

```
1ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1s, 5s
```

## Grafana Dashboards

Three dashboards are pre-provisioned in `docker/telemetry/grafana/dashboards/`:

### RPC Performance (`xrpld-rpc-perf`)

| Panel                       | Type       | PromQL                                                                                                                                             | Labels Used                       |
| --------------------------- | ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------- |
| RPC Request Rate by Command | timeseries | `sum by (xrpl_rpc_command) (rate(traces_span_metrics_calls_total{span_name=~"rpc.command.*"}[5m]))`                                                | `xrpl_rpc_command`                |
| RPC Latency p95 by Command  | timeseries | `histogram_quantile(0.95, sum by (le, xrpl_rpc_command) (rate(traces_span_metrics_duration_milliseconds_bucket{span_name=~"rpc.command.*"}[5m])))` | `xrpl_rpc_command`                |
| RPC Error Rate              | bargauge   | Error spans / total spans × 100, grouped by `xrpl_rpc_command`                                                                                     | `xrpl_rpc_command`, `status_code` |
| RPC Latency Heatmap         | heatmap    | `sum(increase(traces_span_metrics_duration_milliseconds_bucket{span_name=~"rpc.command.*"}[5m])) by (le)`                                          | `le` (bucket boundaries)          |

### Transaction Overview (`xrpld-transactions`)

| Panel                             | Type       | PromQL                                                                                       | Labels Used     |
| --------------------------------- | ---------- | -------------------------------------------------------------------------------------------- | --------------- |
| Transaction Processing Rate       | timeseries | `rate(traces_span_metrics_calls_total{span_name="tx.process"}[5m])` and `tx.receive`         | `span_name`     |
| Transaction Processing Latency    | timeseries | `histogram_quantile(0.95 / 0.50, ... {span_name="tx.process"})`                              | —               |
| Transaction Path Distribution     | piechart   | `sum by (xrpl_tx_local) (rate(traces_span_metrics_calls_total{span_name="tx.process"}[5m]))` | `xrpl_tx_local` |
| Transaction Receive vs Suppressed | timeseries | `rate(traces_span_metrics_calls_total{span_name="tx.receive"}[5m])`                          | —               |

### Consensus Health (`xrpld-consensus`)

| Panel                         | Type       | PromQL                                                                             | Labels Used |
| ----------------------------- | ---------- | ---------------------------------------------------------------------------------- | ----------- |
| Consensus Round Duration      | timeseries | `histogram_quantile(0.95 / 0.50, ... {span_name="consensus.accept"})`              | —           |
| Consensus Proposals Sent Rate | timeseries | `rate(traces_span_metrics_calls_total{span_name="consensus.proposal.send"}[5m])`   | —           |
| Ledger Close Duration         | timeseries | `histogram_quantile(0.95, ... {span_name="consensus.ledger_close"})`               | —           |
| Validation Send Rate          | stat       | `rate(traces_span_metrics_calls_total{span_name="consensus.validation.send"}[5m])` | —           |
| Ledger Apply Duration         | timeseries | `histogram_quantile(0.95 / 0.50, ... {span_name="consensus.accept.apply"})`        | —           |
| Close Time Agreement          | timeseries | `rate(traces_span_metrics_calls_total{span_name="consensus.accept.apply"}[5m])`    | —           |

### Span → Metric → Dashboard Summary

| Span Name                   | Prometheus Metric Filter                  | Grafana Dashboard                             |
| --------------------------- | ----------------------------------------- | --------------------------------------------- |
| `rpc.request`               | `{span_name="rpc.request"}`               | — (available but not paneled)                 |
| `rpc.process`               | `{span_name="rpc.process"}`               | — (available but not paneled)                 |
| `rpc.command.*`             | `{span_name=~"rpc.command.*"}`            | RPC Performance (all 4 panels)                |
| `tx.process`                | `{span_name="tx.process"}`                | Transaction Overview (3 panels)               |
| `tx.receive`                | `{span_name="tx.receive"}`                | Transaction Overview (2 panels)               |
| `consensus.accept`          | `{span_name="consensus.accept"}`          | Consensus Health (Round Duration)             |
| `consensus.proposal.send`   | `{span_name="consensus.proposal.send"}`   | Consensus Health (Proposals Rate)             |
| `consensus.ledger_close`    | `{span_name="consensus.ledger_close"}`    | Consensus Health (Close Duration)             |
| `consensus.validation.send` | `{span_name="consensus.validation.send"}` | Consensus Health (Validation Rate)            |
| `consensus.accept.apply`    | `{span_name="consensus.accept.apply"}`    | Consensus Health (Apply Duration, Close Time) |

## Troubleshooting

### No traces appearing in Jaeger

1. Check xrpld logs for `Telemetry starting` message
2. Verify `enabled=1` in the `[telemetry]` config section
3. Test collector connectivity: `curl -v http://localhost:4318/v1/traces`
4. Check collector logs: `docker compose logs otel-collector`

### High memory usage

- Reduce `sampling_ratio` (e.g., `0.1` for 10% sampling)
- Reduce `max_queue_size` and `batch_size`
- Disable high-volume trace categories: `trace_peer=0`

### Collector connection failures

- Verify endpoint URL matches collector address
- Check firewall rules for ports 4317/4318
- If using TLS, verify certificate path with `tls_ca_cert`

## Performance Tuning

| Scenario                 | Recommendation                                    |
| ------------------------ | ------------------------------------------------- |
| Production mainnet       | `sampling_ratio=0.01`, `trace_peer=0`             |
| Testnet/devnet           | `sampling_ratio=1.0` (full tracing)               |
| Debugging specific issue | `sampling_ratio=1.0` temporarily                  |
| High-throughput node     | Increase `batch_size=1024`, `max_queue_size=4096` |

## Disabling Telemetry

Set `enabled=0` in config (runtime disable) or build without the flag:

```bash
cmake --preset default -Dtelemetry=OFF
```

When telemetry is compiled out, all trace macros expand to no-ops with zero overhead.
