# rippled Telemetry Operator Runbook

## Overview

rippled supports OpenTelemetry distributed tracing to provide visibility into RPC requests, transaction processing, and consensus rounds.

## Quick Start

### 1. Start the observability stack

```bash
docker compose -f docker/telemetry/docker-compose.yml up -d
```

This starts:

- **OTel Collector** on ports 4317 (gRPC) and 4318 (HTTP)
- **Tempo** on http://localhost:3200 (trace backend)
- **Prometheus** on http://localhost:9090
- **Loki** on http://localhost:3100 (log aggregation)
- **Grafana** on http://localhost:3000

### 2. Enable telemetry in rippled

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

All spans instrumented in rippled, grouped by subsystem:

### RPC Spans (Phase 2)

| Span Name            | Source File           | Attributes                                                                                                                   | Description                                        |
| -------------------- | --------------------- | ---------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------- |
| `rpc.request`        | ServerHandler.cpp:271 | —                                                                                                                            | Top-level HTTP RPC request                         |
| `rpc.process`        | ServerHandler.cpp:573 | —                                                                                                                            | RPC processing (child of rpc.request)              |
| `rpc.ws_message`     | ServerHandler.cpp:384 | —                                                                                                                            | WebSocket RPC message                              |
| `rpc.command.<name>` | RPCHandler.cpp:161    | `xrpl.rpc.command`, `xrpl.rpc.version`, `xrpl.rpc.role`, `xrpl.rpc.status`, `xrpl.rpc.duration_ms`, `xrpl.rpc.error_message` | Per-command span (e.g., `rpc.command.server_info`) |

### Transaction Spans (Phase 3)

| Span Name    | Source File         | Attributes                                                             | Description                           |
| ------------ | ------------------- | ---------------------------------------------------------------------- | ------------------------------------- |
| `tx.process` | NetworkOPs.cpp:1227 | `xrpl.tx.hash`, `xrpl.tx.local`, `xrpl.tx.path`                        | Transaction submission and processing |
| `tx.receive` | PeerImp.cpp:1273    | `xrpl.peer.id`, `xrpl.tx.hash`, `xrpl.tx.suppressed`, `xrpl.tx.status` | Transaction received from peer relay  |
| `tx.apply`   | BuildLedger.cpp:88  | `xrpl.ledger.seq`, `xrpl.ledger.tx_count`, `xrpl.ledger.tx_failed`     | Transaction set applied per ledger    |

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

### Ledger Spans (Phase 5)

| Span Name         | Source File          | Attributes                                                         | Description                   |
| ----------------- | -------------------- | ------------------------------------------------------------------ | ----------------------------- |
| `ledger.build`    | BuildLedger.cpp:31   | `xrpl.ledger.seq`, `xrpl.ledger.tx_count`, `xrpl.ledger.tx_failed` | Ledger build during consensus |
| `ledger.validate` | LedgerMaster.cpp:915 | `xrpl.ledger.seq`, `xrpl.ledger.validations`                       | Ledger promoted to validated  |
| `ledger.store`    | LedgerMaster.cpp:409 | `xrpl.ledger.seq`                                                  | Ledger stored in history      |

### Peer Spans (Phase 5)

| Span Name                 | Source File      | Attributes                                     | Description                   |
| ------------------------- | ---------------- | ---------------------------------------------- | ----------------------------- |
| `peer.proposal.receive`   | PeerImp.cpp:1667 | `xrpl.peer.id`, `xrpl.peer.proposal.trusted`   | Proposal received from peer   |
| `peer.validation.receive` | PeerImp.cpp:2264 | `xrpl.peer.id`, `xrpl.peer.validation.trusted` | Validation received from peer |

## Prometheus Metrics (Spanmetrics)

The OTel Collector's spanmetrics connector automatically derives RED (Rate, Errors, Duration) metrics from every span. No custom metrics code is needed in rippled.

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
| `service_name` | Resource attribute | `rippled`                                |
| `span_kind`    | Span kind          | `SPAN_KIND_INTERNAL`                     |

