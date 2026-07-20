# Observability Data Collection Reference

> **Audience**: Developers and operators. This is the single source of truth for all telemetry data collected by xrpld's observability stack.
>
> **Related docs**: [docs/telemetry-runbook.md](../docs/telemetry-runbook.md) (operator runbook with alerting and troubleshooting) | [03-implementation-strategy.md](./03-implementation-strategy.md) (code structure and performance optimization) | [04-code-samples.md](./04-code-samples.md) (C++ instrumentation examples)

## Data Flow Overview

```mermaid
graph LR
    subgraph xrpldNode["xrpld Node"]
        A["Trace Macros<br/>XRPL_TRACE_SPAN<br/>(OTLP/HTTP exporter)"]
        B["beast::insight<br/>OTel native metrics<br/>(OTLP/HTTP exporter)"]
        C["MetricsRegistry<br/>OTel SDK metrics<br/>(OTLP/HTTP exporter)"]
    end

    subgraph collector["OTel Collector  :4317 / :4318"]
        direction TB
        R1["OTLP Receiver<br/>:4317 gRPC  |  :4318 HTTP<br/>(traces + metrics)"]
        BP["Batch Processor<br/>timeout 1s, batch 100"]
        SM["SpanMetrics Connector<br/>derives RED metrics<br/>from trace spans"]

        R1 --> BP
        BP --> SM
    end

    subgraph backends["Trace Backend"]
        D["Grafana Tempo  :3200<br/>TraceQL search &<br/>S3/GCS long-term storage"]
    end

    subgraph metrics["Metrics Stack"]
        E["Prometheus  :9090<br/>scrapes :8889<br/>span-derived + system metrics"]
    end

    subgraph viz["Visualization"]
        F["Grafana  :3000<br/>13 dashboards"]
    end

    A -->|"OTLP/HTTP :4318<br/>(traces + attributes)"| R1
    B -->|"OTLP/HTTP :4318<br/>(gauges, counters, histograms)"| R1
    C -->|"OTLP/HTTP :4318<br/>(counters, histograms,<br/>observable gauges)"| R1

    BP -->|"OTLP/gRPC :4317"| D

    SM -->|"span_calls_total<br/>span_duration_ms<br/>(6 dimension labels)"| E
    R1 -->|"gauges, counters,<br/>histograms (OTLP)"| E

    E -->|"Prometheus<br/>data source"| F
    D -->|"Tempo<br/>data source"| F

    style A fill:#4a90d9,color:#fff,stroke:#2a6db5
    style B fill:#4a90d9,color:#fff,stroke:#2a6db5
    style R1 fill:#5cb85c,color:#fff,stroke:#3d8b3d
    style BP fill:#449d44,color:#fff,stroke:#2d6e2d
    style SM fill:#449d44,color:#fff,stroke:#2d6e2d
    style D fill:#f0ad4e,color:#000,stroke:#c78c2e
    style E fill:#f0ad4e,color:#000,stroke:#c78c2e
    style F fill:#5bc0de,color:#000,stroke:#3aa8c1
    style xrpldNode fill:#1a2633,color:#ccc,stroke:#4a90d9
    style collector fill:#1a3320,color:#ccc,stroke:#5cb85c
    style backends fill:#332a1a,color:#ccc,stroke:#f0ad4e
    style metrics fill:#332a1a,color:#ccc,stroke:#f0ad4e
    style viz fill:#1a2d33,color:#ccc,stroke:#5bc0de
```

There are two independent telemetry pipelines entering a single **OTel Collector** via the same OTLP receiver:

1. **OpenTelemetry Traces** — Distributed spans with attributes, exported via OTLP/HTTP (:4318) to the collector's **OTLP Receiver**. The **Batch Processor** groups spans (1s timeout, batch size 100) before forwarding to trace backends. The **SpanMetrics Connector** derives RED metrics (rate, errors, duration) from every span and feeds them into the metrics pipeline.
2. **beast::insight OTel Metrics** — System-level gauges, counters, and histograms exported natively via OTLP/HTTP (:4318) to the same **OTLP Receiver**. These are batched and exported to Prometheus alongside span-derived metrics. The StatsD UDP transport has been replaced by native OTLP; `server=statsd` remains available as a fallback.

**Trace backend** — The collector exports traces via OTLP/gRPC to:

- **Grafana Tempo** — Preferred trace backend. Supports TraceQL queries at `:3200`, S3/GCS object storage for cost-effective long-term trace retention, and integrates natively with Grafana.

> **Further reading**: [00-tracing-fundamentals.md](./00-tracing-fundamentals.md) for core OpenTelemetry concepts (traces, spans, context propagation, sampling). [07-observability-backends.md](./07-observability-backends.md) for production backend selection, collector placement, and sampling strategies.

---

## 1. OpenTelemetry Spans

### 1.1 Complete Span Inventory (~37 spans)

