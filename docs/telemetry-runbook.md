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

| Option                     | Default                           | Description                                               |
| -------------------------- | --------------------------------- | --------------------------------------------------------- |
| `enabled`                  | `0`                               | Master switch for telemetry                               |
| `endpoint`                 | `http://localhost:4318/v1/traces` | OTLP/HTTP endpoint                                        |
| `service_name`             | `xrpld`                           | OpenTelemetry service name resource attribute             |
| `service_instance_id`      | node public key                   | OpenTelemetry service instance ID resource attribute      |
| `sampling_ratio`           | `1.0`                             | Head-based sampling ratio (0.0--1.0)                      |
| `trace_rpc`                | `1`                               | Enable RPC request tracing                                |
| `trace_transactions`       | `1`                               | Enable transaction tracing                                |
| `trace_consensus`          | `1`                               | Enable consensus tracing                                  |
| `trace_peer`               | `0`                               | Enable peer message tracing (high volume)                 |
| `trace_ledger`             | `1`                               | Enable ledger tracing                                     |
| `consensus_trace_strategy` | `deterministic`                   | Consensus trace ID strategy (`deterministic` or `random`) |
| `batch_size`               | `512`                             | Max spans per batch export                                |
| `batch_delay_ms`           | `5000`                            | Delay between batch exports                               |
| `max_queue_size`           | `2048`                            | Max spans queued before dropping                          |
| `use_tls`                  | `0`                               | Use TLS for exporter connection                           |
| `tls_ca_cert`              | (empty)                           | Path to CA certificate bundle                             |

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

| Span Name    | Source File         | Attributes                                                                                  | Description                           |
| ------------ | ------------------- | ------------------------------------------------------------------------------------------- | ------------------------------------- |
| `tx.process` | NetworkOPs.cpp:1227 | `xrpl.tx.hash`, `xrpl.tx.local`, `xrpl.tx.path`                                             | Transaction submission and processing |
| `tx.receive` | PeerImp.cpp:1273    | `xrpl.peer.id`, `xrpl.tx.hash`, `xrpl.peer.version`, `xrpl.tx.suppressed`, `xrpl.tx.status` | Transaction received from peer relay  |

### Transaction Queue Spans (Phase 3)

| Span Name          | Source File | Attributes                                                            | Description                                        |
| ------------------ | ----------- | --------------------------------------------------------------------- | -------------------------------------------------- |
| `txq.enqueue`      | TxQ.cpp     | `xrpl.txq.tx_hash`                                                    | Transaction enqueue decision (child of tx.process) |
| `txq.apply_direct` | TxQ.cpp     | --                                                                    | Direct apply attempt (bypassing queue)             |
| `txq.batch_clear`  | TxQ.cpp     | --                                                                    | Batch clear of queued transactions for an account  |
| `txq.accept`       | TxQ.cpp     | `xrpl.txq.queue_size`                                                 | Ledger-close accept loop over queued transactions  |
| `txq.accept_tx`    | TxQ.cpp     | `xrpl.txq.tx_hash`, `xrpl.txq.retries_remaining`, `xrpl.txq.ter_code` | Per-transaction apply during accept                |
| `txq.cleanup`      | TxQ.cpp     | `xrpl.txq.ledger_seq`                                                 | Post-close cleanup of expired queue entries        |

### Consensus Spans (Phase 4)

| Span Name                      | Source File      | Attributes                                                                                                                                                                                                                                                                                                                                                                                             | Description                                                        |
| ------------------------------ | ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------ |
| `consensus.round`              | RCLConsensus.cpp | `xrpl.consensus.ledger_id`, `xrpl.consensus.ledger.seq`, `xrpl.consensus.mode`, `xrpl.consensus.trace_strategy`, `xrpl.consensus.round_id`                                                                                                                                                                                                                                                             | Root span for a consensus round (deterministic or random trace ID) |
| `consensus.phase.open`         | Consensus.h      | --                                                                                                                                                                                                                                                                                                                                                                                                     | Open phase duration (child of round)                               |
| `consensus.proposal.send`      | RCLConsensus.cpp | `xrpl.consensus.round`                                                                                                                                                                                                                                                                                                                                                                                 | Consensus proposal broadcast                                       |
| `consensus.ledger_close`       | RCLConsensus.cpp | `xrpl.consensus.ledger.seq`, `xrpl.consensus.mode`                                                                                                                                                                                                                                                                                                                                                     | Ledger close event                                                 |
| `consensus.establish`          | Consensus.h      | `xrpl.consensus.converge_percent`, `xrpl.consensus.establish_count`, `xrpl.consensus.proposers`                                                                                                                                                                                                                                                                                                        | Establish phase duration (child of round)                          |
| `consensus.update_positions`   | Consensus.h      | `xrpl.consensus.converge_percent`, `xrpl.consensus.proposers`, `xrpl.consensus.disputes_count`                                                                                                                                                                                                                                                                                                         | Position update and dispute resolution (see Events below)          |
| `consensus.check`              | Consensus.h      | `xrpl.consensus.agree_count`, `xrpl.consensus.disagree_count`, `xrpl.consensus.converge_percent`, `xrpl.consensus.have_close_time_consensus`, `xrpl.consensus.threshold_percent`, `xrpl.consensus.result`                                                                                                                                                                                              | Consensus threshold check                                          |
| `consensus.accept`             | RCLConsensus.cpp | `xrpl.consensus.proposers`, `xrpl.consensus.round_time_ms`, `xrpl.consensus.quorum`                                                                                                                                                                                                                                                                                                                    | Ledger accepted by consensus                                       |
| `consensus.accept.apply`       | RCLConsensus.cpp | `xrpl.consensus.ledger.seq`, `xrpl.consensus.close_time`, `xrpl.consensus.close_time_correct`, `xrpl.consensus.close_resolution_ms`, `xrpl.consensus.state`, `xrpl.consensus.proposing`, `xrpl.consensus.round_time_ms`, `xrpl.consensus.parent_close_time`, `xrpl.consensus.close_time_self`, `xrpl.consensus.close_time_vote_bins`, `xrpl.consensus.resolution_direction`, `xrpl.consensus.tx_count` | Ledger application with close time details (see Events below)      |
| `consensus.validation.send`    | RCLConsensus.cpp | `xrpl.consensus.ledger.seq`, `xrpl.consensus.proposing`                                                                                                                                                                                                                                                                                                                                                | Validation sent after accept (follows-from link)                   |
| `consensus.mode_change`        | RCLConsensus.cpp | `xrpl.consensus.mode.old`, `xrpl.consensus.mode.new`                                                                                                                                                                                                                                                                                                                                                   | Consensus mode transition                                          |
| `consensus.proposal.receive`   | PeerImp.cpp      | `xrpl.consensus.trusted`, `xrpl.consensus.round`                                                                                                                                                                                                                                                                                                                                                       | Proposal received from peer (standalone span)                      |
| `consensus.validation.receive` | PeerImp.cpp      | `xrpl.consensus.trusted`, `xrpl.consensus.ledger.seq`                                                                                                                                                                                                                                                                                                                                                  | Validation received from peer (standalone span)                    |

