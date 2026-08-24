# xrpld Telemetry Operator Runbook

## Overview

xrpld supports OpenTelemetry distributed tracing to provide visibility into RPC requests, transaction processing, and consensus rounds.

This runbook covers operating a running node and querying its traces. For
building xrpld with telemetry support and the internal architecture, see
[build/telemetry.md](build/telemetry.md).

## Quick Start

### 1. Start the observability stack

```bash
docker compose -f docker/telemetry/docker-compose.yml up -d
```

This starts:

- **OTel Collector** on ports 4317 (gRPC), 4318 (HTTP), and 13133 (health)
- **Tempo** trace storage on http://localhost:3200
- **Grafana** on http://localhost:3000 (Tempo pre-configured as datasource)

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
| `trace_rpc`                | `1`                               | Enable RPC request tracing                                |
| `trace_transactions`       | `1`                               | Enable transaction tracing                                |
| `trace_consensus`          | `1`                               | Enable consensus tracing                                  |
| `trace_peer`               | `1`                               | Enable peer message tracing (high volume)                 |
| `trace_ledger`             | `1`                               | Enable ledger tracing                                     |
| `consensus_trace_strategy` | `deterministic`                   | Consensus trace ID strategy (`deterministic` or `random`) |
| `batch_size`               | `512`                             | Max spans per batch export                                |
| `batch_delay_ms`           | `5000`                            | Delay between batch exports                               |
| `max_queue_size`           | `2048`                            | Max spans queued before dropping                          |
| `use_tls`                  | `0`                               | Use TLS for exporter connection                           |
| `tls_ca_cert`              | (empty)                           | Path to CA certificate bundle                             |
| `tls_client_cert`          | (empty)                           | Client cert (PEM) for mTLS; empty = one-way. See note     |
| `tls_client_key`           | (empty)                           | Private key (PEM) for `tls_client_cert`. See note         |

> **mTLS (mutual TLS) note**: `tls_client_cert` and `tls_client_key` are optional — leaving both empty gives one-way (server-only) TLS. **If either one is set**, `enabled=1` requires both of them **and** `use_tls=1`, or the node exits at startup; see the Troubleshooting entry for `Unable to start ...: [telemetry] ...`. When `enabled=0` they are read but never validated.

## Span Reference

All spans instrumented in xrpld, grouped by subsystem:

### RPC Spans

| Span Name            | Source File       | Attributes                                                  | Description                                           |
| -------------------- | ----------------- | ----------------------------------------------------------- | ----------------------------------------------------- |
| `rpc.http_request`   | ServerHandler.cpp | `request_payload_size`                                      | Top-level HTTP RPC request                            |
| `rpc.ws_upgrade`     | ServerHandler.cpp | —                                                           | WebSocket upgrade handshake                           |
| `rpc.ws_message`     | ServerHandler.cpp | `command`                                                   | WebSocket RPC message                                 |
| `rpc.process`        | ServerHandler.cpp | `is_batch`, `batch_size`                                    | RPC processing (child of rpc.http_request/ws_message) |
| `rpc.command.<name>` | RPCHandler.cpp    | `command`, `version`, `rpc_role`, `rpc_status`, `load_type` | Per-command span (e.g., `rpc.command.server_info`)    |

### Transaction Spans

| Span Name       | Source File     | Attributes                                                                                              | Description                           |
| --------------- | --------------- | ------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| `tx.process`    | NetworkOPs.cpp  | `tx_hash`, `local`, `path`, `tx_type`, `fee`, `sequence`, `ter_result`, `applied`, `current_ledger_seq` | Transaction submission and processing |
| `tx.receive`    | PeerImp.cpp     | `peer_id`, `tx_hash`, `tx_type`, `peer_version`, `suppressed`, `tx_status`, `current_ledger_seq`        | Transaction received from peer relay  |
| `tx.apply`      | BuildLedger.cpp | `ledger_seq`, `tx_count`, `tx_failed`                                                                   | Transaction set applied per ledger    |
| `tx.preflight`  | applySteps.cpp  | `stage`, `tx_type`, `ter_result`                                                                        | Stateless checks stage                |
| `tx.preclaim`   | applySteps.cpp  | `stage`, `tx_type`, `ter_result`, `current_ledger_seq`, `current_ledger_hash`                           | Ledger-aware checks stage             |
| `tx.transactor` | Transactor.cpp  | `stage`, `tx_type`, `ter_result`, `applied`, `current_ledger_seq`, `current_ledger_hash`                | Apply stage (transactor runs)         |

