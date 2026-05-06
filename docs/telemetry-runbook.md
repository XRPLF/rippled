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
- **Tempo** on http://localhost:3200 (trace backend)
- **Prometheus** on http://localhost:9090
- **Loki** on http://localhost:3100 (log aggregation)
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

| Span Name            | Source File           | Attributes                                                                                                                   | Description                                        |
| -------------------- | --------------------- | ---------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------- |
| `rpc.request`        | ServerHandler.cpp:271 | —                                                                                                                            | Top-level HTTP RPC request                         |
| `rpc.process`        | ServerHandler.cpp:573 | —                                                                                                                            | RPC processing (child of rpc.request)              |
| `rpc.ws_message`     | ServerHandler.cpp:384 | —                                                                                                                            | WebSocket RPC message                              |
| `rpc.command.<name>` | RPCHandler.cpp:161    | `xrpl.rpc.command`, `xrpl.rpc.version`, `xrpl.rpc.role`, `xrpl.rpc.status`, `xrpl.rpc.duration_ms`, `xrpl.rpc.error_message` | Per-command span (e.g., `rpc.command.server_info`) |

### Transaction Spans (Phase 3)

| Span Name    | Source File         | Attributes                                                                                  | Description                           |
| ------------ | ------------------- | ------------------------------------------------------------------------------------------- | ------------------------------------- |
| `tx.process` | NetworkOPs.cpp:1227 | `xrpl.tx.hash`, `xrpl.tx.local`, `xrpl.tx.path`                                             | Transaction submission and processing |
| `tx.receive` | PeerImp.cpp:1273    | `xrpl.peer.id`, `xrpl.tx.hash`, `xrpl.peer.version`, `xrpl.tx.suppressed`, `xrpl.tx.status` | Transaction received from peer relay  |
| `tx.apply`   | BuildLedger.cpp:88  | `xrpl.ledger.seq`, `xrpl.ledger.tx_count`, `xrpl.ledger.tx_failed`                          | Transaction set applied per ledger    |

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

| Span Name                      | Source File      | Attributes                                                                                                                                                                                                                                                                                                                                                                                             | Description                                                                                                                           |
| ------------------------------ | ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------- |
| `consensus.round`              | RCLConsensus.cpp | `xrpl.consensus.ledger_id`, `xrpl.consensus.ledger.seq`, `xrpl.consensus.mode`, `xrpl.consensus.trace_strategy`, `xrpl.consensus.round_id`                                                                                                                                                                                                                                                             | Root span for a consensus round (deterministic or random trace ID)                                                                    |
| `consensus.phase.open`         | Consensus.h      | --                                                                                                                                                                                                                                                                                                                                                                                                     | Open phase duration (child of round)                                                                                                  |
| `consensus.proposal.send`      | RCLConsensus.cpp | `xrpl.consensus.round`                                                                                                                                                                                                                                                                                                                                                                                 | Consensus proposal broadcast                                                                                                          |
| `consensus.ledger_close`       | RCLConsensus.cpp | `xrpl.consensus.ledger.seq`, `xrpl.consensus.mode`                                                                                                                                                                                                                                                                                                                                                     | Ledger close event                                                                                                                    |
| `consensus.establish`          | Consensus.h      | `xrpl.consensus.converge_percent`, `xrpl.consensus.establish_count`, `xrpl.consensus.proposers`                                                                                                                                                                                                                                                                                                        | Establish phase duration (child of round)                                                                                             |
| `consensus.update_positions`   | Consensus.h      | `xrpl.consensus.converge_percent`, `xrpl.consensus.proposers`, `xrpl.consensus.disputes_count`                                                                                                                                                                                                                                                                                                         | Position update and dispute resolution (see Events below)                                                                             |
| `consensus.check`              | Consensus.h      | `xrpl.consensus.agree_count`, `xrpl.consensus.disagree_count`, `xrpl.consensus.converge_percent`, `xrpl.consensus.have_close_time_consensus`, `xrpl.consensus.threshold_percent`, `xrpl.consensus.result`                                                                                                                                                                                              | Consensus threshold check                                                                                                             |
| `consensus.accept`             | RCLConsensus.cpp | `xrpl.consensus.proposers`, `xrpl.consensus.round_time_ms`, `xrpl.consensus.quorum`                                                                                                                                                                                                                                                                                                                    | Ledger accepted by consensus                                                                                                          |
| `consensus.accept.apply`       | RCLConsensus.cpp | `xrpl.consensus.ledger.seq`, `xrpl.consensus.close_time`, `xrpl.consensus.close_time_correct`, `xrpl.consensus.close_resolution_ms`, `xrpl.consensus.state`, `xrpl.consensus.proposing`, `xrpl.consensus.round_time_ms`, `xrpl.consensus.parent_close_time`, `xrpl.consensus.close_time_self`, `xrpl.consensus.close_time_vote_bins`, `xrpl.consensus.resolution_direction`, `xrpl.consensus.tx_count` | Ledger application with close time details (see Events below)                                                                         |
| `consensus.validation.send`    | RCLConsensus.cpp | `xrpl.consensus.ledger.seq`, `xrpl.consensus.proposing`                                                                                                                                                                                                                                                                                                                                                | Validation sent after accept (follows-from link)                                                                                      |
| `consensus.mode_change`        | RCLConsensus.cpp | `xrpl.consensus.mode.old`, `xrpl.consensus.mode.new`                                                                                                                                                                                                                                                                                                                                                   | Consensus mode transition                                                                                                             |
| `consensus.proposal.receive`   | PeerImp.cpp      | `xrpl.consensus.trusted`, `xrpl.consensus.round`                                                                                                                                                                                                                                                                                                                                                       | Proposal received from peer (extracts parent context from TraceContext when present; falls back to standalone span for older peers)   |
| `consensus.validation.receive` | PeerImp.cpp      | `xrpl.consensus.trusted`, `xrpl.consensus.ledger.seq`                                                                                                                                                                                                                                                                                                                                                  | Validation received from peer (extracts parent context from TraceContext when present; falls back to standalone span for older peers) |

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