Additionally, span attributes configured as dimensions in the collector become metric labels (dots → underscores):

| Span Attribute                 | Metric Label                   | Applies To                      |
| ------------------------------ | ------------------------------ | ------------------------------- |
| `xrpl.rpc.command`             | `xrpl_rpc_command`             | `rpc.command.*` spans           |
| `xrpl.rpc.status`              | `xrpl_rpc_status`              | `rpc.command.*` spans           |
| `xrpl.consensus.mode`          | `xrpl_consensus_mode`          | `consensus.ledger_close` spans  |
| `xrpl.tx.local`                | `xrpl_tx_local`                | `tx.process` spans              |
| `xrpl.peer.proposal.trusted`   | `xrpl_peer_proposal_trusted`   | `peer.proposal.receive` spans   |
| `xrpl.peer.validation.trusted` | `xrpl_peer_validation_trusted` | `peer.validation.receive` spans |

### Histogram Buckets

Configured in `otel-collector-config.yaml`:

```
1ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1s, 5s
```

## System Metrics (beast::insight via OTel native)

rippled has a built-in metrics framework (`beast::insight`) that exports metrics natively via OTLP/HTTP. These complement the span-derived RED metrics by providing system-level gauges, counters, and timers that don't map to individual trace spans.

### Configuration

Add to `xrpld.cfg`:

```ini
[insight]
server=otel
endpoint=http://localhost:4318/v1/metrics
prefix=rippled
```

The OTel Collector receives these via the OTLP receiver (same endpoint as traces, port 4318) and exports them to Prometheus alongside spanmetrics.

#### StatsD fallback (backward compatibility)

The legacy StatsD backend is still available:

```ini
[insight]
server=statsd
address=127.0.0.1:8125
prefix=rippled
```

When using StatsD, uncomment the `statsd` receiver in `otel-collector-config.yaml` and add port `8125:8125/udp` to the docker-compose otel-collector service.

### Metric Reference

#### Gauges

| Prometheus Metric                             | Source                    | Description                                                                |
| --------------------------------------------- | ------------------------- | -------------------------------------------------------------------------- |
| `rippled_LedgerMaster_Validated_Ledger_Age`   | LedgerMaster.h:373        | Age of validated ledger (seconds)                                          |
| `rippled_LedgerMaster_Published_Ledger_Age`   | LedgerMaster.h:374        | Age of published ledger (seconds)                                          |
| `rippled_State_Accounting_{Mode}_duration`    | NetworkOPs.cpp:774        | Time in each operating mode (Disconnected/Connected/Syncing/Tracking/Full) |
| `rippled_State_Accounting_{Mode}_transitions` | NetworkOPs.cpp:780        | Transition count per mode                                                  |
| `rippled_Peer_Finder_Active_Inbound_Peers`    | PeerfinderManager.cpp:214 | Active inbound peer connections                                            |
| `rippled_Peer_Finder_Active_Outbound_Peers`   | PeerfinderManager.cpp:215 | Active outbound peer connections                                           |
| `rippled_Overlay_Peer_Disconnects`            | OverlayImpl.h:557         | Peer disconnect count                                                      |
| `rippled_job_count`                           | JobQueue.cpp:26           | Current job queue depth                                                    |
| `rippled_{category}_Bytes_In/Out`             | OverlayImpl.h:535         | Overlay traffic bytes per category (57 categories)                         |
| `rippled_{category}_Messages_In/Out`          | OverlayImpl.h:535         | Overlay traffic messages per category                                      |

#### OTel MetricsRegistry Gauges (Phase 9)

These gauges are exported via the OTel Metrics SDK `PeriodicMetricReader` (10s interval), NOT through beast::insight.