The three apply-pipeline spans (`tx.preflight`, `tx.preclaim`, `tx.transactor`)
share a deterministic `trace_id` from `txID[0:16]`, so they group under one
trace per transaction. The `stage` attribute (`preflight` / `preclaim` /
`apply`) drives the collector spanmetrics `stage` dimension, giving per-stage
RED metrics on the _Transaction Overview_ dashboard.

`current_ledger_seq` is the current (open/in-flight) ledger index a span acted on
— the ledger being worked on, not an established one — so a transaction's
txID-keyed spans can be joined to the ledger trace it targeted
(`span.current_ledger_seq`). The view-bearing stages (`tx.preclaim`,
`tx.transactor`) also carry `current_ledger_hash` (the current ledger's parent
hash); `tx.preflight` is stateless and omits both.

### Transaction Queue Spans

| Span Name          | Source File | Attributes                                                        | Description                                                                                                                                                                                  |
| ------------------ | ----------- | ----------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `txq.enqueue`      | TxQ.cpp     | `tx_hash`, `tx_type`, `current_ledger_seq`, `current_ledger_hash` | Enqueue decision; parents to `tx.process` on the submission path (explicit context), a root on the open-ledger rebuild path — `current_ledger_seq` correlates it to the ledger in both cases |
| `txq.apply_direct` | TxQ.cpp     | --                                                                | Direct apply attempt (bypassing queue)                                                                                                                                                       |
| `txq.batch_clear`  | TxQ.cpp     | --                                                                | Batch clear of queued transactions for an account                                                                                                                                            |
| `txq.accept`       | TxQ.cpp     | `queue_size`, `ledger_changed`                                    | Ledger-close accept loop over queued transactions                                                                                                                                            |
| `txq.accept_tx`    | TxQ.cpp     | `tx_hash`, `retries_remaining`, `ter_code`, `txq_status`          | Per-transaction apply during accept                                                                                                                                                          |
| `txq.cleanup`      | TxQ.cpp     | `ledger_seq`                                                      | Post-close cleanup of expired queue entries                                                                                                                                                  |

### PathFinding Spans

| Span Name             | Source File                       | Attributes                                         | Description                                             |
| --------------------- | --------------------------------- | -------------------------------------------------- | ------------------------------------------------------- |
| `pathfind.request`    | PathFind.cpp / RipplePathFind.cpp | `pathfind_source_account`, `pathfind_dest_account` | Path-find RPC entry (accounts hashed; set when present) |
| `pathfind.compute`    | PathRequest.cpp                   | `pathfind_fast`, `pathfind_dest_currency`          | Path computation for one request (`doUpdate`)           |
| `pathfind.discover`   | PathRequest.cpp                   | `pathfind_search_level`, `pathfind_num_paths`      | Graph exploration (one per RPC call in `findPaths`)     |
| `pathfind.update_all` | PathRequestManager.cpp            | `pathfind_ledger_index`, `pathfind_num_requests`   | Async recomputation of active requests on ledger close  |

### Consensus Spans

| Span Name                      | Source File      | Attributes                                                                                                                                                                                                                   | Description                                                                                                                           |
| ------------------------------ | ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| `consensus.round`              | RCLConsensus.cpp | `consensus_ledger_id`, `ledger_seq`, `consensus_mode`, `trace_strategy`, `consensus_round_id`                                                                                                                                | Root span for a consensus round (deterministic or random trace ID)                                                                    |
| `consensus.phase.open`         | Consensus.h      | --                                                                                                                                                                                                                           | Open phase duration (child of round)                                                                                                  |
| `consensus.proposal.send`      | RCLConsensus.cpp | `consensus_round`, `is_bow_out`                                                                                                                                                                                              | Consensus proposal broadcast                                                                                                          |
| `consensus.ledger_close`       | RCLConsensus.cpp | `ledger_seq`, `consensus_mode`                                                                                                                                                                                               | Ledger close event                                                                                                                    |
| `consensus.establish`          | Consensus.h      | `converge_percent`, `establish_count`, `proposers`                                                                                                                                                                           | Establish phase duration (child of round)                                                                                             |
| `consensus.update_positions`   | Consensus.h      | `converge_percent`, `proposers`, `disputes_count`                                                                                                                                                                            | Position update and dispute resolution (see Events below)                                                                             |
| `consensus.check`              | Consensus.h      | `agree_count`, `disagree_count`, `converge_percent`, `have_close_time_consensus`, `threshold_percent`, `proposers_finished`, `consensus_stalled`, `establish_count`, `consensus_result`                                      | Consensus threshold check                                                                                                             |
| `consensus.accept`             | RCLConsensus.cpp | `proposers`, `round_time_ms`, `quorum`, `disputes_count`, `consensus_state`                                                                                                                                                  | Ledger accepted by consensus                                                                                                          |
| `consensus.accept.apply`       | RCLConsensus.cpp | `ledger_seq`, `close_time`, `close_time_correct`, `close_resolution_ms`, `consensus_state`, `proposing`, `round_time_ms`, `parent_close_time`, `close_time_self`, `close_time_vote_bins`, `resolution_direction`, `tx_count` | Ledger application with close time details (see Events below)                                                                         |
| `consensus.validation.send`    | RCLConsensus.cpp | `ledger_seq`, `proposing`, `ledger_hash`, `full_validation`, `validation_sign_time`                                                                                                                                          | Validation sent after accept (follows-from link)                                                                                      |
| `consensus.mode_change`        | RCLConsensus.cpp | `mode_old`, `mode_new`                                                                                                                                                                                                       | Consensus mode transition                                                                                                             |
| `consensus.proposal.receive`   | PeerImp.cpp      | `proposal_trusted`, `consensus_round`                                                                                                                                                                                        | Proposal received from peer (extracts parent context from TraceContext when present; falls back to standalone span for older peers)   |
| `consensus.validation.receive` | PeerImp.cpp      | `validation_trusted`, `ledger_seq`                                                                                                                                                                                           | Validation received from peer (extracts parent context from TraceContext when present; falls back to standalone span for older peers) |