## Cross-Node Trace Propagation

xrpld propagates trace context across nodes via protobuf `TraceContext` fields
embedded in peer-to-peer messages. When Node A sends a transaction, proposal,
or validation, it injects its active span's trace/span IDs into the protobuf
message. Node B extracts that context on receipt and creates a child span,
linking the two nodes into a single distributed trace.

### How It Works

```
Node A (sender)                          Node B (receiver)
+-----------------------------+          +-------------------------------+
| tx.process / consensus.*    |          | PeerImp::onMessage()          |
|   |                         |          |   |                           |
|   v                         |          |   v                           |
| SpanGuard::getTraceBytes()  |          | extract TraceContext from      |
|   |                         |          | protobuf message               |
|   v                         |   send   |   |                           |
| injectSpanContext() --------|--------->|   v                           |
| sets TraceContext fields    |  proto   | txReceiveSpan()               |
| (trace_id, span_id, flags) |  msg     | proposalReceiveSpan()         |
+-----------------------------+          | validationReceiveSpan()       |
                                         |   |                           |
                                         |   v                           |
                                         | child span with parent link   |
                                         +-------------------------------+
```

### Send-Side Injection

| Message Type  | Injection Point            | Mechanism                                  |
| ------------- | -------------------------- | ------------------------------------------ |
| TMTransaction | `NetworkOPs::apply()`      | Injects `tx.process` span into relay msg   |
| TMProposeSet  | `RCLConsensus::propose()`  | Injects active context into proposal msg   |
| TMValidation  | `RCLConsensus::validate()` | Injects active context into validation msg |

### Receive-Side Extraction

| Message Type  | Extraction Point                    | Helper Function                                    |
| ------------- | ----------------------------------- | -------------------------------------------------- |
| TMTransaction | `PeerImp::onMessage(TMTransaction)` | `TxTracing::txReceiveSpan()`                       |
| TMProposeSet  | `PeerImp::onMessage(TMProposeSet)`  | `ConsensusReceiveTracing::proposalReceiveSpan()`   |
| TMValidation  | `PeerImp::onMessage(TMValidation)`  | `ConsensusReceiveTracing::validationReceiveSpan()` |

### Key Files