| Prometheus Metric                                           | Source              | Description                                  |
| ----------------------------------------------------------- | ------------------- | -------------------------------------------- |
| `rippled_server_info{metric="server_state"}`                | MetricsRegistry.cpp | Operating mode (0=DISCONNECTED .. 4=FULL)    |
| `rippled_server_info{metric="uptime"}`                      | MetricsRegistry.cpp | Seconds since server start                   |
| `rippled_server_info{metric="peers"}`                       | MetricsRegistry.cpp | Total connected peers                        |
| `rippled_server_info{metric="validated_ledger_seq"}`        | MetricsRegistry.cpp | Validated ledger sequence number             |
| `rippled_server_info{metric="ledger_current_index"}`        | MetricsRegistry.cpp | Current open ledger sequence                 |
| `rippled_server_info{metric="peer_disconnects_resources"}`  | MetricsRegistry.cpp | Cumulative resource-related peer disconnects |
| `rippled_server_info{metric="last_close_proposers"}`        | MetricsRegistry.cpp | Proposers in last closed round               |
| `rippled_server_info{metric="last_close_converge_time_ms"}` | MetricsRegistry.cpp | Last close convergence time (ms)             |
| `rippled_build_info{version="<ver>"}`                       | MetricsRegistry.cpp | Info-style metric (always 1)                 |
| `rippled_complete_ledgers{bound="start\|end",index="<N>"}`  | MetricsRegistry.cpp | Complete ledger range start/end pairs        |
| `rippled_db_metrics{metric="db_kb_total"}`                  | MetricsRegistry.cpp | Total database size (KB)                     |
| `rippled_db_metrics{metric="db_kb_ledger"}`                 | MetricsRegistry.cpp | Ledger database size (KB)                    |
| `rippled_db_metrics{metric="db_kb_transaction"}`            | MetricsRegistry.cpp | Transaction database size (KB)               |
| `rippled_db_metrics{metric="historical_perminute"}`         | MetricsRegistry.cpp | Historical ledger fetches per minute         |
| `rippled_cache_metrics{metric="AL_size"}`                   | MetricsRegistry.cpp | AcceptedLedger cache size                    |
| `rippled_nodestore_state{metric="node_reads_duration_us"}`  | MetricsRegistry.cpp | Cumulative read time (microseconds)          |
| `rippled_nodestore_state{metric="read_request_bundle"}`     | MetricsRegistry.cpp | Read request bundle count                    |
| `rippled_nodestore_state{metric="read_threads_running"}`    | MetricsRegistry.cpp | Active read threads                          |
| `rippled_nodestore_state{metric="read_threads_total"}`      | MetricsRegistry.cpp | Total read threads configured                |

#### Counters

| Prometheus Metric                 | Source                | Description                    |
| --------------------------------- | --------------------- | ------------------------------ |
| `rippled_rpc_requests`            | ServerHandler.cpp:108 | Total RPC request count        |
| `rippled_ledger_fetches`          | InboundLedgers.cpp:44 | Ledger fetch request count     |
| `rippled_ledger_history_mismatch` | LedgerHistory.cpp:16  | Ledger hash mismatch count     |
| `rippled_warn`                    | Logic.h:33            | Resource manager warning count |
| `rippled_drop`                    | Logic.h:34            | Resource manager drop count    |

#### Histograms (from StatsD timers)

| Prometheus Metric       | Source                | Description                    |
| ----------------------- | --------------------- | ------------------------------ |
| `rippled_rpc_time`      | ServerHandler.cpp:110 | RPC response time (ms)         |
| `rippled_rpc_size`      | ServerHandler.cpp:109 | RPC response size (bytes)      |
| `rippled_ios_latency`   | Application.cpp:438   | I/O service loop latency (ms)  |
| `rippled_pathfind_fast` | PathRequests.h:23     | Fast pathfinding duration (ms) |
| `rippled_pathfind_full` | PathRequests.h:24     | Full pathfinding duration (ms) |

## Grafana Dashboards

Eight dashboards are pre-provisioned in `docker/telemetry/grafana/dashboards/`:

### RPC Performance (`rippled-rpc-perf`)