#### Consensus Span Events

| Parent Span                  | Event Name        | Event Attributes                                            | Description                                             |
| ---------------------------- | ----------------- | ----------------------------------------------------------- | ------------------------------------------------------- |
| `consensus.update_positions` | `dispute.resolve` | `tx_id`, `dispute_our_vote`, `dispute_yays`, `dispute_nays` | Emitted per dispute when votes are tallied              |
| `consensus.accept.apply`     | `tx.included`     | `tx_id`                                                     | Emitted per transaction included in the accepted ledger |

#### Close Time Queries (Tempo TraceQL)

```
# Find rounds where validators disagreed on close time
{name="consensus.accept.apply"} | close_time_correct = false

# Find consensus failures (moved_on)
{name="consensus.accept.apply"} | consensus_state = "moved_on"

# Find slow ledger applications (>5s)
{name="consensus.accept.apply"} | duration > 5s

# Find specific ledger's consensus details
{name="consensus.accept.apply"} | ledger_seq = 92345678

# Find all spans in a consensus round (deterministic trace strategy).
# consensus_round_id is an int64 — the previous ledger sequence plus one.
{name="consensus.round"} | consensus_round_id = 92345679

# Find dispute resolutions
{name="consensus.update_positions"} >> {event:name="dispute.resolve"}
```

### Ledger Spans

| Span Name         | Source File          | Attributes                            | Description                   |
| ----------------- | -------------------- | ------------------------------------- | ----------------------------- |
| `ledger.build`    | BuildLedger.cpp:31   | `ledger_seq`, `tx_count`, `tx_failed` | Ledger build during consensus |
| `ledger.validate` | LedgerMaster.cpp:915 | `ledger_seq`, `validations`           | Ledger promoted to validated  |
| `ledger.store`    | LedgerMaster.cpp:409 | `ledger_seq`                          | Ledger stored in history      |

### Peer Spans

| Span Name                 | Source File      | Attributes                      | Description                   |
| ------------------------- | ---------------- | ------------------------------- | ----------------------------- |
| `peer.proposal.receive`   | PeerImp.cpp:1667 | `peer_id`, `proposal_trusted`   | Proposal received from peer   |
| `peer.validation.receive` | PeerImp.cpp:2264 | `peer_id`, `validation_trusted` | Validation received from peer |

Both peer receive spans are `kConsumer` inbound entry points started as fresh
trace roots. They never inherit an ambient span left active on the peer thread,
so they do not nest under an unrelated transaction's trace. The distributed
child span that links back to the sending node is the separate
`consensus.*.receive` / `tx.receive` span (see Cross-Node Trace Propagation).

---

## Insights and Sample Queries

This section shows what questions you can answer using the span attributes, with example Tempo TraceQL queries.

### Transaction Workflow Analysis

```
# Find all AMM transactions (AMMDeposit, AMMWithdraw, AMMCreate, etc.)
{name="tx.process"} | tx_type =~ "AMM.*"

# Find Payment transactions that failed
{name="tx.process"} | tx_type = "Payment" && ter_result != "tesSUCCESS"

# Compare latency of different transaction types
{name="tx.process"} | tx_type = "OfferCreate"
{name="tx.process"} | tx_type = "Payment"

# Find high-fee transactions (fee > 1 XRP = 1000000 drops)
{name="tx.process"} | fee > 1000000

# Find transactions that were not applied
{name="tx.process"} | applied = false

# Trace a specific transaction by type across the network
{name=~"tx\\..*"} | tx_type = "NFTokenMint"
```