| File                                              | Role                                            |
| ------------------------------------------------- | ----------------------------------------------- |
| `src/xrpld/telemetry/PropagationHelpers.h`        | `injectSpanContext()` — SpanGuard to protobuf   |
| `include/xrpl/telemetry/TraceContextPropagator.h` | OTel context <-> protobuf conversion primitives |
| `src/xrpld/telemetry/ConsensusReceiveTracing.h`   | Proposal/validation receive span factories      |
| `src/xrpld/telemetry/TxTracing.h`                 | Transaction receive span factory                |

### Backwards Compatibility

Older peers that do not populate `TraceContext` fields in their messages will
simply produce empty trace bytes on the receive side. The extraction helpers
detect this and create standalone (root) spans instead of child spans. No
errors are logged and no data is lost — the receive span is still created with
all its normal attributes, it just lacks a cross-node parent link.

### Example Tempo Queries

```
# Find cross-node transaction traces (tx.process -> tx.receive across nodes)
{name="tx.receive"} && status != error

# Find proposals received with cross-node parent context
{name="consensus.proposal.receive"} && nestedSetParent > 0

# Trace a transaction across the network by its hash
{name=~"tx\\..*"} | xrpl.tx.hash = "<hash>"

# Find all spans in a cross-node consensus trace
{rootServiceName="xrpld"} | xrpl.consensus.round_id = "<round_id>"

# Compare latency between sender and receiver for validations
{name="consensus.validation.send" || name="consensus.validation.receive"}
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

## System Metrics (OTel native -- beast::insight)

xrpld has a built-in metrics framework (`beast::insight`) that exports metrics natively via OTLP to the OTel Collector. These complement the span-derived RED metrics by providing system-level gauges, counters, and timers that don't map to individual trace spans.

### Configuration

Add to `xrpld.cfg`:

```ini
[insight]
server=otel
endpoint=http://localhost:4318/v1/metrics
prefix=xrpld
```

The `OTelCollector` implementation exports metrics via OTLP/HTTP to the same OTel Collector that receives traces. No separate StatsD receiver is needed.

> **Fallback**: Set `server=statsd` and `address=127.0.0.1:8125` to use the legacy StatsD UDP path. This requires re-enabling the `statsd` receiver in `otel-collector-config.yaml` and uncommenting port 8125 in `docker-compose.yml`.

### Metric Reference

#### Gauges

| Prometheus Metric                           | Source                    | Description                                                                |
| ------------------------------------------- | ------------------------- | -------------------------------------------------------------------------- |
| `xrpld_LedgerMaster_Validated_Ledger_Age`   | LedgerMaster.h:373        | Age of validated ledger (seconds)                                          |
| `xrpld_LedgerMaster_Published_Ledger_Age`   | LedgerMaster.h:374        | Age of published ledger (seconds)                                          |
| `xrpld_State_Accounting_{Mode}_duration`    | NetworkOPs.cpp:774        | Time in each operating mode (Disconnected/Connected/Syncing/Tracking/Full) |
| `xrpld_State_Accounting_{Mode}_transitions` | NetworkOPs.cpp:780        | Transition count per mode                                                  |
| `xrpld_Peer_Finder_Active_Inbound_Peers`    | PeerfinderManager.cpp:214 | Active inbound peer connections                                            |
| `xrpld_Peer_Finder_Active_Outbound_Peers`   | PeerfinderManager.cpp:215 | Active outbound peer connections                                           |
| `xrpld_Overlay_Peer_Disconnects`            | OverlayImpl.h:557         | Peer disconnect count                                                      |
| `xrpld_job_count`                           | JobQueue.cpp:26           | Current job queue depth                                                    |
| `xrpld_{category}_Bytes_In/Out`             | OverlayImpl.h:535         | Overlay traffic bytes per category (57 categories)                         |
| `xrpld_{category}_Messages_In/Out`          | OverlayImpl.h:535         | Overlay traffic messages per category                                      |

#### Counters

| Prometheus Metric               | Source                | Description                    |
| ------------------------------- | --------------------- | ------------------------------ |
| `xrpld_rpc_requests`            | ServerHandler.cpp:108 | Total RPC request count        |
| `xrpld_ledger_fetches`          | InboundLedgers.cpp:44 | Ledger fetch request count     |
| `xrpld_ledger_history_mismatch` | LedgerHistory.cpp:16  | Ledger hash mismatch count     |
| `xrpld_warn`                    | Logic.h:33            | Resource manager warning count |
| `xrpld_drop`                    | Logic.h:34            | Resource manager drop count    |

#### Histograms

| Prometheus Metric     | Source                | Description                    |
| --------------------- | --------------------- | ------------------------------ |
| `xrpld_rpc_time`      | ServerHandler.cpp:110 | RPC response time (ms)         |
| `xrpld_rpc_size`      | ServerHandler.cpp:109 | RPC response size (bytes)      |
| `xrpld_ios_latency`   | Application.cpp:438   | I/O service loop latency (ms)  |
| `xrpld_pathfind_fast` | PathRequests.h:23     | Fast pathfinding duration (ms) |
| `xrpld_pathfind_full` | PathRequests.h:24     | Full pathfinding duration (ms) |

## Grafana Dashboards

Ten dashboards are pre-provisioned in `docker/telemetry/grafana/dashboards/`:

### RPC Performance (`xrpld-rpc-perf`)

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

### Transaction Overview (`xrpld-transactions`)

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

### Consensus Health (`xrpld-consensus`)

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

### Ledger Operations (`xrpld-ledger-ops`)

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

### Peer Network (`xrpld-peer-net`)

Requires `trace_peer=1` in the `[telemetry]` config section.

| Panel                            | Type       | PromQL                            | Labels Used                    |
| -------------------------------- | ---------- | --------------------------------- | ------------------------------ |
| Proposal Receive Rate            | timeseries | `peer.proposal.receive` rate      | —                              |
| Validation Receive Rate          | timeseries | `peer.validation.receive` rate    | —                              |
| Proposals Trusted vs Untrusted   | piechart   | by `xrpl_peer_proposal_trusted`   | `xrpl_peer_proposal_trusted`   |
| Validations Trusted vs Untrusted | piechart   | by `xrpl_peer_validation_trusted` | `xrpl_peer_validation_trusted` |

### Node Health -- System Metrics (`xrpld-system-node-health`)

| Panel                                  | Type       | PromQL                                                          | Labels Used |
| -------------------------------------- | ---------- | --------------------------------------------------------------- | ----------- |
| Validated Ledger Age                   | stat       | `xrpld_LedgerMaster_Validated_Ledger_Age`                       | —           |
| Published Ledger Age                   | stat       | `xrpld_LedgerMaster_Published_Ledger_Age`                       | —           |
| Operating Mode Duration                | timeseries | `xrpld_State_Accounting_*_duration`                             | —           |
| Operating Mode Transitions             | timeseries | `xrpld_State_Accounting_*_transitions`                          | —           |
| I/O Latency                            | timeseries | `histogram_quantile(0.95, xrpld_ios_latency_bucket)`            | —           |
| Job Queue Depth                        | timeseries | `xrpld_job_count`                                               | —           |
| Ledger Fetch Rate                      | stat       | `rate(xrpld_ledger_fetches[5m])`                                | —           |
| Ledger History Mismatches              | stat       | `rate(xrpld_ledger_history_mismatch[5m])`                       | —           |
| Key Jobs Execution Time                | timeseries | `xrpld_acceptLedger{quantile="$quantile"}` (+ 10 more key jobs) | `quantile`  |
| Key Jobs Dequeue Wait Time             | timeseries | `xrpld_acceptLedger_q{quantile="$quantile"}` (+ 10 more)        | `quantile`  |
| FullBelowCache Size                    | timeseries | `xrpld_Node_family_full_below_cache_size`                       | —           |
| FullBelowCache Hit Rate                | gauge      | `xrpld_Node_family_full_below_cache_hit_rate`                   | —           |
| Ledger Publish Gap                     | stat       | `Published_Ledger_Age - Validated_Ledger_Age`                   | —           |
| State Duration Rate (Full vs Tracking) | timeseries | `rate(xrpld_State_Accounting_Full_duration[5m]) / 1000000`      | —           |
| All Jobs Execution Time (Detail)       | timeseries | `{__name__=~"xrpld_<all_jobs>", quantile="$quantile"}`          | `quantile`  |
| All Jobs Dequeue Wait (Detail)         | timeseries | `{__name__=~"xrpld_<all_jobs>_q", quantile="$quantile"}`        | `quantile`  |

### Network Traffic -- System Metrics (`xrpld-system-network`)

| Panel                                | Type       | PromQL                                     | Labels Used |
| ------------------------------------ | ---------- | ------------------------------------------ | ----------- |
| Active Peers                         | timeseries | `xrpld_Peer_Finder_Active_*_Peers`         | —           |
| Peer Disconnects                     | timeseries | `xrpld_Overlay_Peer_Disconnects`           | —           |
| Total Network Bytes                  | timeseries | `rate(xrpld_total_Bytes_In/Out[5m])`       | —           |
| Total Network Messages               | timeseries | `xrpld_total_Messages_In/Out`              | —           |
| Transaction Traffic                  | timeseries | `xrpld_transactions_Messages_In/Out`       | —           |
| Proposal Traffic                     | timeseries | `xrpld_proposals_Messages_In/Out`          | —           |
| Validation Traffic                   | timeseries | `xrpld_validations_Messages_In/Out`        | —           |
| Traffic by Category                  | bargauge   | `topk(10, xrpld_*_Bytes_In)`               | —           |
| Duplicate Traffic (Wasted Bandwidth) | timeseries | `rate(xrpld_*_duplicate_Bytes_In/Out[5m])` | —           |
| All Traffic Categories (Detail)      | timeseries | `topk(15, rate(xrpld_*_Bytes_In[5m]))`     | —           |

### RPC & Pathfinding -- System Metrics (`xrpld-system-rpc`)

| Panel                     | Type       | PromQL                                                 | Labels Used |
| ------------------------- | ---------- | ------------------------------------------------------ | ----------- |
| RPC Request Rate          | stat       | `rate(xrpld_rpc_requests[5m])`                         | —           |
| RPC Response Time         | timeseries | `histogram_quantile(0.95, xrpld_rpc_time_bucket)`      | —           |
| RPC Response Size         | timeseries | `histogram_quantile(0.95, xrpld_rpc_size_bucket)`      | —           |
| RPC Response Time Heatmap | heatmap    | `xrpld_rpc_time_bucket`                                | —           |
| Pathfinding Fast Duration | timeseries | `histogram_quantile(0.95, xrpld_pathfind_fast_bucket)` | —           |
| Pathfinding Full Duration | timeseries | `histogram_quantile(0.95, xrpld_pathfind_full_bucket)` | —           |
| Resource Warnings Rate    | stat       | `rate(xrpld_warn[5m])`                                 | —           |
| Resource Drops Rate       | stat       | `rate(xrpld_drop[5m])`                                 | —           |

### Span → Metric → Dashboard Summary

| Span Name                      | Prometheus Metric Filter                     | Grafana Dashboard                             |
| ------------------------------ | -------------------------------------------- | --------------------------------------------- |
| `rpc.request`                  | `{span_name="rpc.request"}`                  | RPC Performance (Overall Throughput)          |
| `rpc.process`                  | `{span_name="rpc.process"}`                  | RPC Performance (Overall Throughput)          |
| `rpc.ws_message`               | `{span_name="rpc.ws_message"}`               | RPC Performance (WebSocket Rate)              |
| `rpc.command.*`                | `{span_name=~"rpc.command.*"}`               | RPC Performance (Rate, Latency, Error, Top)   |
| `tx.process`                   | `{span_name="tx.process"}`                   | Transaction Overview (Rate, Latency, Heatmap) |
| `tx.receive`                   | `{span_name="tx.receive"}`                   | Transaction Overview (Rate, Receive)          |
| `tx.apply`                     | `{span_name="tx.apply"}`                     | Transaction Overview + Ledger Ops (Apply)     |
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
| `consensus.accept`             | `{span_name="consensus.accept"}`             | Consensus Health (Duration, Rate, Heatmap)    |
| `consensus.proposal.send`      | `{span_name="consensus.proposal.send"}`      | Consensus Health (Proposals Rate)             |
| `consensus.ledger_close`       | `{span_name="consensus.ledger_close"}`       | Consensus Health (Close, Mode)                |
| `consensus.validation.send`    | `{span_name="consensus.validation.send"}`    | Consensus Health (Validation Rate)            |
| `consensus.accept.apply`       | `{span_name="consensus.accept.apply"}`       | Consensus Health (Apply Duration, Close Time) |
| `consensus.mode_change`        | `{span_name="consensus.mode_change"}`        | -- (available but not paneled)                |
| `consensus.proposal.receive`   | `{span_name="consensus.proposal.receive"}`   | -- (available but not paneled)                |
| `consensus.validation.receive` | `{span_name="consensus.validation.receive"}` | -- (available but not paneled)                |
| `ledger.build`                 | `{span_name="ledger.build"}`                 | Ledger Ops (Build Rate, Duration, Heatmap)    |
| `ledger.validate`              | `{span_name="ledger.validate"}`              | Ledger Ops (Validation Rate)                  |
| `ledger.store`                 | `{span_name="ledger.store"}`                 | Ledger Ops (Store Rate)                       |
| `peer.proposal.receive`        | `{span_name="peer.proposal.receive"}`        | Peer Network (Rate, Trusted/Untrusted)        |
| `peer.validation.receive`      | `{span_name="peer.validation.receive"}`      | Peer Network (Rate, Trusted/Untrusted)        |

## Log-Trace Correlation (Phase 8)

When xrpld is built with `telemetry=ON`, log lines emitted within an active OpenTelemetry span automatically include `trace_id` and `span_id` fields:

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
{job="xrpld"} |= "trace_id=abc123def456789012345678abcdef01"

# Error logs with trace context (log lines with ERR severity that have a trace_id)
{job="xrpld"} |= "ERR" |= "trace_id="

# All logs from a specific partition that were emitted during a span
{job="xrpld"} |= "LedgerMaster" | regexp `trace_id=(?P<trace_id>[a-f0-9]+)` | trace_id != ""

# Logs from the last hour containing trace context
{job="xrpld"} |= "trace_id=" | regexp `(?P<partition>\S+):(?P<sev>\S+)\s+trace_id=(?P<tid>[a-f0-9]+)`

# Count of traced vs untraced log lines
count_over_time({job="xrpld"} |= "trace_id=" [5m])
```