| Panel                       | Type       | PromQL                                                                                                                                             | Labels Used                       |
| --------------------------- | ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------- |
| RPC Request Rate by Command | timeseries | `sum by (xrpl_rpc_command) (rate(traces_span_metrics_calls_total{span_name=~"rpc.command.*"}[5m]))`                                                | `xrpl_rpc_command`                |
| RPC Latency p95 by Command  | timeseries | `histogram_quantile(0.95, sum by (le, xrpl_rpc_command) (rate(traces_span_metrics_duration_milliseconds_bucket{span_name=~"rpc.command.*"}[5m])))` | `xrpl_rpc_command`                |
| RPC Error Rate              | bargauge   | Error spans / total spans × 100, grouped by `xrpl_rpc_command`                                                                                     | `xrpl_rpc_command`, `status_code` |
| RPC Latency Heatmap         | heatmap    | `sum(increase(traces_span_metrics_duration_milliseconds_bucket{span_name=~"rpc.command.*"}[5m])) by (le)`                                          | `le` (bucket boundaries)          |
| Overall RPC Throughput      | timeseries | `rpc.request` + `rpc.process` rate                                                                                                                 | —                                 |
| RPC Success vs Error        | timeseries | by `status_code` (UNSET vs ERROR)                                                                                                                  | `status_code`                     |
| Top Commands by Volume      | bargauge   | `topk(10, ...)` by `xrpl_rpc_command`                                                                                                              | `xrpl_rpc_command`                |
| WebSocket Message Rate      | stat       | `rpc.ws_message` rate                                                                                                                              | —                                 |

### Transaction Overview (`rippled-transactions`)

| Panel                             | Type       | PromQL                                                                                       | Labels Used     |
| --------------------------------- | ---------- | -------------------------------------------------------------------------------------------- | --------------- |
| Transaction Processing Rate       | timeseries | `rate(traces_span_metrics_calls_total{span_name="tx.process"}[5m])` and `tx.receive`         | `span_name`     |
| Transaction Processing Latency    | timeseries | `histogram_quantile(0.95 / 0.50, ... {span_name="tx.process"})`                              | —               |
| Transaction Path Distribution     | piechart   | `sum by (xrpl_tx_local) (rate(traces_span_metrics_calls_total{span_name="tx.process"}[5m]))` | `xrpl_tx_local` |
| Transaction Receive vs Suppressed | timeseries | `rate(traces_span_metrics_calls_total{span_name="tx.receive"}[5m])`                          | —               |
| TX Processing Duration Heatmap    | heatmap    | `tx.process` histogram buckets                                                               | `le`            |
| TX Apply Duration per Ledger      | timeseries | p95/p50 of `tx.apply`                                                                        | —               |
| Peer TX Receive Rate              | timeseries | `tx.receive` rate                                                                            | —               |
| TX Apply Failed Rate              | stat       | `tx.apply` with `STATUS_CODE_ERROR`                                                          | `status_code`   |

### Consensus Health (`rippled-consensus`)

| Panel                         | Type       | PromQL                                                                             | Labels Used           |
| ----------------------------- | ---------- | ---------------------------------------------------------------------------------- | --------------------- |
| Consensus Round Duration      | timeseries | `histogram_quantile(0.95 / 0.50, ... {span_name="consensus.accept"})`              | —                     |
| Consensus Proposals Sent Rate | timeseries | `rate(traces_span_metrics_calls_total{span_name="consensus.proposal.send"}[5m])`   | —                     |
| Ledger Close Duration         | timeseries | `histogram_quantile(0.95, ... {span_name="consensus.ledger_close"})`               | —                     |
| Validation Send Rate          | stat       | `rate(traces_span_metrics_calls_total{span_name="consensus.validation.send"}[5m])` | —                     |
| Ledger Apply Duration         | timeseries | `histogram_quantile(0.95 / 0.50, ... {span_name="consensus.accept.apply"})`        | —                     |
| Close Time Agreement          | timeseries | `rate(traces_span_metrics_calls_total{span_name="consensus.accept.apply"}[5m])`    | —                     |
| Consensus Mode Over Time      | timeseries | `consensus.ledger_close` by `xrpl_consensus_mode`                                  | `xrpl_consensus_mode` |
| Accept vs Close Rate          | timeseries | `consensus.accept` vs `consensus.ledger_close` rate                                | —                     |
| Validation vs Close Rate      | timeseries | `consensus.validation.send` vs `consensus.ledger_close`                            | —                     |
| Accept Duration Heatmap       | heatmap    | `consensus.accept` histogram buckets                                               | `le`                  |