#### Consensus Span Events

| Parent Span                  | Event Name        | Event Attributes                                                                | Description                                             |
| ---------------------------- | ----------------- | ------------------------------------------------------------------------------- | ------------------------------------------------------- |
| `consensus.update_positions` | `dispute.resolve` | `xrpl.tx.id`, `xrpl.dispute.our_vote`, `xrpl.dispute.yays`, `xrpl.dispute.nays` | Emitted per dispute when votes are tallied              |
| `consensus.accept.apply`     | `tx.included`     | `xrpl.tx.id`                                                                    | Emitted per transaction included in the accepted ledger |

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

# Find all spans in a consensus round (deterministic trace strategy)
{name="consensus.round"} | xrpl.consensus.round_id = "<round_id>"

# Find dispute resolutions
{name="consensus.update_positions"} >> {event:name="dispute.resolve"}
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

| Span Name                      | Prometheus Metric Filter                     | Grafana Dashboard                             |
| ------------------------------ | -------------------------------------------- | --------------------------------------------- |
| `rpc.request`                  | `{span_name="rpc.request"}`                  | -- (available but not paneled)                |
| `rpc.process`                  | `{span_name="rpc.process"}`                  | -- (available but not paneled)                |
| `rpc.command.*`                | `{span_name=~"rpc.command.*"}`               | RPC Performance (all 4 panels)                |
| `tx.process`                   | `{span_name="tx.process"}`                   | Transaction Overview (3 panels)               |
| `tx.receive`                   | `{span_name="tx.receive"}`                   | Transaction Overview (2 panels)               |
| `txq.enqueue`                  | `{span_name="txq.enqueue"}`                  | -- (available but not paneled)                |
| `txq.apply_direct`             | `{span_name="txq.apply_direct"}`             | -- (available but not paneled)                |
| `txq.batch_clear`              | `{span_name="txq.batch_clear"}`              | -- (available but not paneled)                |
| `txq.accept`                   | `{span_name="txq.accept"}`                   | -- (available but not paneled)                |
| `txq.accept_tx`                | `{span_name="txq.accept_tx"}`                | -- (available but not paneled)                |
| `txq.cleanup`                  | `{span_name="txq.cleanup"}`                  | -- (available but not paneled)                |
| `consensus.round`              | `{span_name="consensus.round"}`              | -- (available but not paneled)                |
| `consensus.phase.open`         | `{span_name="consensus.phase.open"}`         | -- (available but not paneled)                |
| `consensus.establish`          | `{span_name="consensus.establish"}`          | -- (available but not paneled)                |
| `consensus.update_positions`   | `{span_name="consensus.update_positions"}`   | -- (available but not paneled)                |
| `consensus.check`              | `{span_name="consensus.check"}`              | -- (available but not paneled)                |
| `consensus.accept`             | `{span_name="consensus.accept"}`             | Consensus Health (Round Duration)             |
| `consensus.proposal.send`      | `{span_name="consensus.proposal.send"}`      | Consensus Health (Proposals Rate)             |
| `consensus.ledger_close`       | `{span_name="consensus.ledger_close"}`       | Consensus Health (Close Duration)             |
| `consensus.validation.send`    | `{span_name="consensus.validation.send"}`    | Consensus Health (Validation Rate)            |
| `consensus.accept.apply`       | `{span_name="consensus.accept.apply"}`       | Consensus Health (Apply Duration, Close Time) |
| `consensus.mode_change`        | `{span_name="consensus.mode_change"}`        | -- (available but not paneled)                |
| `consensus.proposal.receive`   | `{span_name="consensus.proposal.receive"}`   | -- (available but not paneled)                |
| `consensus.validation.receive` | `{span_name="consensus.validation.receive"}` | -- (available but not paneled)                |

## Troubleshooting

### No traces appearing in Tempo

1. Check xrpld logs for `Telemetry starting` message
2. Verify `enabled=1` in the `[telemetry]` config section
3. Test collector connectivity: `curl -v http://localhost:4318/v1/traces`
4. Check collector logs: `docker compose -f docker/telemetry/docker-compose.yml logs otel-collector`
5. Verify Tempo is receiving data: open Grafana → Explore → select Tempo datasource → search by `service.name = xrpld`
6. Check Tempo logs: `docker compose -f docker/telemetry/docker-compose.yml logs tempo`

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