> **See also**: [02-design-decisions.md §2.3](./02-design-decisions.md#23-span-naming-conventions) for naming conventions and the full span catalog with rationale. [04-code-samples.md §4.6](./04-code-samples.md#46-span-flow-visualization) for span flow diagrams.

> **Span names vs. attribute keys**: span names use dotted `subsystem.operation`
> form (e.g. `rpc.http_request`). Span _attribute_ keys use the bare/underscore
> form from the 2026-05-13 naming redesign (e.g. `tx_hash`, not `xrpl.tx.hash`).
> The dotted `xrpl.*` form is reserved for OTel **resource** attributes set once
> at startup. See §1.2 for the full attribute inventory.

#### RPC Spans

Controlled by `trace_rpc=1` in `[telemetry]` config.

| Span Name            | Parent             | Source File       | Description                                                              |
| -------------------- | ------------------ | ----------------- | ------------------------------------------------------------------------ |
| `rpc.http_request`   | —                  | ServerHandler.cpp | Top-level HTTP JSON-RPC request entry point                              |
| `rpc.ws_message`     | —                  | ServerHandler.cpp | WebSocket message handling (one per inbound frame)                       |
| `rpc.ws_upgrade`     | —                  | ServerHandler.cpp | WebSocket upgrade handshake (records handshake failures)                 |
| `rpc.process`        | `rpc.http_request` | ServerHandler.cpp | RPC processing pipeline (single or batch request)                        |
| `rpc.command.<name>` | `rpc.process`      | RPCHandler.cpp    | Per-command span (e.g., `rpc.command.server_info`, `rpc.command.ledger`) |

**Where to find**: Tempo → TraceQL: `{resource.service.name="xrpld" && name=~"rpc.http_request|rpc.command.*"}`

**Grafana dashboard**: _RPC Performance_ (`rpc-performance`)

#### gRPC Spans

Controlled by `trace_rpc=1` in `[telemetry]` config.

| Span Name           | Parent | Source File    | Description                                                                                                               |
| ------------------- | ------ | -------------- | ------------------------------------------------------------------------------------------------------------------------- |
| `grpc.<MethodName>` | —      | GRPCServer.cpp | One flat span per gRPC method (e.g., `grpc.GetLedger`, `grpc.GetLedgerData`, `grpc.GetLedgerDiff`, `grpc.GetLedgerEntry`) |

The method name is embedded in the span name (formed at the call site as
`grpc.<MethodName>`), so dashboards break out per-method latency and error
rates without TraceQL attribute filters.

**Where to find**: Tempo → TraceQL: `{resource.service.name="xrpld" && name=~"grpc.*"}`

**Grafana dashboard**: _RPC Performance_ (`rpc-performance`)

#### Transaction Spans

Controlled by `trace_transactions=1` in `[telemetry]` config.

| Span Name       | Parent         | Source File     | Description                                                       |
| --------------- | -------------- | --------------- | ----------------------------------------------------------------- |
| `tx.process`    | —              | NetworkOPs.cpp  | Transaction submission entry point (local or peer-relayed)        |
| `tx.receive`    | —              | PeerImp.cpp     | Raw transaction received from peer overlay (before deduplication) |
| `tx.apply`      | `ledger.build` | BuildLedger.cpp | Transaction set applied to new ledger during consensus            |
| `tx.preflight`  | —              | applySteps.cpp  | Stateless checks stage (`stage=preflight`)                        |
| `tx.preclaim`   | —              | applySteps.cpp  | Ledger-aware checks stage before fee claim (`stage=preclaim`)     |
| `tx.transactor` | —              | Transactor.cpp  | Apply stage — the transactor runs (`stage=apply`)                 |

The three apply-pipeline spans share a deterministic `trace_id` derived from
`txID[0:16]`, so preflight, preclaim, and transactor for one transaction group
under a single trace even though they run sequentially and often on different
threads. A transaction that hard-fails preflight or preclaim never reaches the
later spans — the `stage` attribute identifies where it stopped.

**Where to find**: Tempo → TraceQL: `{resource.service.name="xrpld" && name=~"tx.process|tx.receive"}`
or, for the apply pipeline: `{resource.service.name="xrpld" && name=~"tx.preflight|tx.preclaim|tx.transactor"}`

**Grafana dashboard**: _Transaction Overview_ (`transaction-overview`)

#### Transaction Queue (TxQ) Spans

Controlled by `trace_transactions=1` in `[telemetry]` config.

| Span Name          | Parent        | Source File | Description                                         |
| ------------------ | ------------- | ----------- | --------------------------------------------------- |
| `txq.enqueue`      | `tx.process`  | TxQ.cpp     | Enqueue decision when a tx is submitted             |
| `txq.apply_direct` | `txq.enqueue` | TxQ.cpp     | Direct apply attempt that bypasses the queue        |
| `txq.batch_clear`  | `txq.enqueue` | TxQ.cpp     | Batch clear of an account's queued txs              |
| `txq.accept`       | —             | TxQ.cpp     | Ledger-close accept loop (drains the queue)         |
| `txq.accept_tx`    | `txq.accept`  | TxQ.cpp     | Per-queued-transaction apply inside the accept loop |
| `txq.cleanup`      | —             | TxQ.cpp     | Post-close cleanup of expired queue entries         |

**Where to find**: Tempo → TraceQL: `{resource.service.name="xrpld" && name=~"txq.*"}`

**Grafana dashboard**: _Transaction Overview_ (`transaction-overview`)

#### Consensus Spans

Controlled by `trace_consensus=1` in `[telemetry]` config.

| Span Name                      | Parent             | Source File      | Description                                                         |
| ------------------------------ | ------------------ | ---------------- | ------------------------------------------------------------------- |
| `consensus.round`              | — (root)           | RCLConsensus.cpp | Root span for one consensus round (deterministic trace per round)   |
| `consensus.phase.open`         | `consensus.round`  | Consensus.h      | Open phase — collecting transactions before close                   |
| `consensus.proposal.send`      | `consensus.round`  | RCLConsensus.cpp | Node broadcasts its transaction set proposal                        |
| `consensus.ledger_close`       | `consensus.round`  | RCLConsensus.cpp | Ledger close event triggered by consensus                           |
| `consensus.establish`          | `consensus.round`  | Consensus.h      | Establish phase — converging on the transaction set                 |
| `consensus.update_positions`   | `consensus.round`  | Consensus.h      | Position update with per-dispute vote details                       |
| `consensus.check`              | `consensus.round`  | Consensus.h      | Consensus threshold check (agree/disagree tally)                    |
| `consensus.accept`             | `consensus.round`  | RCLConsensus.cpp | Consensus accepts a ledger (round complete)                         |
| `consensus.accept.apply`       | `consensus.accept` | RCLConsensus.cpp | Ledger application with close-time details (jtACCEPT thread)        |
| `consensus.validation.send`    | `consensus.round`  | RCLConsensus.cpp | Validation message sent after ledger accepted (follows-from link)   |
| `consensus.mode_change`        | `consensus.round`  | RCLConsensus.cpp | Operating-mode transition during the round                          |
| `consensus.proposal.receive`   | (context)          | PeerImp.cpp      | Proposal received from a peer (context-propagated into the round)   |
| `consensus.validation.receive` | (context)          | PeerImp.cpp      | Validation received from a peer (context-propagated into the round) |

The `.receive` spans are created per-message in the overlay and joined to the
round trace via context propagation rather than direct parenting. The
`consensus.validation.send` span uses a follows-from link off the round.

**Where to find**: Tempo → TraceQL: `{resource.service.name="xrpld" && name=~"consensus.*"}`

**Grafana dashboard**: _Consensus Health_ (`consensus-health`)

#### Ledger Spans

Controlled by `trace_ledger=1` in `[telemetry]` config.

| Span Name         | Parent | Source File       | Description                                    |
| ----------------- | ------ | ----------------- | ---------------------------------------------- |
| `ledger.build`    | —      | BuildLedger.cpp   | Build new ledger from accepted transaction set |
| `ledger.validate` | —      | LedgerMaster.cpp  | Ledger promoted to validated status            |
| `ledger.store`    | —      | LedgerMaster.cpp  | Ledger stored to database/history              |
| `ledger.acquire`  | —      | InboundLedger.cpp | Fetch a missing ledger from peers              |

**Where to find**: Tempo → TraceQL: `{resource.service.name="xrpld" && name=~"ledger.*"}`

**Grafana dashboard**: _Ledger Operations_ (`ledger-operations`)

#### Peer Spans

Controlled by `trace_peer` in `[telemetry]` config. **Enabled by default** (high volume).

| Span Name                 | Parent | Source File | Description                           |
| ------------------------- | ------ | ----------- | ------------------------------------- |
| `peer.proposal.receive`   | —      | PeerImp.cpp | Consensus proposal received from peer |
| `peer.validation.receive` | —      | PeerImp.cpp | Validation message received from peer |

A `—` parent means the span is a fresh trace root (`kConsumer`): it is started
via `SpanGuard::rootSpan()` at the inbound-message entry point and never
inherits an ambient span left active on the peer thread, so it does not nest
under an unrelated transaction's trace.

**Where to find**: Tempo → TraceQL: `{resource.service.name="xrpld" && name=~"peer.*"}`

**Grafana dashboard**: _Peer Network_ (`peer-network`)

#### PathFind Spans

Controlled by `trace_rpc=1` in `[telemetry]` config.

| Span Name             | Parent             | Source File     | Description                                                |
| --------------------- | ------------------ | --------------- | ---------------------------------------------------------- |
| `pathfind.request`    | `rpc.command.*`    | PathRequest.cpp | `path_find` / `ripple_path_find` RPC entry                 |
| `pathfind.compute`    | `pathfind.request` | PathRequest.cpp | Path computation for one request (`PathRequest::doUpdate`) |
| `pathfind.discover`   | `pathfind.compute` | Pathfinder.cpp  | Graph exploration (one per RPC call)                       |
| `pathfind.update_all` | —                  | PathRequest.cpp | Async recomputation of all active requests at ledger close |

**Where to find**: Tempo → TraceQL: `{resource.service.name="xrpld" && name=~"pathfind.*"}`

---

### 1.2 Complete Attribute Inventory (bare/underscore keys)

> **See also**: [02-design-decisions.md §2.4.2](./02-design-decisions.md#242-span-attributes-by-category) for attribute design rationale and privacy considerations.

Every span can carry key-value attributes that provide context for filtering and
aggregation. Per the 2026-05-13 naming redesign, span-attribute keys use the
**bare** field name (the span name already carries the domain), or the
`<domain>_<field>` underscore form where a bare name would collide (e.g.
`rpc_status`, `grpc_status`, `tx_status`, `txq_status`).

> **Dotted keys are resource attributes, never span attributes:**
>
> - `xrpl.network.id` and `xrpl.network.type` are **resource** attributes set
>   once at startup on the OTel resource — not span attributes. They appear on
>   every span's resource scope, queried as `{resource.xrpl.network.id=...}`.
> - The ledger hash uses the bare `ledger_hash` key on every span that records
>   it (both `consensus.validation.send` and `peer.validation.receive`) — there
>   is no dotted span attribute.

#### RPC Attributes

| Attribute              | Type    | Set On                            | Description                                      |
| ---------------------- | ------- | --------------------------------- | ------------------------------------------------ |
| `command`              | string  | `rpc.command.*`, `rpc.ws_message` | RPC command name (e.g., `server_info`, `ledger`) |
| `version`              | int64   | `rpc.command.*`                   | API version number                               |
| `rpc_role`             | string  | `rpc.command.*`                   | Caller role: `"admin"` or `"user"`               |
| `rpc_status`           | string  | `rpc.command.*`                   | Result: `"success"` or `"error"`                 |
| `request_payload_size` | int64   | `rpc.http_request`                | Bytes of inbound request payload                 |
| `is_batch`             | boolean | `rpc.process`                     | `true` if the request is a JSON-RPC batch        |
| `batch_size`           | int64   | `rpc.process`                     | Number of sub-requests in a batch                |
| `load_type`            | string  | `rpc.command.*`                   | Resource cost category after execution           |

**Tempo query**: `{span.command="server_info"}` to find all `server_info` calls.

**Prometheus label**: `command` (used as a SpanMetrics dimension).

#### gRPC Attributes

| Attribute     | Type   | Set On              | Description                          |
| ------------- | ------ | ------------------- | ------------------------------------ |
| `method`      | string | `grpc.<MethodName>` | gRPC method name (e.g., `GetLedger`) |
| `grpc_role`   | string | `grpc.<MethodName>` | Caller role: `"admin"` or `"user"`   |
| `grpc_status` | string | `grpc.<MethodName>` | Result: `"success"` or `"error"`     |

**Tempo query**: `{span.method="GetLedger"}` or `{name="grpc.GetLedger"}`.

**Prometheus labels**: `method`, `grpc_role`, `grpc_status` (SpanMetrics dimensions).

#### Transaction Attributes

| Attribute      | Type    | Set On                                                       | Description                                                           |
| -------------- | ------- | ------------------------------------------------------------ | --------------------------------------------------------------------- |
| `tx_hash`      | string  | `tx.process`, `tx.receive`                                   | Transaction hash (hex-encoded)                                        |
| `local`        | boolean | `tx.process`                                                 | `true` if locally submitted, `false` if peer-relayed                  |
| `path`         | string  | `tx.process`                                                 | Submission path: `"sync"` or `"async"`                                |
| `tx_type`      | string  | `tx.process`, `tx.preflight`, `tx.preclaim`, `tx.transactor` | Transaction type name (e.g., `Payment`)                               |
| `fee`          | int64   | `tx.process`                                                 | Transaction fee in drops                                              |
| `sequence`     | int64   | `tx.process`                                                 | Transaction sequence number                                           |
| `suppressed`   | boolean | `tx.receive`                                                 | `true` if transaction was suppressed (duplicate)                      |
| `tx_status`    | string  | `tx.receive`                                                 | Transaction status (e.g., `"known_bad"`)                              |
| `peer_id`      | int64   | `tx.receive`                                                 | Peer identifier (also set on peer spans)                              |
| `peer_version` | string  | `tx.receive`                                                 | Peer protocol version string                                          |
| `stage`        | string  | `tx.preflight`, `tx.preclaim`, `tx.transactor`               | Apply-pipeline stage: `preflight`, `preclaim`, or `apply`             |
| `ter_result`   | string  | `tx.preflight`, `tx.preclaim`, `tx.transactor`               | Engine result token for that stage (e.g., `tesSUCCESS`, `terPRE_SEQ`) |
| `applied`      | boolean | `tx.transactor`                                              | `true` if the transaction was applied to the ledger                   |

**Tempo query**: `{span.tx_hash="<hash>"}` to trace a specific transaction across nodes.

**Prometheus labels**: `local`, `suppressed`, `tx_type`, `ter_result`, `stage` (SpanMetrics dimensions).

#### Transaction Queue (TxQ) Attributes

| Attribute            | Type    | Set On                         | Description                                                 |
| -------------------- | ------- | ------------------------------ | ----------------------------------------------------------- |
| `tx_hash`            | string  | `txq.enqueue`, `txq.accept_tx` | Transaction hash                                            |
| `tx_type`            | string  | `txq.enqueue`                  | Transaction type name                                       |
| `txq_status`         | string  | `txq.enqueue`, `txq.accept_tx` | Queue outcome (e.g. `queued`, `applied_direct`, `rejected`) |
| `fee_level_paid`     | int64   | `txq.enqueue`                  | Fee level paid by the queued tx                             |
| `required_fee_level` | int64   | `txq.enqueue`                  | Minimum fee level for inclusion                             |
| `num_cleared`        | int64   | `txq.batch_clear`              | Entries cleared in a batch                                  |
| `queue_size`         | int64   | `txq.accept`                   | Current TxQ depth                                           |
| `ledger_changed`     | boolean | `txq.accept`                   | Whether the ledger changed since last attempt               |
| `ter_code`           | int64   | `txq.accept_tx`                | Transaction engine result code                              |
| `retries_remaining`  | int64   | `txq.accept_tx`                | Retries left before discard                                 |
| `ledger_seq`         | int64   | `txq.cleanup`                  | Ledger sequence number                                      |
| `expired_count`      | int64   | `txq.cleanup`                  | Number of expired entries cleared                           |

**Prometheus label**: `txq_status` (SpanMetrics dimension).

#### Consensus Attributes

| Attribute                  | Type    | Set On                                                                                             | Description                                              |
| -------------------------- | ------- | -------------------------------------------------------------------------------------------------- | -------------------------------------------------------- |
| `consensus_ledger_id`      | string  | `consensus.round`                                                                                  | Previous-ledger id anchoring the round                   |
| `ledger_seq`               | int64   | `consensus.round`, `consensus.ledger_close`, `consensus.accept.apply`, `consensus.validation.send` | Ledger sequence number                                   |
| `consensus_mode`           | string  | `consensus.round`, `consensus.ledger_close`                                                        | Node mode: `"Proposing"`, `"Observing"`, `"Wrong"`, etc. |
| `consensus_round_id`       | int64   | `consensus.round`                                                                                  | Round identifier                                         |
| `consensus_phase`          | string  | `consensus.round`                                                                                  | Current phase name (updated on each transition)          |
| `trace_strategy`           | string  | `consensus.round`                                                                                  | Trace-id strategy (`deterministic` / `random`)           |
| `previous_ledger_seq`      | int64   | `consensus.round`                                                                                  | Sequence of the previous ledger                          |
| `previous_proposers`       | int64   | `consensus.round`                                                                                  | Proposer count in the previous round                     |
| `previous_round_time_ms`   | int64   | `consensus.round`                                                                                  | Duration of the previous round                           |
| `consensus_round`          | int64   | `consensus.proposal.send`                                                                          | Proposal sequence number for the broadcast proposal      |
| `is_bow_out`               | boolean | `consensus.proposal.send`                                                                          | Whether the proposal is a bow-out (resigning the round)  |
| `tx_count_open`            | int64   | `consensus.ledger_close`                                                                           | Transactions in the open ledger at close                 |
| `close_time_resolution_ms` | int64   | `consensus.ledger_close`                                                                           | Close-time rounding granularity                          |
| `converge_percent`         | int64   | `consensus.establish`, `consensus.update_positions`                                                | Convergence percentage                                   |
| `establish_count`          | int64   | `consensus.establish`                                                                              | Establish-phase iteration count                          |
| `proposers`                | int64   | `consensus.establish`, `consensus.update_positions`, `consensus.accept`                            | Number of proposers                                      |
| `disputes_count`           | int64   | `consensus.establish`, `consensus.update_positions`                                                | Number of disputed transactions                          |
| `tx_id`                    | string  | `consensus.update_positions`                                                                       | Disputed transaction id (per-dispute event)              |
| `dispute_our_vote`         | boolean | `consensus.update_positions`                                                                       | Our vote on the disputed tx                              |
| `dispute_yays`             | int64   | `consensus.update_positions`                                                                       | Yes votes on the disputed tx                             |
| `dispute_nays`             | int64   | `consensus.update_positions`                                                                       | No votes on the disputed tx                              |
| `agree_count`              | int64   | `consensus.check`                                                                                  | Agreeing proposer count                                  |
| `disagree_count`           | int64   | `consensus.check`                                                                                  | Disagreeing proposer count                               |
| `threshold_percent`        | int64   | `consensus.check`                                                                                  | Agreement threshold percentage                           |
| `consensus_result`         | string  | `consensus.check`                                                                                  | Check outcome                                            |
| `quorum`                   | int64   | `consensus.check`, `consensus.accept`                                                              | Quorum required                                          |
| `round_time_ms`            | int64   | `consensus.accept`, `consensus.accept.apply`                                                       | Total consensus round duration in milliseconds           |
| `consensus_state`          | string  | `consensus.accept.apply`                                                                           | Consensus outcome: `"finished"` or `"moved_on"`          |
| `close_time`               | int64   | `consensus.accept.apply`                                                                           | Agreed-upon ledger close time (epoch seconds)            |
| `close_time_correct`       | boolean | `consensus.accept.apply`                                                                           | Whether validators agreed on close time                  |
| `close_resolution_ms`      | int64   | `consensus.accept.apply`                                                                           | Close-time rounding granularity in milliseconds          |
| `proposing`                | boolean | `consensus.accept.apply`, `consensus.validation.send`                                              | Whether this node was a proposer                         |
| `parent_close_time`        | int64   | `consensus.accept.apply`                                                                           | Parent ledger close time                                 |
| `close_time_self`          | int64   | `consensus.accept.apply`                                                                           | This node's close-time vote                              |
| `close_time_vote_bins`     | string  | `consensus.accept.apply`                                                                           | Distribution of close-time votes                         |
| `resolution_direction`     | string  | `consensus.accept.apply`                                                                           | Whether close resolution increased/decreased/unchanged   |
| `tx_count`                 | int64   | `consensus.accept.apply`                                                                           | Transactions in the accepted set                         |
| `ledger_hash`              | string  | `consensus.validation.send`                                                                        | Full hash of the validated ledger (shared with peer)     |
| `full_validation`          | boolean | `consensus.validation.send`                                                                        | Whether this is a full validation                        |
| `validation_sign_time`     | int64   | `consensus.validation.send`                                                                        | Validation signing time                                  |
| `mode_old`                 | string  | `consensus.mode_change`                                                                            | Operating mode before the transition                     |
| `mode_new`                 | string  | `consensus.mode_change`                                                                            | Operating mode after the transition                      |

**Tempo query**: `{span.consensus_mode="Proposing"}` to find rounds where the node was proposing.

**Prometheus labels**: `consensus_mode`, `consensus_state`, `consensus_phase`, `consensus_result`, `consensus_stalled`, `mode_new`, `close_time_correct` (SpanMetrics dimensions).

#### Ledger Attributes

| Attribute             | Type    | Set On                                            | Description                                      |
| --------------------- | ------- | ------------------------------------------------- | ------------------------------------------------ |
| `ledger_seq`          | int64   | `ledger.build`, `ledger.validate`, `ledger.store` | Ledger sequence number                           |
| `close_time`          | int64   | `ledger.build`                                    | Ledger close time (epoch seconds)                |
| `close_time_correct`  | boolean | `ledger.build`                                    | Whether close time was agreed upon by validators |
| `close_resolution_ms` | int64   | `ledger.build`                                    | Close time rounding granularity in milliseconds  |
| `tx_count`            | int64   | `tx.apply`                                        | Transactions applied to the ledger               |
| `tx_failed`           | int64   | `tx.apply`                                        | Failed transactions in the apply set             |
| `validations`         | int64   | `ledger.validate`                                 | Number of validations received for this ledger   |
| `acquire_reason`      | string  | `ledger.acquire`                                  | Why the ledger fetch was triggered               |
| `timeouts`            | int64   | `ledger.acquire`                                  | Number of fetch timeouts                         |
| `peer_count`          | int64   | `ledger.acquire`                                  | Peers queried during the fetch                   |
| `outcome`             | string  | `ledger.acquire`                                  | Fetch outcome                                    |

The apply-step span `tx.apply` (child of `ledger.build`) carries `tx_count`/`tx_failed`;
the parent `ledger.build` carries `ledger_seq` and the close-time attributes.
`ledger.acquire` (InboundLedger) also sets `ledger_seq`.

**Tempo query**: `{span.ledger_seq=12345}` to find all spans for a specific ledger.

#### Peer Attributes

| Attribute            | Type    | Set On                                                           | Description                                          |
| -------------------- | ------- | ---------------------------------------------------------------- | ---------------------------------------------------- |
| `peer_id`            | int64   | `tx.receive`, `peer.proposal.receive`, `peer.validation.receive` | Peer identifier                                      |
| `proposal_trusted`   | boolean | `peer.proposal.receive`                                          | Whether the proposal came from a trusted validator   |
| `validation_trusted` | boolean | `peer.validation.receive`                                        | Whether the validation came from a trusted validator |
| `full_validation`    | boolean | `peer.validation.receive`                                        | Whether the validation is a full validation          |
| `ledger_hash`        | string  | `peer.validation.receive`                                        | Validated ledger hash (shared with consensus spans)  |

**Prometheus labels**: `proposal_trusted`, `validation_trusted` (SpanMetrics dimensions).

#### PathFind Attributes

| Attribute                 | Type    | Set On                | Description                              |
| ------------------------- | ------- | --------------------- | ---------------------------------------- |
| `pathfind_source_account` | string  | `pathfind.request`    | Originating account for the path search  |
| `pathfind_dest_account`   | string  | `pathfind.request`    | Destination account                      |
| `pathfind_fast`           | boolean | `pathfind.compute`    | Whether fast pathfinding mode is enabled |
| `pathfind_search_level`   | int64   | `pathfind.discover`   | Depth of graph exploration               |
| `pathfind_num_paths`      | int64   | `pathfind.discover`   | Total paths produced                     |
| `pathfind_ledger_index`   | int64   | `pathfind.update_all` | Target ledger index                      |
| `pathfind_num_requests`   | int64   | `pathfind.update_all` | Active requests recomputed               |

---

### 1.3 SpanMetrics — Derived Prometheus Metrics

> **See also**: [01-architecture-analysis.md](./01-architecture-analysis.md) §1.8.2 for how span-derived metrics map to operational insights.

The OTel Collector's SpanMetrics connector automatically generates RED (Rate, Errors, Duration) metrics from every span. No custom metrics code in xrpld is needed.

| Prometheus Metric                   | Type      | Description                                                                    |
| ----------------------------------- | --------- | ------------------------------------------------------------------------------ |
| `span_calls_total`                  | Counter   | Total span invocations                                                         |
| `span_duration_milliseconds_bucket` | Histogram | Latency distribution (buckets: 1, 5, 10, 25, 50, 100, 250, 500, 1000, 5000 ms) |
| `span_duration_milliseconds_count`  | Histogram | Observation count                                                              |
| `span_duration_milliseconds_sum`    | Histogram | Cumulative latency                                                             |

**Standard labels on every metric**: `span_name`, `status_code`, `service_name`, `span_kind`

**Additional dimension labels** (configured in `otel-collector-config.yaml`).
The Prometheus label is the **bare span-attribute key verbatim** — the
SpanMetrics connector does not rewrite or prefix it:

| Prometheus Label / Span Attribute | Type    | Applies To                                     |
| --------------------------------- | ------- | ---------------------------------------------- |
| `command`                         | string  | `rpc.command.*`                                |
| `rpc_status`                      | string  | `rpc.command.*`                                |
| `consensus_mode`                  | string  | `consensus.round`, `consensus.ledger_close`    |
| `close_time_correct`              | boolean | `consensus.accept.apply`                       |
| `local`                           | boolean | `tx.process`                                   |
| `suppressed`                      | boolean | `tx.receive`                                   |
| `proposal_trusted`                | boolean | `peer.proposal.receive`                        |
| `validation_trusted`              | boolean | `peer.validation.receive`                      |
| `tx_type`                         | string  | `tx.*`, `txq.enqueue`                          |
| `ter_result`                      | string  | `tx.preflight`, `tx.preclaim`, `tx.transactor` |
| `stage`                           | string  | `tx.preflight`, `tx.preclaim`, `tx.transactor` |
| `txq_status`                      | string  | `txq.enqueue`, `txq.accept_tx`                 |
| `consensus_state`                 | string  | `consensus.accept.apply`                       |
| `load_type`                       | string  | `rpc.command.*`                                |
| `is_batch`                        | boolean | `rpc.process`                                  |
| `mode_new`                        | string  | `consensus.mode_change`                        |
| `consensus_stalled`               | boolean | `consensus.check`                              |
| `consensus_phase`                 | string  | `consensus.round`                              |
| `consensus_result`                | string  | `consensus.check`                              |
| `method`                          | string  | `grpc.<MethodName>`                            |
| `grpc_role`                       | string  | `grpc.<MethodName>`                            |
| `grpc_status`                     | string  | `grpc.<MethodName>`                            |

The `stage` dimension (3 values: `preflight`, `preclaim`, `apply`) turns the
apply-pipeline spans into per-stage RED metrics with no native instruments — the
_Transaction Overview_ dashboard charts rate, p95 latency, and failure rate by stage.

> **Sampling caveat**: xrpld head sampling is fixed at 1.0 (every trace is
> recorded), so span-derived metrics are not undercounted at the node. If the
> collector is configured with tail sampling, span-derived metrics reflect only
> the retained traces, whereas native StatsD/meter metrics do not sample.
> Account for any collector-side tail sampling when reading absolute stage rates.

**Where to query**: Prometheus → `span_calls_total{span_name="rpc.command.server_info"}`

---

## 2. System Metrics (beast::insight — OTel native)

> **See also**: [02-design-decisions.md](./02-design-decisions.md) for the beast::insight coexistence design. [06-implementation-phases.md](./06-implementation-phases.md) for the Phase 6/7 metric inventory.
>
> **Migration complete**: Phase 7 replaced the StatsD UDP transport with native OTel Metrics SDK export via OTLP/HTTP. The `beast::insight::Collector` interface and all metric names are preserved — only the wire protocol changed. `[insight] server=statsd` remains as a fallback.

These are system-level metrics emitted by xrpld's `beast::insight` framework via OTel OTLP/HTTP. They cover operational data that doesn't map to individual trace spans.

### Configuration

```ini
# Recommended: native OTel metrics via OTLP/HTTP
[insight]
server=otel
endpoint=http://localhost:4318/v1/metrics
prefix=xrpld
```

Fallback (StatsD):

```ini
[insight]
server=statsd
address=127.0.0.1:8125
prefix=xrpld
```

### 2.1 Gauges

| Prometheus Metric                           | Source File           | Description                               | Typical Range                   |
| ------------------------------------------- | --------------------- | ----------------------------------------- | ------------------------------- |
| `ledgermaster_validated_ledger_age`         | LedgerMaster.h        | Seconds since last validated ledger       | 0–10 (healthy), >30 (stale)     |
| `ledgermaster_published_ledger_age`         | LedgerMaster.h        | Seconds since last published ledger       | 0–10 (healthy)                  |
| `state_accounting_disconnected_duration`    | NetworkOPs.cpp        | Cumulative seconds in Disconnected state  | Monotonic                       |
| `state_accounting_connected_duration`       | NetworkOPs.cpp        | Cumulative seconds in Connected state     | Monotonic                       |
| `state_accounting_syncing_duration`         | NetworkOPs.cpp        | Cumulative seconds in Syncing state       | Monotonic                       |
| `state_accounting_tracking_duration`        | NetworkOPs.cpp        | Cumulative seconds in Tracking state      | Monotonic                       |
| `state_accounting_full_duration`            | NetworkOPs.cpp        | Cumulative seconds in Full state          | Monotonic (should dominate)     |
| `state_accounting_disconnected_transitions` | NetworkOPs.cpp        | Count of transitions to Disconnected      | Low                             |
| `state_accounting_connected_transitions`    | NetworkOPs.cpp        | Count of transitions to Connected         | Low                             |
| `state_accounting_syncing_transitions`      | NetworkOPs.cpp        | Count of transitions to Syncing           | Low                             |
| `state_accounting_tracking_transitions`     | NetworkOPs.cpp        | Count of transitions to Tracking          | Low                             |
| `state_accounting_full_transitions`         | NetworkOPs.cpp        | Count of transitions to Full              | Low (should be 1 after startup) |
| `peer_finder_active_inbound_peers`          | PeerfinderManager.cpp | Active inbound peer connections           | 0–85                            |
| `peer_finder_active_outbound_peers`         | PeerfinderManager.cpp | Active outbound peer connections          | 10–21                           |
| `overlay_peer_disconnects`                  | OverlayImpl.cpp       | Cumulative peer disconnection count       | Low growth                      |
| `overlay_peer_disconnects_charges`          | OverlayImpl.cpp       | Disconnects due to resource limit charges | Low growth (subset of above)    |
| `jobq_job_count`                            | JobQueue.cpp          | Current job queue depth (group `jobq`)    | 0–100 (healthy)                 |

**Grafana dashboard**: _Node Health_ (`node-health`)

### 2.2 Counters

| Prometheus Metric         | Source File        | Description                                   |
| ------------------------- | ------------------ | --------------------------------------------- |
| `rpc_requests`            | ServerHandler.cpp  | Total RPC requests received                   |
| `ledger_fetches`          | InboundLedgers.cpp | Inbound ledger fetch attempts                 |
| `ledger_history_mismatch` | LedgerHistory.cpp  | Ledger hash mismatches detected               |
| `warn`                    | Logic.h            | Resource manager warnings issued              |
| `drop`                    | Logic.h            | Resource manager drops (connections rejected) |

**Note**: With `server=otel`, `warn` and `drop` are properly exported as OTel Counter instruments. The previous StatsD `|m` type limitation no longer applies.

**Grafana dashboard**: _RPC & Pathfinding_ (`rpc-pathfinding`)

### 2.3 Histograms (Event timers)

| Prometheus Metric | Source File       | Unit | Description                    |
| ----------------- | ----------------- | ---- | ------------------------------ |
| `rpc_time`        | ServerHandler.cpp | ms   | RPC response time distribution |
| `rpc_size`        | ServerHandler.cpp | ms\* | RPC response size (see note)   |
| `ios_latency`     | Application.cpp   | ms   | I/O service loop latency       |
| `pathfind_fast`   | PathRequests.h    | ms   | Fast pathfinding duration      |
| `pathfind_full`   | PathRequests.h    | ms   | Full pathfinding duration      |

Quantiles collected: 0th, 50th, 90th, 95th, 99th, 100th percentile.

\* **`rpc_size` instrument mismatch (known issue):** response size in bytes is
recorded through the millisecond-scaled event histogram (`makeEvent`), so it is
exported as `rpc_size_milliseconds_bucket` with time-scaled boundaries that top
out at 5000. Byte values above ~5 KB saturate in the last bucket, so the
percentiles are not true byte sizes. The _RPC & Pathfinding_ panel is flagged
accordingly. A dedicated byte-unit histogram is needed to fix this; tracked
separately.

**Grafana dashboards**: _Node Health_ (`ios_latency`), _RPC & Pathfinding_ (`rpc_time`, `rpc_size`, `pathfind_*`)

### 2.4 Overlay Traffic Metrics

For each of the 45+ overlay traffic categories (defined in `TrafficCount.h`), four gauges are emitted:

- `{category}_bytes_in`
- `{category}_bytes_out`
- `{category}_messages_in`
- `{category}_messages_out`

**Key categories**:

| Category                                                          | Description                |
| ----------------------------------------------------------------- | -------------------------- |
| `total`                                                           | All traffic aggregated     |
| `overhead` / `overhead_overlay`                                   | Protocol overhead          |
| `transactions` / `transactions_duplicate`                         | Transaction relay          |
| `proposals` / `proposals_untrusted` / `proposals_duplicate`       | Consensus proposals        |
| `validations` / `validations_untrusted` / `validations_duplicate` | Consensus validations      |
| `ledger_data_get` / `ledger_data_share`                           | Ledger data exchange       |
| `ledger_data_Transaction_Node_get/share`                          | Transaction node data      |
| `ledger_data_Account_State_Node_get/share`                        | Account state node data    |
| `ledger_data_Transaction_Set_candidate_get/share`                 | Transaction set candidates |
| `getObject` / `haveTxSet` / `ledgerData`                          | Object requests            |
| `ping` / `status`                                                 | Keepalive and status       |
| `set_get`                                                         | Set requests               |

**Grafana dashboards**: _Network Traffic_ (`network-traffic`), _Overlay Traffic Detail_ (`overlay-traffic-detail`), _Ledger Data & Sync_ (`ledger-data-sync`)

---

## 3. Grafana Dashboard Reference

> **See also**: [05-configuration-reference.md](./05-configuration-reference.md) §5.8 for Grafana data source provisioning (Tempo, Prometheus) and TraceQL query examples.

### 3.1 Span-Derived Dashboards (5)

| Dashboard            | UID                    | Data Source              | Key Panels                                                                         |
| -------------------- | ---------------------- | ------------------------ | ---------------------------------------------------------------------------------- |
| RPC Performance      | `rpc-performance`      | Prometheus (SpanMetrics) | Request rate by command, p95 latency by command, error rate, heatmap, top commands |
| Transaction Overview | `transaction-overview` | Prometheus (SpanMetrics) | Processing rate, latency p95/p50, local vs relay split, apply duration, heatmap    |
| Consensus Health     | `consensus-health`     | Prometheus (SpanMetrics) | Round duration p95/p50, proposals rate, close duration, mode timeline, heatmap     |
| Ledger Operations    | `ledger-operations`    | Prometheus (SpanMetrics) | Build rate, build duration, validation rate, store rate, build vs close comparison |
| Peer Network         | `peer-network`         | Prometheus (SpanMetrics) | Proposal receive rate, validation receive rate, trusted vs untrusted breakdown     |

### 3.2 System Metrics Dashboards (5)

| Dashboard              | UID                      | Data Source       | Key Panels                                                                        |
| ---------------------- | ------------------------ | ----------------- | --------------------------------------------------------------------------------- |
| Node Health            | `node-health`            | Prometheus (OTLP) | Ledger age, operating mode, I/O latency, job queue, fetch rate                    |
| Network Traffic        | `network-traffic`        | Prometheus (OTLP) | Active peers, disconnects, bytes in/out, messages in/out, traffic by category     |
| RPC & Pathfinding      | `rpc-pathfinding`        | Prometheus (OTLP) | RPC rate, response time/size, pathfinding duration, resource warnings/drops       |
| Overlay Traffic Detail | `overlay-traffic-detail` | Prometheus (OTLP) | Squelch, overhead, validator lists, set get/share, have/requested tx, proof paths |
| Ledger Data & Sync     | `ledger-data-sync`       | Prometheus (OTLP) | Ledger data exchange, legacy ledger share/get, getobject by type, traffic heatmap |

### 3.3 Deployment-Tier Template Variables

Every dashboard carries seven filtering template variables (each variable name
matches its Prometheus label), letting one Grafana stack be sliced by tier and
by perf-comparison run:

| Variable                  | Source label             | Description                                                      |
| ------------------------- | ------------------------ | ---------------------------------------------------------------- |
| `$node`                   | `service_instance_id`    | Filter by xrpld node instance                                    |
| `$service_name`           | `service_name`           | Filter by service (`service.name`, e.g. `xrpld`)                 |
| `$deployment_environment` | `deployment_environment` | Filter by deployment tier (`local` / `test` / `ci` / `prod`)     |
| `$xrpl_network_type`      | `xrpl_network_type`      | Filter by network (`mainnet` / `testnet` / `devnet` / `perf`)    |
| `$xrpl_work_item`         | `xrpl_work_item`         | Filter by perf-iac work item / ticket (e.g. `RIPD-7455`)         |
| `$xrpl_branch`            | `xrpl_branch`            | Filter by comparison side (`baseline:<ref>:<commit>` / `test:…`) |
| `$xrpl_node_role`         | `xrpl_node_role`         | Filter by node role (`validator` / `peer`)                       |

The last three are populated only during perf-iac comparison runs (stamped as
resource attributes by perf-iac's own alloy pipeline, not the repo collector).
Outside those runs the labels are absent; the filters default to **All**, which
matches series lacking the label so every dashboard still renders.

See [telemetry-runbook.md](../docs/telemetry-runbook.md) "Deployment Tiers"
for how the tier attributes are set and reach metrics.

### 3.4 Accessing the Dashboards

1. Open Grafana at **http://localhost:3000**
2. Navigate to **Dashboards → xrpld** folder
3. All 10 dashboards are auto-provisioned from `docker/telemetry/grafana/dashboards/`

---

## 4. Tempo Trace Search Guide

> **See also**: [08-appendix.md](./08-appendix.md) §8.2 for span hierarchy visualizations. [05-configuration-reference.md](./05-configuration-reference.md) §5.8.5 for TraceQL query examples.

### Finding Traces by Type

| What to Find             | Tempo TraceQL Query                                                            |
| ------------------------ | ------------------------------------------------------------------------------ |
| All RPC calls            | `{resource.service.name="xrpld" && name="rpc.http_request"}`                   |
| Specific RPC command     | `{resource.service.name="xrpld" && name="rpc.command.server_info"}`            |
| Slow RPC calls           | `{resource.service.name="xrpld" && name=~"rpc.command.*"} \| duration > 100ms` |
| Failed RPC calls         | `{span.rpc_status="error"}`                                                    |
| gRPC method calls        | `{resource.service.name="xrpld" && name="grpc.GetLedger"}`                     |
| Specific transaction     | `{span.tx_hash="<hex_hash>"}`                                                  |
| Local transactions only  | `{span.local=true}`                                                            |
| Consensus rounds         | `{resource.service.name="xrpld" && name="consensus.round"}`                    |
| Rounds by mode           | `{span.consensus_mode="Proposing"}`                                            |
| Specific ledger          | `{span.ledger_seq=12345}`                                                      |
| Peer proposals (trusted) | `{span.proposal_trusted=true}`                                                 |

### Trace Structure

A typical RPC trace shows the span hierarchy:

```
rpc.http_request (ServerHandler)
  └── rpc.process (ServerHandler)
       └── rpc.command.server_info (RPCHandler)
```

A consensus round groups its lifecycle spans under a single root
(`consensus.round`); the build/ledger spans run as their own trees:

```
consensus.round                    (root — one per round)
  ├── consensus.phase.open         (open phase)
  ├── consensus.proposal.send      (broadcast proposal)
  ├── consensus.ledger_close       (close event)
  ├── consensus.establish          (establish phase)
  ├── consensus.update_positions   (position updates)
  ├── consensus.check              (threshold check)
  ├── consensus.accept             (accept result)
  │     └── consensus.accept.apply (apply, jtACCEPT thread)
  └── consensus.validation.send    (send validation, follows-from link)

ledger.build                       (build new ledger)
  └── tx.apply                     (apply transaction set)
ledger.validate                    (promote to validated)
ledger.store                       (persist to DB)
```

---

## 5. Prometheus Query Examples

> **See also**: [05-configuration-reference.md](./05-configuration-reference.md) §5.8.7 for correlating Prometheus system metrics with trace-derived metrics.

### Span-Derived Metrics

```promql
# RPC request rate by command (last 5 minutes)
sum by (command) (rate(span_calls_total{span_name=~"rpc.command.*"}[5m]))

# RPC p95 latency by command
histogram_quantile(0.95, sum by (le, command) (rate(span_duration_milliseconds_bucket{span_name=~"rpc.command.*"}[5m])))

# Consensus round duration p95
histogram_quantile(0.95, sum by (le) (rate(span_duration_milliseconds_bucket{span_name="consensus.round"}[5m])))

# Transaction processing rate (local vs relay)
sum by (local) (rate(span_calls_total{span_name="tx.process"}[5m]))

# Trusted vs untrusted proposal rate
sum by (proposal_trusted) (rate(span_calls_total{span_name="peer.proposal.receive"}[5m]))
```

### StatsD Metrics

```promql
# Validated ledger age (should be < 10s)
ledgermaster_validated_ledger_age

# Active peer count
peer_finder_active_inbound_peers + peer_finder_active_outbound_peers

# RPC response time p95
histogram_quantile(0.95, rpc_time_bucket)

# Total network bytes in (rate)
rate(total_bytes_in[5m])

# Operating mode (should be "Full" after startup)
state_accounting_full_duration
```

---

## 5a. Log-Trace Correlation (Phase 8)

> **Plan details**: [06-implementation-phases.md §6.8.1](./06-implementation-phases.md) — motivation, architecture, Mermaid diagrams
> **Task breakdown**: [Phase8_taskList.md](./Phase8_taskList.md) — per-task implementation details

Phase 8 injects OTel trace context into xrpld's `Logs::format()` output, enabling log-trace correlation. When a log line is emitted within an active OTel span, the trace and span identifiers are automatically appended after the severity field:

### Log Format

```
<timestamp> <partition>:<severity> trace_id=<32hex> span_id=<16hex> <message>
```

Example:

```
2024-Jan-15 10:30:45.123456 UTC LedgerMaster:NFO trace_id=abc123def456789012345678abcdef01 span_id=0123456789abcdef Validated ledger 42
```

- **`trace_id=<hex32>`** — 32-character lowercase hex trace identifier. Links to the distributed trace in Tempo.
- **`span_id=<hex16>`** — 16-character lowercase hex span identifier. Identifies the specific span within the trace.
- **Only present** when the log is emitted within an active OTel span. Log lines outside of traced code paths have no trace context fields.

### Implementation

The trace context injection is implemented in `Logs::format()` (`src/libxrpl/basics/Log.cpp`), guarded by `#ifdef XRPL_ENABLE_TELEMETRY`. It checks the thread-local runtime context value directly (via `RuntimeContext::GetCurrent().GetValue(kSpanKey)`) to avoid the heap allocation that `GetSpan()` performs on the no-span path. On threads without an active span, the cost is a thread-local read + variant type check (~15-20ns). On the active-span path, total cost is ~50ns per log call.

### Log Ingestion Pipeline

```
xrpld debug.log -> OTel Collector filelog receiver -> regex_parser -> Loki exporter -> Grafana Loki
```

The OTel Collector's `filelog` receiver tails `debug.log` files and uses a `regex_parser` operator to extract structured fields:

| Field       | Type     | Description                                              |
| ----------- | -------- | -------------------------------------------------------- |
| `timestamp` | datetime | Log timestamp                                            |
| `partition` | string   | Log partition (e.g., `LedgerMaster`, `PeerImp`)          |
| `severity`  | string   | Severity code (`TRC`, `DBG`, `NFO`, `WRN`, `ERR`, `FTL`) |
| `trace_id`  | string   | 32-hex trace identifier (optional)                       |
| `span_id`   | string   | 16-hex span identifier (optional)                        |
| `message`   | string   | Log message body                                         |

### Grafana Correlation

Bidirectional linking between logs and traces is configured via Grafana datasource provisioning:

- **Tempo -> Loki** (`tracesToLogs`): Clicking "Logs for this trace" on a Tempo trace view filters Loki logs by `trace_id`, showing all log lines from that trace.
- **Loki -> Tempo** (`derivedFields`): A regex-based derived field on the Loki datasource extracts `trace_id` from log lines and renders it as a clickable link to the corresponding trace in Tempo.

### Loki Backend

Grafana Loki (v3.4.2) serves as the log storage backend. It receives log entries from the OTel Collector's `otlphttp/loki` exporter via the native OTLP endpoint at `http://loki:3100/otlp`.

### LogQL Query Examples

```logql
# Find all logs for a specific trace
{job="xrpld"} |= "trace_id=abc123def456789012345678abcdef01"

# Error logs with trace context
{job="xrpld"} |= "ERR" |= "trace_id="

# Logs from a specific partition with trace context
{job="xrpld"} |= "LedgerMaster" | regexp `trace_id=(?P<trace_id>[a-f0-9]+)` | trace_id != ""

# Count traced log lines over time
count_over_time({job="xrpld"} |= "trace_id=" [5m])
```

---

## 5b. Internal Metric Gap Fill (Phase 9)

> **Status**: Implemented.
> **Plan details**: [06-implementation-phases.md §6.8.2](./06-implementation-phases.md) — motivation, architecture, third-party context
> **Task breakdown**: [Phase9_taskList.md](./Phase9_taskList.md) — per-task implementation details

Phase 9 fills the metrics that exist inside xrpld but previously lacked time-series export. It
uses a hybrid approach: `beast::insight` extensions for NodeStore I/O plus OTel `ObservableGauge`
async callbacks for new categories.

> **Authoritative metric names live in [§ Phase 9: OTel SDK-Exported Metrics](#phase-9-otel-sdk-exported-metrics-metricsregistry) below.**
> Most internal metrics are emitted as **labeled** gauges — one instrument carrying many logical
> values via a `metric` label (e.g. `cache_metrics{metric="sle_hit_rate"}`,
> `txq_metrics{metric="txq_count"}`, `load_factor_metrics{metric="load_factor"}`,
> `nodestore_state{metric="node_reads_total"}`) — not the flat per-name form. Query the
> labeled names; the flat names (`cache_sle_hit_rate`, `txq_count`, …) are **not** emitted.

#### Server Info (via OTel MetricsRegistry)

| Prometheus Metric                                   | Type  | Labels   | Description                                  |
| --------------------------------------------------- | ----- | -------- | -------------------------------------------- |
| `server_info{metric="server_state"}`                | Gauge | `metric` | Operating mode (0=DISCONNECTED .. 4=FULL)    |
| `server_info{metric="uptime"}`                      | Gauge | `metric` | Seconds since server start                   |
| `server_info{metric="peers"}`                       | Gauge | `metric` | Total connected peers                        |
| `server_info{metric="validated_ledger_seq"}`        | Gauge | `metric` | Validated ledger sequence number             |
| `server_info{metric="ledger_current_index"}`        | Gauge | `metric` | Current open ledger sequence                 |
| `server_info{metric="peer_disconnects_resources"}`  | Gauge | `metric` | Cumulative resource-related peer disconnects |
| `server_info{metric="last_close_proposers"}`        | Gauge | `metric` | Proposers in last closed round               |
| `server_info{metric="last_close_converge_time_ms"}` | Gauge | `metric` | Last close convergence time (milliseconds)   |

#### Build Info (via OTel MetricsRegistry)

| Prometheus Metric             | Type  | Labels    | Description                       |
| ----------------------------- | ----- | --------- | --------------------------------- |
| `build_info{version="<ver>"}` | Gauge | `version` | Info-style metric, always value 1 |

#### Complete Ledger Ranges (via OTel MetricsRegistry)

| Prometheus Metric                             | Type  | Labels          | Description                 |
| --------------------------------------------- | ----- | --------------- | --------------------------- |
| `complete_ledgers{bound="start",index="<N>"}` | Gauge | `bound`,`index` | Start of contiguous range N |
| `complete_ledgers{bound="end",index="<N>"}`   | Gauge | `bound`,`index` | End of contiguous range N   |

#### Database Metrics (via OTel MetricsRegistry)

| Prometheus Metric                           | Type  | Labels   | Description                       |
| ------------------------------------------- | ----- | -------- | --------------------------------- |
| `db_metrics{metric="db_kb_total"}`          | Gauge | `metric` | Total database size (KB)          |
| `db_metrics{metric="db_kb_ledger"}`         | Gauge | `metric` | Ledger database size (KB)         |
| `db_metrics{metric="db_kb_transaction"}`    | Gauge | `metric` | Transaction database size (KB)    |
| `db_metrics{metric="historical_perminute"}` | Gauge | `metric` | Historical ledger fetches per min |

#### Extended Cache Metrics (additions to existing cache_metrics)

| Prometheus Metric                 | Type  | Labels   | Description               |
| --------------------------------- | ----- | -------- | ------------------------- |
| `cache_metrics{metric="al_size"}` | Gauge | `metric` | AcceptedLedger cache size |

#### Extended NodeStore Metrics (additions to existing nodestore_state)

| Prometheus Metric                                  | Type  | Labels   | Description                         |
| -------------------------------------------------- | ----- | -------- | ----------------------------------- |
| `nodestore_state{metric="node_reads_duration_us"}` | Gauge | `metric` | Cumulative read time (microseconds) |
| `nodestore_state{metric="read_request_bundle"}`    | Gauge | `metric` | Read request bundle count           |
| `nodestore_state{metric="read_threads_running"}`   | Gauge | `metric` | Active read threads                 |
| `nodestore_state{metric="read_threads_total"}`     | Gauge | `metric` | Total read threads configured       |

### New Grafana Dashboards (Phase 9)

| Dashboard          | UID          | Data Source | Key Panels                                                        |
| ------------------ | ------------ | ----------- | ----------------------------------------------------------------- |
| Fee Market & TxQ   | `fee-market` | Prometheus  | TxQ depth/capacity, fee levels, load factor breakdown, escalation |
| Job Queue Analysis | `job-queue`  | Prometheus  | Per-job rates, queue wait times, execution times, queue depth     |

---

## 5c. Future: Synthetic Workload Generation & Telemetry Validation (Phase 10)

> **Plan details**: [06-implementation-phases.md §6.8.3](./06-implementation-phases.md) — motivation, architecture
> **Task breakdown**: [Phase10_taskList.md](./Phase10_taskList.md) — per-task implementation details
> **Tools**: [docker/telemetry/workload/](../docker/telemetry/workload/) — RPC load generator, transaction submitter, validation suite, benchmarks

Phase 10 builds a 5-node validator docker-compose harness with RPC load generators, transaction submitters, and automated validation scripts that verify all spans, metrics, dashboards, and log-trace correlation work end-to-end. Includes a benchmark suite comparing telemetry-ON vs telemetry-OFF overhead.

### Running the Validation Suite

```bash
# Full end-to-end validation (start cluster, generate load, validate):
docker/telemetry/workload/run-full-validation.sh --xrpld .build/xrpld

# Validation only (assumes stack and cluster are already running):
python3 docker/telemetry/workload/validate_telemetry.py --report /tmp/report.json

# Performance benchmark (baseline vs telemetry):
docker/telemetry/workload/benchmark.sh --xrpld .build/xrpld --duration 300
```

### Validated Telemetry Inventory

> **Counting note — families vs series.** A _metric family_ is one distinct Prometheus `__name__`
> (histogram `_bucket`/`_count`/`_sum` collapsed to one). A _series_ is a family × its label
> combinations. The legacy overlay-traffic block is the bulk of the count: ~56 message categories ×
> 4 (`_bytes_in/_out`, `_messages_in/_out`) ≈ 224 families on its own. The labeled gauges
> (`cache_metrics{metric}`, …) are few families but many series. Validate against the figures
> below as **families currently emitting** (idle nodes under-report — workload-gated metrics such as
> per-RPC/error counters appear only once exercised, which is Phase 10's purpose).

| Category                       | Expected Count            | Validation Method                | Config File             |
| ------------------------------ | ------------------------- | -------------------------------- | ----------------------- |
| Trace spans                    | ~37 (required + optional) | Tempo API query                  | `expected_spans.json`   |
| Span attributes                | per-span assertion        | Per-span attribute assertion     | `expected_spans.json`   |
| Legacy beast::insight families | ~270 (≈224 traffic)       | Prometheus `__name__` query      | `expected_metrics.json` |
| Native MetricsRegistry         | 35 instruments            | Prometheus query                 | `expected_metrics.json` |
| SpanMetrics RED                | 4 per span                | Prometheus query                 | `expected_metrics.json` |
| Grafana dashboards             | 15                        | Dashboard API "no data" check    | `expected_metrics.json` |
| Log-trace links                | Present                   | Loki query + Tempo reverse check | —                       |

### Performance Overhead Targets

| Metric            | Target       | Measurement Method                  |
| ----------------- | ------------ | ----------------------------------- |
| CPU overhead      | < 3%         | ps avg CPU% baseline vs telemetry   |
| Memory overhead   | < 5MB        | ps peak RSS baseline vs telemetry   |
| RPC p99 latency   | < 2ms impact | server_info round-trip timing       |
| Throughput impact | < 5%         | Ledger close rate comparison        |
| Consensus impact  | < 1%         | Consensus round time p95 comparison |

---

## 5d. Future: Third-Party Data Collection Pipelines (Phase 11)

> **Status**: Planned, not yet implemented.
> **Plan details**: [06-implementation-phases.md §6.8.4](./06-implementation-phases.md) — motivation, architecture, consumer gap analysis
> **Task breakdown**: [Phase11_taskList.md](./Phase11_taskList.md) — per-task implementation details

Phase 11 builds a custom OTel Collector receiver (Go) that polls xrpld's admin RPCs and exports `xrpl_*` metrics for external consumers. No xrpld code changes.

### Exported Metrics (via Custom OTel Collector Receiver)

#### Node Health (from server_info)

| Prometheus Metric                       | Type  | Description                                     |
| --------------------------------------- | ----- | ----------------------------------------------- |
| `xrpl_server_state`                     | Gauge | Operating mode (0=disconnected ... 5=proposing) |
| `xrpl_server_state_duration_seconds`    | Gauge | Seconds in current state                        |
| `xrpl_uptime_seconds`                   | Gauge | Consecutive seconds running                     |
| `xrpl_io_latency_ms`                    | Gauge | I/O subsystem latency                           |
| `xrpl_amendment_blocked`                | Gauge | 1 if amendment-blocked, 0 otherwise             |
| `xrpl_peers_count`                      | Gauge | Connected peers                                 |
| `xrpl_validated_ledger_seq`             | Gauge | Latest validated ledger sequence                |
| `xrpl_validated_ledger_age_seconds`     | Gauge | Seconds since last validated close              |
| `xrpl_last_close_proposers`             | Gauge | Proposers in last consensus round               |
| `xrpl_last_close_converge_time_seconds` | Gauge | Last consensus round duration                   |
| `xrpl_load_factor`                      | Gauge | Transaction cost multiplier                     |
| `xrpl_state_duration_seconds`           | Gauge | Per-state duration (`state` label)              |
| `xrpl_state_transitions_total`          | Gauge | Per-state transition count (`state` label)      |

#### Peer Topology (from peers)

| Prometheus Metric           | Type  | Description                         |
| --------------------------- | ----- | ----------------------------------- |
| `xrpl_peers_inbound_count`  | Gauge | Inbound peer connections            |
| `xrpl_peers_outbound_count` | Gauge | Outbound peer connections           |
| `xrpl_peer_latency_p50_ms`  | Gauge | Median peer latency                 |
| `xrpl_peer_latency_p95_ms`  | Gauge | p95 peer latency                    |
| `xrpl_peer_version_count`   | Gauge | Peers per version (`version` label) |
| `xrpl_peer_diverged_count`  | Gauge | Peers with diverged tracking status |

#### Validator & Amendment (from validators, feature)

| Prometheus Metric                     | Type  | Description                             |
| ------------------------------------- | ----- | --------------------------------------- |
| `xrpl_trusted_validators_count`       | Gauge | UNL validator count                     |
| `xrpl_amendment_enabled_count`        | Gauge | Enabled amendments                      |
| `xrpl_amendment_majority_count`       | Gauge | Amendments with majority                |
| `xrpl_amendment_unsupported_majority` | Gauge | 1 if unsupported amendment has majority |
| `xrpl_validator_list_active`          | Gauge | 1 if validator list is active           |

#### Fee Market (from fee)

| Prometheus Metric                | Type  | Description                           |
| -------------------------------- | ----- | ------------------------------------- |
| `xrpl_fee_open_ledger_fee_drops` | Gauge | Minimum fee for open ledger inclusion |
| `xrpl_fee_median_fee_drops`      | Gauge | Median fee level                      |
| `xrpl_fee_queue_size`            | Gauge | Current transaction queue depth       |
| `xrpl_fee_current_ledger_size`   | Gauge | Transactions in current open ledger   |

#### DEX & AMM (optional, from book_offers, amm_info)

| Prometheus Metric          | Type  | Labels                | Description            |
| -------------------------- | ----- | --------------------- | ---------------------- |
| `xrpl_amm_tvl_drops`       | Gauge | `pool="<id>"`         | Total value locked     |
| `xrpl_amm_trading_fee`     | Gauge | `pool="<id>"`         | Pool trading fee (bps) |
| `xrpl_orderbook_bid_depth` | Gauge | `pair="<base/quote>"` | Total bid volume       |
| `xrpl_orderbook_ask_depth` | Gauge | `pair="<base/quote>"` | Total ask volume       |
| `xrpl_orderbook_spread`    | Gauge | `pair="<base/quote>"` | Best bid-ask spread    |

### Phase 9: OTel SDK-Exported Metrics (MetricsRegistry)

Phase 9 introduces the `MetricsRegistry` class (`src/xrpld/telemetry/MetricsRegistry.h/.cpp`)
which registers metrics directly with the OpenTelemetry Metrics SDK. These are exported
via OTLP/HTTP to the OTel Collector and scraped by Prometheus.

#### NodeStore I/O (Observable Gauge — `nodestore_state`)

| Prometheus Metric                              | Type  | Labels   | Description                          |
| ---------------------------------------------- | ----- | -------- | ------------------------------------ |
| `nodestore_state{metric="node_reads_total"}`   | Gauge | `metric` | Cumulative NodeStore read operations |
| `nodestore_state{metric="node_reads_hit"}`     | Gauge | `metric` | Reads served from cache              |
| `nodestore_state{metric="node_writes"}`        | Gauge | `metric` | Cumulative write operations          |
| `nodestore_state{metric="node_written_bytes"}` | Gauge | `metric` | Cumulative bytes written             |
| `nodestore_state{metric="node_read_bytes"}`    | Gauge | `metric` | Cumulative bytes read                |
| `nodestore_state{metric="write_load"}`         | Gauge | `metric` | Current write load score             |
| `nodestore_state{metric="read_queue"}`         | Gauge | `metric` | Items in read prefetch queue         |

#### Cache Hit Rates & Sizes (Observable Gauge — `cache_metrics`)

| Prometheus Metric                             | Type  | Labels   | Description                   |
| --------------------------------------------- | ----- | -------- | ----------------------------- |
| `cache_metrics{metric="sle_hit_rate"}`        | Gauge | `metric` | SLE cache hit rate (0.0-1.0)  |
| `cache_metrics{metric="ledger_hit_rate"}`     | Gauge | `metric` | Ledger cache hit rate         |
| `cache_metrics{metric="al_hit_rate"}`         | Gauge | `metric` | AcceptedLedger cache hit rate |
| `cache_metrics{metric="treenode_cache_size"}` | Gauge | `metric` | SHAMap TreeNode cache entries |
| `cache_metrics{metric="treenode_track_size"}` | Gauge | `metric` | Tracked tree nodes            |
| `cache_metrics{metric="fullbelow_size"}`      | Gauge | `metric` | FullBelow cache entries       |

#### Transaction Queue (Observable Gauge — `txq_metrics`)

| Prometheus Metric                                    | Type  | Labels   | Description                      |
| ---------------------------------------------------- | ----- | -------- | -------------------------------- |
| `txq_metrics{metric="txq_count"}`                    | Gauge | `metric` | Transactions currently in queue  |
| `txq_metrics{metric="txq_max_size"}`                 | Gauge | `metric` | Maximum queue capacity           |
| `txq_metrics{metric="txq_in_ledger"}`                | Gauge | `metric` | Transactions in open ledger      |
| `txq_metrics{metric="txq_per_ledger"}`               | Gauge | `metric` | Expected transactions per ledger |
| `txq_metrics{metric="txq_reference_fee_level"}`      | Gauge | `metric` | Reference fee level              |
| `txq_metrics{metric="txq_min_processing_fee_level"}` | Gauge | `metric` | Minimum fee to get processed     |
| `txq_metrics{metric="txq_med_fee_level"}`            | Gauge | `metric` | Median fee level in queue        |
| `txq_metrics{metric="txq_open_ledger_fee_level"}`    | Gauge | `metric` | Open ledger fee escalation level |

#### Per-RPC Method Metrics (Synchronous Counters/Histogram)

| Prometheus Metric           | Type      | Labels            | Description                      |
| --------------------------- | --------- | ----------------- | -------------------------------- |
| `rpc_method_started_total`  | Counter   | `method="<name>"` | RPC calls started                |
| `rpc_method_finished_total` | Counter   | `method="<name>"` | RPC calls completed successfully |
| `rpc_method_errored_total`  | Counter   | `method="<name>"` | RPC calls that errored           |
| `rpc_method_us`             | Histogram | `method="<name>"` | Execution time distribution (us) |

#### Per-Job-Type Metrics (Synchronous Counters/Histogram)

| Prometheus Metric    | Type      | Labels              | Description                       |
| -------------------- | --------- | ------------------- | --------------------------------- |
| `job_queued_total`   | Counter   | `job_type="<name>"` | Jobs enqueued                     |
| `job_started_total`  | Counter   | `job_type="<name>"` | Jobs started                      |
| `job_finished_total` | Counter   | `job_type="<name>"` | Jobs completed                    |
| `job_queued_us`      | Histogram | `job_type="<name>"` | Queue wait time distribution (us) |
| `job_running_us`     | Histogram | `job_type="<name>"` | Execution time distribution (us)  |

#### Counted Object Instances (Observable Gauge — `object_count`)

| Prometheus Metric                      | Type  | Labels          | Description                    |
| -------------------------------------- | ----- | --------------- | ------------------------------ |
| `object_count{type="transaction"}`     | Gauge | `type="<name>"` | Live Transaction objects       |
| `object_count{type="ledger"}`          | Gauge | `type="<name>"` | Live Ledger objects            |
| `object_count{type="nodeobject"}`      | Gauge | `type="<name>"` | Live NodeObject instances      |
| `object_count{type="sttx"}`            | Gauge | `type="<name>"` | Serialized transaction objects |
| `object_count{type="stledgerentry"}`   | Gauge | `type="<name>"` | Serialized ledger entries      |
| `object_count{type="inboundledger"}`   | Gauge | `type="<name>"` | Ledgers being fetched          |
| `object_count{type="pathfinder"}`      | Gauge | `type="<name>"` | Active pathfinding operations  |
| `object_count{type="pathrequest"}`     | Gauge | `type="<name>"` | Active path requests           |
| `object_count{type="hashrouterentry"}` | Gauge | `type="<name>"` | Hash router entries            |

#### Load Factor Breakdown (Observable Gauge — `load_factor_metrics`)

| Prometheus Metric                                          | Type  | Labels   | Description                             |
| ---------------------------------------------------------- | ----- | -------- | --------------------------------------- |
| `load_factor_metrics{metric="load_factor"}`                | Gauge | `metric` | Combined transaction cost multiplier    |
| `load_factor_metrics{metric="load_factor_server"}`         | Gauge | `metric` | Server + cluster + network contribution |
| `load_factor_metrics{metric="load_factor_local"}`          | Gauge | `metric` | Local server load only                  |
| `load_factor_metrics{metric="load_factor_net"}`            | Gauge | `metric` | Network-wide load estimate              |
| `load_factor_metrics{metric="load_factor_cluster"}`        | Gauge | `metric` | Cluster peer load                       |
| `load_factor_metrics{metric="load_factor_fee_escalation"}` | Gauge | `metric` | Open ledger fee escalation              |
| `load_factor_metrics{metric="load_factor_fee_queue"}`      | Gauge | `metric` | Queue entry fee level                   |

#### Prometheus Query Examples (Phase 9)

```promql
# NodeStore cache hit ratio
nodestore_state{metric="node_reads_hit"} / nodestore_state{metric="node_reads_total"}

# RPC error rate for server_info
rate(rpc_method_errored_total{method="server_info"}[5m])

# Job queue wait time p95
histogram_quantile(0.95, sum by (le) (rate(job_queued_us_bucket[5m])))

# TxQ utilization percentage
txq_metrics{metric="txq_count"} / txq_metrics{metric="txq_max_size"}

# High load factor alert candidate
load_factor_metrics{metric="load_factor"} > 5
```

### Phase 7+: External Dashboard Parity Metrics

> **Source**: [External Dashboard Parity Spec](./06-implementation-phases.md#appendix-external-dashboard-parity) — metrics inspired by the community [xrpl-validator-dashboard](https://github.com/realgrapedrop/xrpl-validator-dashboard).
>
> **Task breakdown**: Phase 7 Tasks 7.9-7.16 (implementation), Phase 9 Tasks 9.11-9.13 (dashboards)

These metrics fill gaps identified by comparing xrpld's internal observability with the community external dashboard's 86-metric coverage. All are exported via the OTel Metrics SDK (same `PeriodicMetricReader` as Phase 9 metrics).

#### Validation Agreement (Observable Gauge — `validation_agreement`)

| Prometheus Metric                                  | Type   | Labels   | Description                             |
| -------------------------------------------------- | ------ | -------- | --------------------------------------- |
| `validation_agreement{metric="agreement_pct_1h"}`  | Double | `metric` | Rolling 1h agreement percentage (0-100) |
| `validation_agreement{metric="agreement_pct_24h"}` | Double | `metric` | Rolling 24h agreement percentage        |
| `validation_agreement{metric="agreements_1h"}`     | Int64  | `metric` | Agreed validations in 1h window         |
| `validation_agreement{metric="missed_1h"}`         | Int64  | `metric` | Missed validations in 1h window         |
| `validation_agreement{metric="agreements_24h"}`    | Int64  | `metric` | Agreed validations in 24h window        |
| `validation_agreement{metric="missed_24h"}`        | Int64  | `metric` | Missed validations in 24h window        |

Data source: `ValidationTracker` class with 8s grace period and 5m late repair window.

#### Validator Health (Observable Gauge — `validator_health`)

| Prometheus Metric                              | Type   | Labels   | Description                    |
| ---------------------------------------------- | ------ | -------- | ------------------------------ |
| `validator_health{metric="amendment_blocked"}` | Int64  | `metric` | 1 if amendment-blocked, else 0 |
| `validator_health{metric="unl_blocked"}`       | Int64  | `metric` | 1 if UNL-blocked, else 0       |
| `validator_health{metric="unl_expiry_days"}`   | Double | `metric` | Days until UNL list expires    |
| `validator_health{metric="validation_quorum"}` | Int64  | `metric` | Validation quorum threshold    |

#### Peer Quality (Observable Gauge — `peer_quality`)

| Prometheus Metric                                 | Type   | Labels   | Description                          |
| ------------------------------------------------- | ------ | -------- | ------------------------------------ |
| `peer_quality{metric="peer_latency_p90_ms"}`      | Double | `metric` | P90 peer latency in milliseconds     |
| `peer_quality{metric="peers_insane_count"}`       | Int64  | `metric` | Peers with diverged tracking status  |
| `peer_quality{metric="peers_higher_version_pct"}` | Double | `metric` | % of peers on newer xrpld version    |
| `peer_quality{metric="upgrade_recommended"}`      | Int64  | `metric` | 1 if >60% of peers are newer version |

#### Ledger Economy (Observable Gauge — `ledger_economy`)

| Prometheus Metric                             | Type   | Labels   | Description                        |
| --------------------------------------------- | ------ | -------- | ---------------------------------- |
| `ledger_economy{metric="base_fee_xrp"}`       | Double | `metric` | Base transaction fee in drops      |
| `ledger_economy{metric="reserve_base_xrp"}`   | Double | `metric` | Account reserve in drops           |
| `ledger_economy{metric="reserve_inc_xrp"}`    | Double | `metric` | Owner reserve increment in drops   |
| `ledger_economy{metric="ledger_age_seconds"}` | Double | `metric` | Seconds since last validated close |
| `ledger_economy{metric="transaction_rate"}`   | Double | `metric` | Smoothed transaction rate (tx/s)   |

#### State Tracking (Observable Gauge — `state_tracking`)

| Prometheus Metric                                        | Type   | Labels   | Description                            |
| -------------------------------------------------------- | ------ | -------- | -------------------------------------- |
| `state_tracking{metric="state_value"}`                   | Int64  | `metric` | Numeric state 0-6 (see encoding below) |
| `state_tracking{metric="time_in_current_state_seconds"}` | Double | `metric` | Duration in current state              |

State value encoding: 0=disconnected, 1=connected, 2=syncing, 3=tracking, 4=full, 5=validating (FULL + validating), 6=proposing (FULL + proposing).

#### Storage Detail (Observable Gauge — `storage_detail`)

| Prometheus Metric                     | Type  | Labels   | Description            |
| ------------------------------------- | ----- | -------- | ---------------------- |
| `storage_detail{metric="nudb_bytes"}` | Int64 | `metric` | NuDB backend file size |

#### Synchronous Counters (Phase 7+)

| Prometheus Metric           | Type    | Description                  | Increment Site   |
| --------------------------- | ------- | ---------------------------- | ---------------- |
| `ledgers_closed_total`      | Counter | Ledgers closed by consensus  | RCLConsensus.cpp |
| `validations_sent_total`    | Counter | Validations sent             | RCLConsensus.cpp |
| `validations_checked_total` | Counter | Network validations observed | LedgerMaster.cpp |
| `state_changes_total`       | Counter | Operating mode transitions   | NetworkOPs.cpp   |

Lifetime tallies exported as monotonic **ObservableCounters** (not synchronous
counters), observed from an existing cumulative source each collection cycle:

| Prometheus Metric             | Type              | Description                                | Source                                               |
| ----------------------------- | ----------------- | ------------------------------------------ | ---------------------------------------------------- |
| `validation_agreements_total` | ObservableCounter | Lifetime validations that initially agreed | ValidationTracker.cpp                                |
| `validation_missed_total`     | ObservableCounter | Lifetime validations that initially missed | ValidationTracker.cpp                                |
| `jq_trans_overflow_total`     | ObservableCounter | Job queue transaction overflows            | Overlay::getJqTransOverflow (PeerImp.cpp increments) |

> **Counting semantics (initial-classification only):** each reconciled ledger increments exactly
> one of these two counters, at first classification. A later late-repair (miss → agreement) does
> **not** move either counter — keeping both strictly monotonic (a Prometheus `_total` must never
> decrease) and additive (`agreements_total + missed_total` = ledgers reconciled). The
> repair-aware, windowed view remains on `validation_agreement{metric="…"}`.

#### Span Attribute Enrichments (Phases 2-4)

| Span Name                   | New Attribute                        | Type   | Source                   |
| --------------------------- | ------------------------------------ | ------ | ------------------------ |
| `rpc.command.*`             | `xrpl.node.amendment_blocked`        | bool   | Phase 2 — RPCHandler.cpp |
| `rpc.command.*`             | `xrpl.node.server_state`             | string | Phase 2 — RPCHandler.cpp |
| `tx.receive`                | `xrpl.peer.version`                  | string | Phase 3 — PeerImp.cpp    |
| `consensus.validation.send` | `xrpl.validation.ledger_hash`        | string | Phase 4 — RCLConsensus   |
| `consensus.validation.send` | `xrpl.validation.full`               | bool   | Phase 4 — RCLConsensus   |
| `peer.validation.receive`   | `xrpl.peer.validation.ledger_hash`   | string | Phase 4 — PeerImp.cpp    |
| `peer.validation.receive`   | `xrpl.peer.validation.full`          | bool   | Phase 4 — PeerImp.cpp    |
| `consensus.accept`          | `xrpl.consensus.validation_quorum`   | int64  | Phase 4 — RCLConsensus   |
| `consensus.accept`          | `xrpl.consensus.proposers_validated` | int64  | Phase 4 — RCLConsensus   |

### New Grafana Dashboards (Phase 9)

| Dashboard                            | UID                | Data Source | Key Panels                                                                                                               |
| ------------------------------------ | ------------------ | ----------- | ------------------------------------------------------------------------------------------------------------------------ |
| Fee Market & TxQ                     | `fee-market`       | Prometheus  | TxQ depth/capacity, fee levels, load factor breakdown                                                                    |
| Job Queue Analysis                   | `job-queue`        | Prometheus  | Per-job rates, queue wait times, execution times                                                                         |
| RPC Performance (per-method section) | `rpc-performance`  | Prometheus  | Per-method call rates, error rates, latency distributions (added as a section to the existing RPC Performance dashboard) |
| Validator Health                     | `validator-health` | Prometheus  | Agreement %, validation rate, amendment/UNL, state                                                                       |
| Peer Quality                         | `peer-quality`     | Prometheus  | P90 latency, insane peers, version awareness, disconnects                                                                |

### Updated Grafana Dashboards (Phase 9)

| Dashboard            | UID                        | New Panels Added                                                     |
| -------------------- | -------------------------- | -------------------------------------------------------------------- |
| Node Health (StatsD) | `xrpld-statsd-node-health` | NodeStore I/O, cache hit rates, object instance counts               |
| System Node Health   | `node-health`              | Ledger economy row: base fee, reserves, ledger age, transaction rate |

### New Grafana Dashboards (Phase 11)

| Dashboard          | UID                         | Data Source | Key Panels                                                             |
| ------------------ | --------------------------- | ----------- | ---------------------------------------------------------------------- |
| Validator Health   | `validator-health`          | Prometheus  | Server state timeline, proposer count, converge time, amendment voting |
| Network Topology   | `xrpld-network-topology`    | Prometheus  | Peer count, version distribution, latency distribution, diverged peers |
| Fee Market (Ext)   | `xrpld-fee-market-external` | Prometheus  | Fee levels, queue depth, load factor breakdown, escalation timeline    |
| DEX & AMM Overview | `xrpld-dex-amm`             | Prometheus  | AMM TVL, order book depth, spread trends, trading fee revenue          |

### Prometheus Alerting Rules (Phase 11)

| Alert Name                         | Severity | Condition                                                   | For |
| ---------------------------------- | -------- | ----------------------------------------------------------- | --- |
| `XRPLServerNotFull`                | Critical | `xrpl_server_state < 4` for 15m                             | 15m |
| `XRPLAmendmentBlocked`             | Critical | `xrpl_amendment_blocked == 1`                               | 1m  |
| `XRPLNoPeers`                      | Critical | `xrpl_peers_count == 0`                                     | 5m  |
| `XRPLLedgerStale`                  | Critical | `xrpl_validated_ledger_age_seconds > 120`                   | 2m  |
| `XRPLHighIOLatency`                | Critical | `xrpl_io_latency_ms > 100`                                  | 5m  |
| `XRPLUnsupportedAmendmentMajority` | Critical | `xrpl_amendment_unsupported_majority == 1`                  | 1m  |
| `XRPLLowPeerCount`                 | Warning  | `xrpl_peers_count < 10`                                     | 15m |
| `XRPLHighLoadFactor`               | Warning  | `xrpl_load_factor > 10`                                     | 10m |
| `XRPLSlowConsensus`                | Warning  | `xrpl_last_close_converge_time_seconds > 6`                 | 5m  |
| `XRPLValidatorListExpiring`        | Warning  | `(xrpl_validator_list_expiration_seconds - time()) < 86400` | 1h  |
| `XRPLStateFlapping`                | Warning  | `rate(xrpl_state_transitions_total{state="full"}[1h]) > 2`  | 30m |

---

## 6. Known Issues

| Issue                                                              | Impact                                           | Status                                                               |
| ------------------------------------------------------------------ | ------------------------------------------------ | -------------------------------------------------------------------- |
| `warn` and `drop` metrics use non-standard StatsD `\|m` meter type | Metrics silently dropped by OTel StatsD receiver | Phase 6 Task 6.1 — needs `\|m` → `\|c` change in StatsDCollector.cpp |
| `jobq_job_count` may not emit in standalone mode                   | Missing from Prometheus in some test configs     | Requires active job queue activity                                   |
| `rpc_requests` depends on `[insight]` config                       | Zero series if StatsD not configured             | Requires `[insight] server=statsd` in xrpld.cfg                      |
| Peer tracing enabled by default                                    | `peer.*` spans emit unless `trace_peer=0`        | High volume — set `trace_peer=0` to opt out on busy mainnet nodes    |

---

## 7. Privacy and Data Collection

The telemetry system is designed with privacy in mind:

- **No private keys** are ever included in spans or metrics
- **No account balances** or financial data is traced
- **Transaction hashes** are included (public on-ledger data) but not transaction contents
- **Peer IDs** are internal identifiers, not IP addresses
- **All telemetry is opt-in** — disabled by default at build time (`-Dtelemetry=OFF`)
- **Sampling** — head sampling is fixed at 1.0 (sample everything); reduce data volume with collector-side tail sampling
- **Data stays local** — the default stack sends data to `localhost` only

---

## 8. Configuration Quick Reference

> **Full reference**: [05-configuration-reference.md](./05-configuration-reference.md) §5.1 for all `[telemetry]` options with defaults, the config parser implementation, and collector YAML configurations (dev and production).

### Minimal Setup (development)

```ini
[telemetry]
enabled=1

[insight]
server=statsd
address=127.0.0.1:8125
prefix=xrpld
```

### Production Setup

```ini
[telemetry]
enabled=1
endpoint=http://otel-collector:4318/v1/traces
trace_peer=0
batch_size=1024
max_queue_size=4096

[insight]
server=statsd
address=otel-collector:8125
prefix=xrpld
```

### Trace Category Toggle

| Config Key           | Default | Controls                     |
| -------------------- | ------- | ---------------------------- |
| `trace_rpc`          | `1`     | `rpc.*` spans                |
| `trace_transactions` | `1`     | `tx.*` spans                 |
| `trace_consensus`    | `1`     | `consensus.*` spans          |
| `trace_ledger`       | `1`     | `ledger.*` spans             |
| `trace_peer`         | `1`     | `peer.*` spans (high volume) |