### Ledger Operations (`rippled-ledger-ops`)

| Panel                   | Type       | PromQL                                         | Labels Used |
| ----------------------- | ---------- | ---------------------------------------------- | ----------- |
| Ledger Build Rate       | stat       | `ledger.build` call rate                       | —           |
| Ledger Build Duration   | timeseries | p95/p50 of `ledger.build`                      | —           |
| Ledger Validation Rate  | stat       | `ledger.validate` call rate                    | —           |
| Build Duration Heatmap  | heatmap    | `ledger.build` histogram buckets               | `le`        |
| TX Apply Duration       | timeseries | p95/p50 of `tx.apply`                          | —           |
| TX Apply Rate           | timeseries | `tx.apply` call rate                           | —           |
| Ledger Store Rate       | stat       | `ledger.store` call rate                       | —           |
| Build vs Close Duration | timeseries | p95 `ledger.build` vs `consensus.ledger_close` | —           |

### Peer Network (`rippled-peer-net`)

Requires `trace_peer=1` in the `[telemetry]` config section.

| Panel                            | Type       | PromQL                            | Labels Used                    |
| -------------------------------- | ---------- | --------------------------------- | ------------------------------ |
| Proposal Receive Rate            | timeseries | `peer.proposal.receive` rate      | —                              |
| Validation Receive Rate          | timeseries | `peer.validation.receive` rate    | —                              |
| Proposals Trusted vs Untrusted   | piechart   | by `xrpl_peer_proposal_trusted`   | `xrpl_peer_proposal_trusted`   |
| Validations Trusted vs Untrusted | piechart   | by `xrpl_peer_validation_trusted` | `xrpl_peer_validation_trusted` |

### Node Health — System Metrics (`rippled-system-node-health`)

| Panel                      | Type       | PromQL                                                 | Labels Used      |
| -------------------------- | ---------- | ------------------------------------------------------ | ---------------- |
| Validated Ledger Age       | stat       | `rippled_LedgerMaster_Validated_Ledger_Age`            | —                |
| Published Ledger Age       | stat       | `rippled_LedgerMaster_Published_Ledger_Age`            | —                |
| Operating Mode Duration    | timeseries | `rippled_State_Accounting_*_duration`                  | —                |
| Operating Mode Transitions | timeseries | `rippled_State_Accounting_*_transitions`               | —                |
| I/O Latency                | timeseries | `histogram_quantile(0.95, rippled_ios_latency_bucket)` | —                |
| Job Queue Depth            | timeseries | `rippled_job_count`                                    | —                |
| Ledger Fetch Rate          | stat       | `rate(rippled_ledger_fetches[5m])`                     | —                |
| Ledger History Mismatches  | stat       | `rate(rippled_ledger_history_mismatch[5m])`            | —                |
| Server State               | stat       | `rippled_server_info{metric="server_state"}`           | `metric`         |
| Uptime                     | stat       | `rippled_server_info{metric="uptime"}`                 | `metric`         |
| Peer Count                 | stat       | `rippled_server_info{metric="peers"}`                  | `metric`         |
| Validated Ledger Seq       | stat       | `rippled_server_info{metric="validated_ledger_seq"}`   | `metric`         |
| Build Version              | stat       | `rippled_build_info`                                   | `version`        |
| Complete Ledger Ranges     | table      | `rippled_complete_ledgers`                             | `bound`, `index` |
| Database Sizes             | timeseries | `rippled_db_metrics{metric=~"db_kb_.*"}`               | `metric`         |
| Historical Fetch Rate      | stat       | `rippled_db_metrics{metric="historical_perminute"}`    | `metric`         |