### Verifying Log Correlation

1. Start the observability stack and xrpld with telemetry enabled.
2. Send an RPC request: `curl http://localhost:5005 -d '{"method":"server_info"}'`
3. Check the debug.log for `trace_id=` entries: `grep trace_id= /path/to/debug.log`
4. Open Grafana at http://localhost:3000 -> Explore -> Loki and search for `{job="xrpld"} |= "trace_id="`.
5. Click the TraceID link to navigate to the corresponding trace in Tempo.

## Troubleshooting

### No traces appearing in Tempo

1. Check xrpld logs for `Telemetry starting` message
2. Verify `enabled=1` in the `[telemetry]` config section
3. Test collector connectivity: `curl -v http://localhost:4318/v1/traces`
4. Check collector logs: `docker compose -f docker/telemetry/docker-compose.yml logs otel-collector`
5. Verify Tempo is receiving data: open Grafana → Explore → select Tempo datasource → search by `service.name = xrpld`
6. Check Tempo logs: `docker compose -f docker/telemetry/docker-compose.yml logs tempo`

### No system metrics in Prometheus

1. Check xrpld logs for `OTelCollector starting` message
2. Verify `server=otel` in the `[insight]` config section
3. Verify the endpoint in `[insight]` points to the OTLP/HTTP port (default: `http://localhost:4318/v1/metrics`)
4. Check that the `otlp` receiver is in the metrics pipeline receivers in `otel-collector-config.yaml`
5. Query Prometheus directly: `curl 'http://localhost:9090/api/v1/query?query=xrpld_job_count'`

### High memory usage

- Reduce `sampling_ratio` (e.g., `0.1` for 10% sampling)
- Reduce `max_queue_size` and `batch_size`
- Disable high-volume trace categories: `trace_peer=0`

### Collector connection failures

- Verify endpoint URL matches collector address
- Check firewall rules for ports 4317/4318
- If using TLS, verify certificate path with `tls_ca_cert`

### No trace_id in log output

- Verify xrpld was built with `telemetry=ON` (the `XRPL_ENABLE_TELEMETRY` preprocessor flag)
- Verify `enabled=1` in the `[telemetry]` config section
- Log lines only contain `trace_id`/`span_id` when emitted inside an active span — background logs outside of RPC/consensus/transaction processing will not have trace context
- Check that the specific trace category is enabled (e.g., `trace_rpc=1`)

### No logs in Loki

- Verify the log file mount in docker-compose.yml points to the correct xrpld log directory
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