### Apply Pipeline by Stage

```
# All three stages of one transaction (preflight -> preclaim -> apply)
{name=~"tx.preflight|tx.preclaim|tx.transactor"}

# Transactions that failed at the preclaim stage
{name="tx.preclaim"} | ter_result != "tesSUCCESS"

# Transactions that hard-failed preflight (never reached preclaim/apply)
{name="tx.preflight"} | ter_result != "tesSUCCESS"
```

PromQL on the span-derived metrics (dashboard: _Transaction Overview_):

```
# Per-stage throughput — the funnel preflight >= preclaim >= apply
sum by (stage) (rate(traces_span_metrics_calls_total{span_name=~"tx.preflight|tx.preclaim|tx.transactor"}[5m]))

# Per-stage p95 latency
histogram_quantile(0.95, sum by (le, stage) (rate(traces_span_metrics_duration_milliseconds_bucket{span_name=~"tx.preflight|tx.preclaim|tx.transactor"}[5m])))

# Per-stage failure rate (ter_result != tesSUCCESS; a failing ter completes the
# span normally, so filter on the attribute, not status_code which only flags exceptions)
sum by (stage) (rate(traces_span_metrics_calls_total{span_name=~"tx.preflight|tx.preclaim|tx.transactor", ter_result!~"tesSUCCESS|"}[5m]))
```

> **Alerting**: a rising `tx.preflight` / `tx.preclaim` failure rate points to
> malformed or stale-sequence submissions (often spam or a misbehaving client);
> a rising `tx.transactor` failure rate points to apply-time problems. Alert per
> stage rather than on a single aggregate so the failing stage is obvious.

> **Sampling caveat**: these stage metrics are span-derived, so they count only
> the spans the collector's spanmetrics connector sees. Head sampling at the node
> is fixed at 1.0 and is not configurable (`Telemetry.h`), and the shipped
> collector pipeline has no tail sampling, so today nothing is dropped and the
> counts are absolute. Volume reduction is delegated to the collector: adding a
> tail-sampling processor to the traces pipeline puts it ahead of the spanmetrics
> connector, and these metrics would then undercount proportionally — treat them
> as relative trends in that case. Native StatsD metrics are never sampled.

### Transaction Queue Health

```
# Find transactions rejected from the queue
{name="txq.accept_tx"} | txq_status = "failed"

# Which transaction types get queued most often?
{name="txq.enqueue"} | tx_type = "Payment"
{name="txq.enqueue"} | tx_type = "OfferCreate"

# Find ledger closes that applied queued transactions
{name="txq.accept"} | ledger_changed = true

# Find transactions that exhausted retries
{name="txq.accept_tx"} | txq_status = "retried" && retries_remaining = 0
```

### RPC Debugging

```
# Find batch RPC requests
{name="rpc.process"} | is_batch = true

# Find large RPC payloads (>100KB)
{name="rpc.http_request"} | request_payload_size > 100000

# Find resource-heavy RPC commands (by load_type)
{name=~"rpc.command.*"} | load_type = "exceptioned RPC"

# Find a specific WebSocket command
{name="rpc.ws_message"} | command = "subscribe"

# Find slow pathfinding with many source assets
{name="pathfind.discover"} | pathfind_num_source_assets > 10
```

### PathFinding Performance

```
# Find pathfinding for specific currencies
{name="pathfind.compute"} | pathfind_dest_currency = "USD"

# Find expensive pathfinding (many source assets to explore)
{name="pathfind.discover"} | pathfind_num_source_assets > 20

# Find large pathfinding requests
{name="pathfind.compute"} | duration > 1s
```

### Consensus Health

```
# Find rounds where consensus timed out (expired)
{name="consensus.accept"} | consensus_state = "expired"

# Find rounds where we moved on without full agreement
{name="consensus.accept"} | consensus_state = "moved_on"

# Find rounds with many disputes
{name="consensus.accept"} | disputes_count > 5

# Find bow-out proposals (node resigned from round)
{name="consensus.proposal.send"} | is_bow_out = true

# Correlate validation with its ledger
{name="consensus.validation.send"} | ledger_hash = "<hash>"

# Find rounds where validators disagreed on close time
{name="consensus.accept.apply"} | close_time_correct = false
```

### Cross-Subsystem Correlation

```
# Follow a transaction from receive through queue to ledger
{name=~"tx\\..*|txq\\..*"} | tx_type = "Payment" && duration > 500ms

# Find all NFT-related activity
{name=~"tx\\..*|txq\\..*"} | tx_type =~ "NFToken.*"

# Find consensus rounds with slow transactions
{name="consensus.accept"} | round_time_ms > 5000
```