### Network Traffic — System Metrics (`rippled-system-network`)

| Panel                  | Type       | PromQL                                 | Labels Used |
| ---------------------- | ---------- | -------------------------------------- | ----------- |
| Active Peers           | timeseries | `rippled_Peer_Finder_Active_*_Peers`   | —           |
| Peer Disconnects       | timeseries | `rippled_Overlay_Peer_Disconnects`     | —           |
| Total Network Bytes    | timeseries | `rippled_total_Bytes_In/Out`           | —           |
| Total Network Messages | timeseries | `rippled_total_Messages_In/Out`        | —           |
| Transaction Traffic    | timeseries | `rippled_transactions_Messages_In/Out` | —           |
| Proposal Traffic       | timeseries | `rippled_proposals_Messages_In/Out`    | —           |
| Validation Traffic     | timeseries | `rippled_validations_Messages_In/Out`  | —           |
| Traffic by Category    | bargauge   | `topk(10, rippled_*_Bytes_In)`         | —           |

### RPC & Pathfinding — System Metrics (`rippled-system-rpc`)

| Panel                     | Type       | PromQL                                                   | Labels Used |
| ------------------------- | ---------- | -------------------------------------------------------- | ----------- |
| RPC Request Rate          | stat       | `rate(rippled_rpc_requests[5m])`                         | —           |
| RPC Response Time         | timeseries | `histogram_quantile(0.95, rippled_rpc_time_bucket)`      | —           |
| RPC Response Size         | timeseries | `histogram_quantile(0.95, rippled_rpc_size_bucket)`      | —           |
| RPC Response Time Heatmap | heatmap    | `rippled_rpc_time_bucket`                                | —           |
| Pathfinding Fast Duration | timeseries | `histogram_quantile(0.95, rippled_pathfind_fast_bucket)` | —           |
| Pathfinding Full Duration | timeseries | `histogram_quantile(0.95, rippled_pathfind_full_bucket)` | —           |
| Resource Warnings Rate    | stat       | `rate(rippled_warn[5m])`                                 | —           |
| Resource Drops Rate       | stat       | `rate(rippled_drop[5m])`                                 | —           |

### Span → Metric → Dashboard Summary

| Span Name                   | Prometheus Metric Filter                  | Grafana Dashboard                             |
| --------------------------- | ----------------------------------------- | --------------------------------------------- |
| `rpc.request`               | `{span_name="rpc.request"}`               | RPC Performance (Overall Throughput)          |
| `rpc.process`               | `{span_name="rpc.process"}`               | RPC Performance (Overall Throughput)          |
| `rpc.ws_message`            | `{span_name="rpc.ws_message"}`            | RPC Performance (WebSocket Rate)              |
| `rpc.command.*`             | `{span_name=~"rpc.command.*"}`            | RPC Performance (Rate, Latency, Error, Top)   |
| `tx.process`                | `{span_name="tx.process"}`                | Transaction Overview (Rate, Latency, Heatmap) |
| `tx.receive`                | `{span_name="tx.receive"}`                | Transaction Overview (Rate, Receive)          |
| `tx.apply`                  | `{span_name="tx.apply"}`                  | Transaction Overview + Ledger Ops (Apply)     |
| `consensus.accept`          | `{span_name="consensus.accept"}`          | Consensus Health (Duration, Rate, Heatmap)    |
| `consensus.proposal.send`   | `{span_name="consensus.proposal.send"}`   | Consensus Health (Proposals Rate)             |
| `consensus.ledger_close`    | `{span_name="consensus.ledger_close"}`    | Consensus Health (Close, Mode)                |
| `consensus.validation.send` | `{span_name="consensus.validation.send"}` | Consensus Health (Validation Rate)            |
| `consensus.accept.apply`    | `{span_name="consensus.accept.apply"}`    | Consensus Health (Apply Duration, Close Time) |
| `ledger.build`              | `{span_name="ledger.build"}`              | Ledger Ops (Build Rate, Duration, Heatmap)    |
| `ledger.validate`           | `{span_name="ledger.validate"}`           | Ledger Ops (Validation Rate)                  |
| `ledger.store`              | `{span_name="ledger.store"}`              | Ledger Ops (Store Rate)                       |
| `peer.proposal.receive`     | `{span_name="peer.proposal.receive"}`     | Peer Network (Rate, Trusted/Untrusted)        |
| `peer.validation.receive`   | `{span_name="peer.validation.receive"}`   | Peer Network (Rate, Trusted/Untrusted)        |

## Log-Trace Correlation (Phase 8)

When rippled is built with `telemetry=ON`, log lines emitted within an active OpenTelemetry span automatically include `trace_id` and `span_id` fields:

```
2024-01-15T10:30:45.123Z LedgerMaster:NFO trace_id=abc123def456789012345678abcdef01 span_id=0123456789abcdef Validated ledger 42
```

This enables bidirectional navigation between logs and traces in Grafana:

- **Tempo -> Loki**: Click "Logs for this trace" on any trace in Grafana Tempo to see all log lines from that trace.
- **Loki -> Tempo**: Click the `TraceID` derived field link on any log line containing `trace_id=` to jump to the full trace in Tempo.

### Log Ingestion Pipeline

Log files are ingested by the OTel Collector's `filelog` receiver, which tails `debug.log` files and parses them with a regex that extracts `timestamp`, `partition`, `severity`, `trace_id`, `span_id`, and `message` fields. Parsed entries are exported to Grafana Loki.

### LogQL Query Examples

```logql
# Find all logs for a specific trace
{job="rippled"} |= "trace_id=abc123def456789012345678abcdef01"

# Error logs with trace context (log lines with ERR severity that have a trace_id)
{job="rippled"} |= "ERR" |= "trace_id="

# All logs from a specific partition that were emitted during a span
{job="rippled"} |= "LedgerMaster" | regexp `trace_id=(?P<trace_id>[a-f0-9]+)` | trace_id != ""

# Logs from the last hour containing trace context
{job="rippled"} |= "trace_id=" | regexp `(?P<partition>\S+):(?P<sev>\S+)\s+trace_id=(?P<tid>[a-f0-9]+)`

# Count of traced vs untraced log lines
count_over_time({job="rippled"} |= "trace_id=" [5m])
```

### Verifying Log Correlation

1. Start the observability stack and rippled with telemetry enabled.
2. Send an RPC request: `curl http://localhost:5005 -d '{"method":"server_info"}'`
3. Check the debug.log for `trace_id=` entries: `grep trace_id= /path/to/debug.log`
4. Open Grafana at http://localhost:3000 -> Explore -> Loki and search for `{job="rippled"} |= "trace_id="`.
5. Click the TraceID link to navigate to the corresponding trace in Tempo.

## Troubleshooting

### No traces appearing in Tempo

1. Check rippled logs for `Telemetry starting` message
2. Verify `enabled=1` in the `[telemetry]` config section
3. Test collector connectivity: `curl -v http://localhost:4318/v1/traces`
4. Check collector logs: `docker compose logs otel-collector`

### No system metrics in Prometheus

1. Check rippled logs for `OTelCollector starting` message
2. Verify `server=otel` in the `[insight]` config section
3. Verify the endpoint in `[insight]` points to the OTLP/HTTP port (default: `http://localhost:4318/v1/metrics`)
4. Check that the `otlp` receiver is in the metrics pipeline receivers in `otel-collector-config.yaml`
5. Query Prometheus directly: `curl 'http://localhost:9090/api/v1/query?query=rippled_job_count'`

### Server info gauge shows server_state=0

This is normal during startup. The server starts in DISCONNECTED mode (0) and
progresses through CONNECTED (1), SYNCING (2), TRACKING (3), to FULL (4).
Wait for the node to sync with the network.

### Database metrics showing zero

The `getKBUsed*()` methods require SQLite databases to exist. If running with
`--standalone` or before the first ledger is stored, these will be zero.

### High memory usage