### Where to Look (Quick Reference)

| Question                            | Span                        | Key Attributes                 |
| ----------------------------------- | --------------------------- | ------------------------------ |
| "Which tx type is slowest?"         | `tx.process`                | `tx_type` + duration           |
| "Why was my tx rejected?"           | `tx.process`                | `ter_result`, `applied`        |
| "Is the TxQ backing up?"            | `txq.accept`                | `queue_size`, `ledger_changed` |
| "Why was my tx dropped from queue?" | `txq.accept_tx`             | `txq_status`, `ter_code`       |
| "Are batch requests a problem?"     | `rpc.process`               | `is_batch`, `batch_size`       |
| "Which RPC is expensive?"           | `rpc.command.*`             | `load_type`, duration          |
| "Did consensus stall?"              | `consensus.check`           | `consensus_stalled`            |
| "Was consensus outcome normal?"     | `consensus.accept`          | `consensus_state`              |
| "Did a validator bow out?"          | `consensus.proposal.send`   | `is_bow_out`                   |
| "Which ledger was validated?"       | `consensus.validation.send` | `ledger_hash`                  |
| "What tx work fed a given ledger?"  | `tx.*` / `txq.*`            | `current_ledger_seq`           |

### Correlating a transaction to the ledger it was worked on

The `tx.process`, `tx.receive`, `txq.enqueue`, `tx.preclaim`, and `tx.transactor`
spans carry `current_ledger_seq` — the open/in-flight ledger they acted on (not
an established ledger). Because these spans are keyed on the transaction id (their
own trace) while the ledger/consensus spans are keyed on the ledger, use the
attribute to bridge the two id-spaces:

```
# All transaction-side work recorded against ledger N
{span.current_ledger_seq = N}

# Join to the ledger build/consensus trace for the same ledger
{name="ledger.build" && span.ledger_seq = N}
```