- Reduce `sampling_ratio` (e.g., `0.1` for 10% sampling)
- Reduce `max_queue_size` and `batch_size`
- Disable high-volume trace categories: `trace_peer=0`

### Collector connection failures

- Verify endpoint URL matches collector address
- Check firewall rules for ports 4317/4318
- If using TLS, verify certificate path with `tls_ca_cert`

### No trace_id in log output

- Verify rippled was built with `telemetry=ON` (the `XRPL_ENABLE_TELEMETRY` preprocessor flag)
- Verify `enabled=1` in the `[telemetry]` config section
- Log lines only contain `trace_id`/`span_id` when emitted inside an active span — background logs outside of RPC/consensus/transaction processing will not have trace context
- Check that the specific trace category is enabled (e.g., `trace_rpc=1`)

### No logs in Loki

- Verify the log file mount in docker-compose.yml points to the correct rippled log directory
- Check OTel Collector logs for filelog receiver errors: `docker compose logs otel-collector`
- Verify Loki is running: `curl http://localhost:3100/ready`
- Check the filelog receiver glob pattern matches your log file paths

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

## Validating Telemetry Stack

After deploying telemetry, use the Phase 10 workload tools to validate the full stack end-to-end.

### Quick Validation

```bash
# Run the full validation suite (starts cluster, generates load, validates):
docker/telemetry/workload/run-full-validation.sh --xrpld .build/xrpld

# Check the report:
cat /tmp/xrpld-validation/reports/validation-report.json | jq '.summary'
```

### What Gets Validated

| Category   | Checks         | Description                                             |
| ---------- | -------------- | ------------------------------------------------------- |
| Spans      | 16+ span types | All span names appear in Tempo with required attributes |
| Metrics    | 30+ metrics    | SpanMetrics, StatsD gauges/counters, Phase 9 metrics    |
| Logs       | 2 checks       | trace_id/span_id present in Loki, cross-reference works |
| Dashboards | 10 dashboards  | All Grafana dashboards load without errors              |

### Running Individual Tools

```bash
# RPC load only:
python3 docker/telemetry/workload/rpc_load_generator.py \
    --endpoints ws://localhost:6006 --rate 50 --duration 120

# Transaction mix only:
python3 docker/telemetry/workload/tx_submitter.py \
    --endpoint ws://localhost:6006 --tps 5 --duration 120

# Validation only (assumes load already ran):
python3 docker/telemetry/workload/validate_telemetry.py \
    --report /tmp/report.json
```

### Interpreting Failures

- **Span failures**: Check that the relevant trace category is enabled in `[telemetry]` config (e.g., `trace_rpc=1`).
- **Metric failures**: Verify the OTel Collector is running and Prometheus is scraping port 8889. Check `docker compose logs otel-collector`.
- **Dashboard failures**: Ensure Grafana provisioning is mounted correctly. Check `docker compose logs grafana`.

## Performance Benchmarking

Measure the overhead of the telemetry stack against a baseline:

```bash
docker/telemetry/workload/benchmark.sh --xrpld .build/xrpld --duration 300
```

### Benchmark Thresholds

| Metric            | Target | Description                            |
| ----------------- | ------ | -------------------------------------- |
| CPU overhead      | < 3%   | Average CPU increase across nodes      |
| Memory overhead   | < 5MB  | Peak RSS increase per node             |
| RPC p99 latency   | < 2ms  | Additional p99 latency for server_info |
| Throughput impact | < 5%   | Reduction in ledger close rate         |
| Consensus impact  | < 1%   | Increase in consensus round time       |

### Tuning for Production

If benchmarks exceed thresholds:

1. **Reduce sampling**: `sampling_ratio=0.01` (1% of traces)
2. **Disable peer tracing**: `trace_peer=0` (highest volume category)
3. **Increase batch delay**: `batch_delay_ms=10000` (less frequent exports)
4. **Reduce queue size**: `max_queue_size=1024` (back-pressure earlier)

See `docker/telemetry/workload/README.md` for full documentation.