`txq.enqueue` and the view-bearing apply stages also carry `current_ledger_hash`
(the current ledger's parent hash), which equals the `consensus.round`
deterministic trace-id seed on the consensus-build path. `tx.preflight` is
stateless and omits both attributes.

---

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
{} >> {name="consensus.proposal.receive"}

# Trace a transaction across the network by its hash
{name=~"tx\\..*"} | tx_hash = "<hash>"

# Find all spans in a cross-node consensus trace
{rootServiceName="xrpld"} | consensus_round_id = 92345679

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

Additionally, span attributes configured as dimensions in the collector
become metric labels. The span attribute keys are already underscore form
(the naming convention forbids dots), so the label name matches the attribute
name verbatim. Prometheus' dots → underscores sanitization only fires for
dotted attribute names (e.g. resource attributes like `service.name`), which
does not apply to these dimensions.

| Span Attribute       | Metric Label         | Applies To                      |
| -------------------- | -------------------- | ------------------------------- |
| `command`            | `command`            | `rpc.command.*` spans           |
| `rpc_status`         | `rpc_status`         | `rpc.command.*` spans           |
| `consensus_mode`     | `consensus_mode`     | `consensus.ledger_close` spans  |
| `local`              | `local`              | `tx.process` spans              |
| `proposal_trusted`   | `proposal_trusted`   | `peer.proposal.receive` spans   |
| `validation_trusted` | `validation_trusted` | `peer.validation.receive` spans |

### Histogram Buckets

Configured in `otel-collector-config.yaml`:

```
1ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1s, 5s
```

## StatsD Metrics (beast::insight)

xrpld has a built-in metrics framework (`beast::insight`) that emits StatsD-format metrics over UDP. These complement the span-derived RED metrics by providing system-level gauges, counters, and timers that don't map to individual trace spans.

### Configuration

Add to `xrpld.cfg`:

```ini
[insight]
server=statsd
address=127.0.0.1:8125
prefix=xrpld
```

The OTel Collector receives these via a `statsd` receiver on UDP port 8125 and exports them to Prometheus alongside spanmetrics.

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
| `xrpld_jobq_job_count`                      | JobQueue.cpp:26           | Current job queue depth                                                    |
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

#### Histograms (from StatsD timers)

| Prometheus Metric     | Source                | Description                    |
| --------------------- | --------------------- | ------------------------------ |
| `xrpld_rpc_time`      | ServerHandler.cpp:110 | RPC response time (ms)         |
| `xrpld_rpc_size`      | ServerHandler.cpp:109 | RPC response size (bytes)      |
| `xrpld_ios_latency`   | Application.cpp:438   | I/O service loop latency (ms)  |
| `xrpld_pathfind_fast` | PathRequests.h:23     | Fast pathfinding duration (ms) |
| `xrpld_pathfind_full` | PathRequests.h:24     | Full pathfinding duration (ms) |

## Deployment Tiers

Multiple xrpld instances can send telemetry to per-tier collectors that all
forward to one Grafana stack. Four resource attributes segregate the data so
one dashboard set serves every deployment:

| Dimension   | Attribute                | Set by     | Example values                 |
| ----------- | ------------------------ | ---------- | ------------------------------ |
| Node        | `service.instance.id`    | xrpld cfg  | `alice-laptop`, `ci-runner-7`  |
| Service     | `service.name`           | xrpld cfg  | `xrpld`, `xrpld-validator`     |
| Network     | `xrpl.network.type`      | xrpld node | `mainnet`, `testnet`, `devnet` |
| Environment | `deployment.environment` | collector  | `local`, `test`, `ci`, `prod`  |

Dashboards expose these as the template variables `$node`, `$service_name`,
`$xrpl_network_type`, and `$deployment_environment` (each variable name
matches its Prometheus label). Select them top-down — environment → network
→ service → node. Selecting **All** matches every value, including series
lacking the label, so mixed old/new data never disappears.

### Who owns which attribute

- **Node and service** come from xrpld config (`service_instance_id`,
  `service_name`). Unique per process.
- **Network** is a property of the chain the node joined; the node derives it
  from `[network_id]` and stamps `xrpl.network.type` on all three signals.
- **Environment** is a property of where the collector runs; each collector
  serves one environment and stamps it.

### The upsert vs insert rule

The collector's `resource/tier` processor uses two actions on purpose:

- `deployment.environment` → **`upsert`** (overwrite). The collector _is_ the
  environment, so it is authoritative.
- `xrpl.network.type` → **`insert`** (fill only if absent). The node knows
  its real network, so the collector must not overwrite it — `insert` only
  supplies a value when the source did not (e.g. an older xrpld build). This
  is what lets a local node connected to mainnet report `network=mainnet`,
  not the collector's default.

### Configuring a collector for a tier

Each tier runs its own collector. Set the two values in the `resource/tier`
processor of the collector config (`otel-collector-config.yaml` for local
backends, `otel-collector-config.grafanacloud.yaml` for Grafana Cloud):

```yaml
processors:
  resource/tier:
    attributes:
      - key: deployment.environment
        value: <tier> # local | test | ci | prod
        action: upsert
      - key: xrpl.network.type
        value: <network> # mainnet | testnet | devnet (fallback only)
        action: insert
```

Suggested per-tier values:

| Collector           | `deployment.environment` | `xrpl.network.type` (fallback) |
| ------------------- | ------------------------ | ------------------------------ |
| Developer laptop    | `local`                  | `devnet`                       |
| Test machines       | `test`                   | `testnet`                      |
| CI runs             | `ci`                     | `testnet`                      |
| Production observer | `prod`                   | `mainnet`                      |

The `xrpl.network.type` value is only a fallback: when the node stamps its
own network (all current builds do), the node's value wins. Set it to the
network the collector most commonly serves.

### How the tier labels reach metrics

Resource attributes do not become Prometheus labels automatically. Two
collector settings make it work, both already enabled:

- `prometheus.resource_to_telemetry_conversion: enabled: true` promotes
  resource attributes to metric labels on the local scrape surface.
- `spanmetrics.resource_metrics_key_attributes` lists the tier attributes so
  span-derived series stay grouped per node and tier.

Traces and logs carry resource attributes natively; Grafana Cloud ingests all
three signals' attributes over OTLP directly.

## Grafana Dashboards

Ten dashboards are pre-provisioned in `docker/telemetry/grafana/dashboards/`:

### RPC Performance (`rpc-performance`)

| Panel                       | Type       | PromQL                                                                                                                                    | Labels Used              |
| --------------------------- | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------- | ------------------------ |
| RPC Request Rate by Command | timeseries | `sum by (command) (rate(traces_span_metrics_calls_total{span_name=~"rpc.command.*"}[5m]))`                                                | `command`                |
| RPC Latency p95 by Command  | timeseries | `histogram_quantile(0.95, sum by (le, command) (rate(traces_span_metrics_duration_milliseconds_bucket{span_name=~"rpc.command.*"}[5m])))` | `command`                |
| RPC Error Rate              | bargauge   | Error spans / total spans × 100, grouped by `command`                                                                                     | `command`, `status_code` |
| RPC Latency Heatmap         | heatmap    | `sum(increase(traces_span_metrics_duration_milliseconds_bucket{span_name=~"rpc.command.*"}[5m])) by (le)`                                 | `le` (bucket boundaries) |
| Overall RPC Throughput      | timeseries | `rpc.http_request` + `rpc.process` rate                                                                                                   | —                        |
| RPC Success vs Error        | timeseries | by `status_code` (UNSET vs ERROR)                                                                                                         | `status_code`            |
| Top Commands by Volume      | bargauge   | `topk(10, ...)` by `command`                                                                                                              | `command`                |
| WebSocket Message Rate      | stat       | `rpc.ws_message` rate                                                                                                                     | —                        |

### Transaction Overview (`transaction-overview`)

| Panel                              | Type           | PromQL                                                                                              | Labels Used                         |
| ---------------------------------- | -------------- | --------------------------------------------------------------------------------------------------- | ----------------------------------- |
| Transaction Processing Rate        | timeseries     | `rate(traces_span_metrics_calls_total{span_name="tx.process"}[5m])` and `tx.receive`                | `span_name`                         |
| Transaction Processing Latency     | timeseries     | `histogram_quantile(0.95 / 0.50, ... {span_name="tx.process"})`                                     | —                                   |
| Transaction Path Distribution      | piechart       | `sum by (local) (increase(traces_span_metrics_calls_total{span_name="tx.process"}[5m]))`            | `local`                             |
| Transaction Receive vs Suppressed  | timeseries     | `rate(traces_span_metrics_calls_total{span_name="tx.receive"}[5m])`                                 | —                                   |
| TX Processing Duration Heatmap     | heatmap        | `tx.process` histogram buckets                                                                      | `le`                                |
| TX Apply Duration per Ledger       | timeseries     | p95/p50 of `tx.apply`                                                                               | —                                   |
| Peer TX Receive Rate               | timeseries     | `tx.receive` rate                                                                                   | —                                   |
| TX Apply Failed Rate               | stat           | `tx.apply` with `STATUS_CODE_ERROR`                                                                 | `status_code`                       |
| TxQ Accept: Applied Ratio per Node | state-timeline | applied / (applied+failed) of `traces_span_metrics_calls_total{span_name="txq.accept_tx"}` per node | `txq_status`, `service_instance_id` |

### Consensus Health (`consensus-health`)

| Panel                         | Type       | PromQL                                                                             | Labels Used      |
| ----------------------------- | ---------- | ---------------------------------------------------------------------------------- | ---------------- |
| Consensus Round Duration      | timeseries | `histogram_quantile(0.95 / 0.50, ... {span_name="consensus.accept"})`              | —                |
| Consensus Proposals Sent Rate | timeseries | `rate(traces_span_metrics_calls_total{span_name="consensus.proposal.send"}[5m])`   | —                |
| Ledger Close Duration         | timeseries | `histogram_quantile(0.95, ... {span_name="consensus.ledger_close"})`               | —                |
| Validation Send Rate          | stat       | `rate(traces_span_metrics_calls_total{span_name="consensus.validation.send"}[5m])` | —                |
| Ledger Apply Duration         | timeseries | `histogram_quantile(0.95 / 0.50, ... {span_name="consensus.accept.apply"})`        | —                |
| Close Time Agreement          | timeseries | `rate(traces_span_metrics_calls_total{span_name="consensus.accept.apply"}[5m])`    | —                |
| Consensus Mode Over Time      | timeseries | `consensus.ledger_close` by `consensus_mode`                                       | `consensus_mode` |
| Accept vs Close Rate          | timeseries | `consensus.accept` vs `consensus.ledger_close` rate                                | —                |
| Validation vs Close Rate      | timeseries | `consensus.validation.send` vs `consensus.ledger_close`                            | —                |
| Accept Duration Heatmap       | heatmap    | `consensus.accept` histogram buckets                                               | `le`             |

### Ledger Operations (`ledger-operations`)

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

### Peer Network (`peer-network`)

Requires `trace_peer=1` in the `[telemetry]` config section.

| Panel                            | Type       | PromQL                                                                    | Labels Used          |
| -------------------------------- | ---------- | ------------------------------------------------------------------------- | -------------------- |
| Proposal Receive Rate            | timeseries | `peer.proposal.receive` rate                                              | —                    |
| Validation Receive Rate          | timeseries | `peer.validation.receive` rate                                            | —                    |
| Proposals Trusted vs Untrusted   | piechart   | `increase()` counts in the selected window, split by `proposal_trusted`   | `proposal_trusted`   |
| Validations Trusted vs Untrusted | piechart   | `increase()` counts in the selected window, split by `validation_trusted` | `validation_trusted` |

### Node Health -- StatsD (`xrpld-statsd-node-health`)

| Panel                                  | Type       | PromQL                                                          | Labels Used |
| -------------------------------------- | ---------- | --------------------------------------------------------------- | ----------- |
| Validated Ledger Age                   | stat       | `xrpld_LedgerMaster_Validated_Ledger_Age`                       | —           |
| Published Ledger Age                   | stat       | `xrpld_LedgerMaster_Published_Ledger_Age`                       | —           |
| Operating Mode Duration                | timeseries | `xrpld_State_Accounting_*_duration`                             | —           |
| Operating Mode Transitions             | timeseries | `xrpld_State_Accounting_*_transitions`                          | —           |
| I/O Latency                            | timeseries | `histogram_quantile(0.95, xrpld_ios_latency_bucket)`            | —           |
| Job Queue Depth                        | timeseries | `xrpld_jobq_job_count`                                          | —           |
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

### Network Traffic -- StatsD (`xrpld-statsd-network`)

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

### RPC & Pathfinding -- StatsD (`xrpld-statsd-rpc`)

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
| `rpc.http_request`             | `{span_name="rpc.http_request"}`             | RPC Performance (Overall Throughput)          |
| `rpc.ws_upgrade`               | `{span_name="rpc.ws_upgrade"}`               | -- (available but not paneled)                |
| `rpc.ws_message`               | `{span_name="rpc.ws_message"}`               | RPC Performance (WebSocket Rate)              |
| `rpc.process`                  | `{span_name="rpc.process"}`                  | RPC Performance (Overall Throughput)          |
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

## Troubleshooting

### No traces appearing in Tempo

1. Check xrpld logs for `Telemetry starting` message
2. Verify `enabled=1` in the `[telemetry]` config section
3. Test collector connectivity: `curl -v http://localhost:4318/v1/traces`
4. Check collector logs: `docker compose -f docker/telemetry/docker-compose.yml logs otel-collector`
5. Verify Tempo is receiving data: open Grafana → Explore → select Tempo datasource → search by `service.name = xrpld`
6. Check Tempo logs: `docker compose -f docker/telemetry/docker-compose.yml logs tempo`

### High memory usage

- Reduce trace volume with collector-side tail sampling (xrpld head sampling is
  fixed at 1.0 and is not configurable)
- Reduce `max_queue_size` and `batch_size`
- Disable high-volume trace categories: `trace_peer=0`

### Collector connection failures

- Verify endpoint URL matches collector address
- Check firewall rules for ports 4317/4318
- If using TLS, verify certificate path with `tls_ca_cert`

### Node exits at startup with `Unable to start ...: [telemetry] ...`

- Symptom: the process exits immediately with a non-zero status (255 on POSIX)
  — a clean exit, not a crash — after printing that line on stderr. Any
  exception thrown while the `Application` object is constructed prints the same
  `Unable to start` prefix, so confirm the text after the colon begins with
  `[telemetry]` before using this entry
- Cause: either the `[telemetry]` mTLS keys (`tls_client_cert` and
  `tls_client_key`) contradict each other, or one of the TLS certificate paths
  cannot be read. Only these three checks are gated on `enabled=1`; the rest of
  the section is still read when telemetry is off, so a malformed value in any
  key — including `enabled` itself, which is read before the gate — still fails
  startup with a different message
- Fix: the three checks need different remedies, and the printed message says
  which one fired
  - `tls_client_cert and tls_client_key must be set together` — exactly one of
    the two paths is set. Either delete the one that is set, or add the missing
    one **and** set `use_tls=1`. Unless `use_tls=1` is already set, adding the
    missing path on its own just moves the failure to the second check
  - `tls_client_cert/tls_client_key require use_tls=1` — both paths are set but
    TLS is off. Either set `use_tls=1`, or delete **both** paths. Deleting only
    one of them trips the first check
  - `<key> cannot be read` — the named key (`tls_ca_cert`, `tls_client_cert` or
    `tls_client_key`) points at a file the node cannot open; the message also
    prints the path and the OS error. Fix the path or its permissions — the
    pairing is not what is wrong here. This check runs only when `use_tls=1`,
    and an empty `tls_ca_cert` is always accepted (it selects the system CA
    store)
  - If you did not mean to enable telemetry at all, set `enabled=0` — that
    clears all three checks whichever one fired

## Performance Tuning

| Scenario                 | Recommendation                                            |
| ------------------------ | --------------------------------------------------------- |
| Production mainnet       | `trace_peer=0`; reduce volume via collector tail sampling |
| Testnet/devnet           | Full tracing (head sampling fixed at 1.0)                 |
| Debugging specific issue | Full tracing (head sampling fixed at 1.0)                 |
| High-throughput node     | Increase `batch_size=1024`, `max_queue_size=4096`         |

## Disabling Telemetry

Set `enabled=0` in config (runtime disable) or build without the flag:

```bash
cmake --preset default -Dtelemetry=OFF
```

When telemetry is compiled out, all trace macros expand to no-ops with zero overhead.
