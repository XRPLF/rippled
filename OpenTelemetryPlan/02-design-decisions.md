# Design Decisions

> **Parent Document**: [OpenTelemetryPlan.md](./OpenTelemetryPlan.md)
> **Related**: [Architecture Analysis](./01-architecture-analysis.md) | [Code Samples](./04-code-samples.md)

---

## 2.1 OpenTelemetry Components

### 2.1.1 SDK Selection

**Primary Choice**: OpenTelemetry C++ SDK (`opentelemetry-cpp`)

| Component                               | Purpose                | Required    |
| --------------------------------------- | ---------------------- | ----------- |
| `opentelemetry-cpp::api`                | Tracing API headers    | Yes         |
| `opentelemetry-cpp::sdk`                | SDK implementation     | Yes         |
| `opentelemetry-cpp::ext`                | Extensions (exporters) | Yes         |
| `opentelemetry-cpp::otlp_grpc_exporter` | OTLP/gRPC export       | Recommended |
| `opentelemetry-cpp::otlp_http_exporter` | OTLP/HTTP export       | Alternative |

### 2.1.2 Instrumentation Strategy

**Manual Instrumentation** (recommended):

| Approach   | Pros                                                              | Cons                                                    |
| ---------- | ----------------------------------------------------------------- | ------------------------------------------------------- |
| **Manual** | Precise control, optimized placement, rippled-specific attributes | More development effort                                 |
| **Auto**   | Less code, automatic coverage                                     | Less control, potential overhead, limited customization |

---

## 2.2 Exporter Configuration

```mermaid
flowchart TB
    subgraph nodes["rippled Nodes"]
        node1["rippled<br/>Node 1"]
        node2["rippled<br/>Node 2"]
        node3["rippled<br/>Node 3"]
    end

    collector["OpenTelemetry<br/>Collector<br/>(sidecar or standalone)"]

    subgraph backends["Observability Backends"]
        jaeger["Jaeger<br/>(Dev)"]
        tempo["Tempo<br/>(Prod)"]
        elastic["Elastic<br/>APM"]
    end

    node1 -->|"OTLP/gRPC<br/>:4317"| collector
    node2 -->|"OTLP/gRPC<br/>:4317"| collector
    node3 -->|"OTLP/gRPC<br/>:4317"| collector

    collector --> jaeger
    collector --> tempo
    collector --> elastic

    style nodes fill:#0d47a1,stroke:#082f6a,color:#ffffff
    style backends fill:#1b5e20,stroke:#0d3d14,color:#ffffff
    style collector fill:#bf360c,stroke:#8c2809,color:#ffffff
```

### 2.2.1 OTLP/gRPC (Recommended)

```cpp
// Configuration for OTLP over gRPC
namespace otlp = opentelemetry::exporter::otlp;

otlp::OtlpGrpcExporterOptions opts;
opts.endpoint = "localhost:4317";
opts.use_ssl_credentials = true;
opts.ssl_credentials_cacert_path = "/path/to/ca.crt";
```

### 2.2.2 OTLP/HTTP (Alternative)

```cpp
// Configuration for OTLP over HTTP
namespace otlp = opentelemetry::exporter::otlp;

otlp::OtlpHttpExporterOptions opts;
opts.url = "http://localhost:4318/v1/traces";
opts.content_type = otlp::HttpRequestContentType::kJson;  // or kBinary
```

---

## 2.3 Span Naming Conventions

### 2.3.1 Naming Schema

```
<component>.<operation>[.<sub-operation>]
```

**Examples**:

- `tx.receive` - Transaction received from peer
- `consensus.phase.establish` - Consensus establish phase
- `rpc.command.server_info` - server_info RPC command

### 2.3.2 Complete Span Catalog

```yaml
# Transaction Spans
tx:
  receive: "Transaction received from network"
  validate: "Transaction signature/format validation"
  process: "Full transaction processing"
  relay: "Transaction relay to peers"
  apply: "Apply transaction to ledger"

# Consensus Spans
consensus:
  round: "Complete consensus round"
  phase:
    open: "Open phase - collecting transactions"
    establish: "Establish phase - reaching agreement"
    accept: "Accept phase - applying consensus"
  proposal:
    receive: "Receive peer proposal"
    send: "Send our proposal"
  validation:
    receive: "Receive peer validation"
    send: "Send our validation"

# RPC Spans
rpc:
  request: "HTTP/WebSocket request handling"
  command:
    "*": "Specific RPC command (dynamic)"

# Peer Spans
peer:
  connect: "Peer connection establishment"
  disconnect: "Peer disconnection"
  message:
    send: "Send protocol message"
    receive: "Receive protocol message"

# Ledger Spans
ledger:
  acquire: "Ledger acquisition from network"
  build: "Build new ledger"
  validate: "Ledger validation"
  close: "Close ledger"

# Job Spans
job:
  enqueue: "Job added to queue"
  execute: "Job execution"
```

---

## 2.4 Attribute Schema

### 2.4.1 Resource Attributes (Set Once at Startup)

```cpp
// Standard OpenTelemetry semantic conventions
resource::SemanticConventions::SERVICE_NAME        = "rippled"
resource::SemanticConventions::SERVICE_VERSION     = BuildInfo::getVersionString()
resource::SemanticConventions::SERVICE_INSTANCE_ID = <node_public_key_base58>

// Custom rippled attributes
"xrpl.network.id"      = <network_id>           // e.g., 0 for mainnet
"xrpl.network.type"    = "mainnet" | "testnet" | "devnet" | "standalone"
"xrpl.node.type"       = "validator" | "stock" | "reporting"
"xrpl.node.cluster"    = <cluster_name>         // If clustered
```

### 2.4.2 Span Attributes by Category

#### Transaction Attributes

```cpp
"xrpl.tx.hash"         = string   // Transaction hash (hex)
"xrpl.tx.type"         = string   // "Payment", "OfferCreate", etc.
"xrpl.tx.account"      = string   // Source account (redacted in prod)
"xrpl.tx.sequence"     = int64    // Account sequence number
"xrpl.tx.fee"          = int64    // Fee in drops
"xrpl.tx.result"       = string   // "tesSUCCESS", "tecPATH_DRY", etc.
"xrpl.tx.ledger_index" = int64    // Ledger containing transaction
```

#### Consensus Attributes

```cpp
"xrpl.consensus.round"          = int64    // Round number
"xrpl.consensus.phase"          = string   // "open", "establish", "accept"
"xrpl.consensus.mode"           = string   // "proposing", "observing", etc.
"xrpl.consensus.proposers"      = int64    // Number of proposers
"xrpl.consensus.ledger.prev"    = string   // Previous ledger hash
"xrpl.consensus.ledger.seq"     = int64    // Ledger sequence
"xrpl.consensus.tx_count"       = int64    // Transactions in consensus set
"xrpl.consensus.duration_ms"    = float64  // Round duration
```

#### RPC Attributes

```cpp
"xrpl.rpc.command"     = string   // Command name
"xrpl.rpc.version"     = int64    // API version
"xrpl.rpc.role"        = string   // "admin" or "user"
"xrpl.rpc.params"      = string   // Sanitized parameters (optional)
```

#### Peer & Message Attributes

```cpp
"xrpl.peer.id"            = string   // Peer public key (base58)
"xrpl.peer.address"       = string   // IP:port
"xrpl.peer.latency_ms"    = float64  // Measured latency
"xrpl.peer.cluster"       = string   // Cluster name if clustered
"xrpl.message.type"       = string   // Protocol message type name
"xrpl.message.size_bytes" = int64    // Message size
"xrpl.message.compressed" = bool     // Whether compressed
```

#### Ledger & Job Attributes

```cpp
"xrpl.ledger.hash"       = string   // Ledger hash
"xrpl.ledger.index"      = int64    // Ledger sequence/index
"xrpl.ledger.close_time" = int64    // Close time (epoch)
"xrpl.ledger.tx_count"   = int64    // Transaction count
"xrpl.job.type"          = string   // Job type name
"xrpl.job.queue_ms"      = float64  // Time spent in queue
"xrpl.job.worker"        = int64    // Worker thread ID
```

### 2.4.3 Data Collection Summary

The following table summarizes what data is collected by category:

| Category        | Attributes Collected                                                 | Purpose                     |
| --------------- | -------------------------------------------------------------------- | --------------------------- |
| **Transaction** | `tx.hash`, `tx.type`, `tx.result`, `tx.fee`, `ledger_index`          | Trace transaction lifecycle |
| **Consensus**   | `round`, `phase`, `mode`, `proposers` (public keys), `duration_ms`   | Analyze consensus timing    |
| **RPC**         | `command`, `version`, `status`, `duration_ms`                        | Monitor RPC performance     |
| **Peer**        | `peer.id` (public key), `latency_ms`, `message.type`, `message.size` | Network topology analysis   |
| **Ledger**      | `ledger.hash`, `ledger.index`, `close_time`, `tx_count`              | Ledger progression tracking |
| **Job**         | `job.type`, `queue_ms`, `worker`                                     | JobQueue performance        |

### 2.4.4 Privacy & Sensitive Data Policy

OpenTelemetry instrumentation is designed to collect **operational metadata only**, never sensitive content.

#### Data NOT Collected

The following data is explicitly **excluded** from telemetry collection:

| Excluded Data           | Reason                                    |
| ----------------------- | ----------------------------------------- |
| **Private Keys**        | Never exposed; not relevant to tracing    |
| **Account Balances**    | Financial data; privacy sensitive         |
| **Transaction Amounts** | Financial data; privacy sensitive         |
| **Raw TX Payloads**     | May contain sensitive memo/data fields    |
| **Personal Data**       | No PII collected                          |
| **IP Addresses**        | Configurable; excluded by default in prod |

#### Privacy Protection Mechanisms

| Mechanism                     | Description                                                               |
| ----------------------------- | ------------------------------------------------------------------------- |
| **Account Hashing**           | `xrpl.tx.account` is hashed at collector level before storage             |
| **Configurable Redaction**    | Sensitive fields can be excluded via `[telemetry]` config section         |
| **Sampling**                  | Only 10% of traces recorded by default, reducing data exposure            |
| **Local Control**             | Node operators have full control over what gets exported                  |
| **No Raw Payloads**           | Transaction content is never recorded, only metadata (hash, type, result) |
| **Collector-Level Filtering** | Additional redaction/hashing can be configured at OTel Collector          |

#### Collector-Level Data Protection

The OpenTelemetry Collector can be configured to hash or redact sensitive attributes before export:

```yaml
processors:
  attributes:
    actions:
      # Hash account addresses before storage
      - key: xrpl.tx.account
        action: hash
      # Remove IP addresses entirely
      - key: xrpl.peer.address
        action: delete
      # Redact specific fields
      - key: xrpl.rpc.params
        action: delete
```

#### Configuration Options for Privacy

In `rippled.cfg`, operators can control data collection granularity:

```ini
[telemetry]
enabled=1

# Disable collection of specific components
trace_transactions=1
trace_consensus=1
trace_rpc=1
trace_peer=0          # Disable peer tracing (high volume, includes addresses)

# Redact specific attributes
redact_account=1      # Hash account addresses before export
redact_peer_address=1 # Remove peer IP addresses
```

> **Key Principle**: Telemetry collects **operational metadata** (timing, counts, hashes) — never **sensitive content** (keys, balances, amounts, raw payloads).

---

## 2.5 Context Propagation Design

### 2.5.1 Propagation Boundaries

```mermaid
flowchart TB
    subgraph http["HTTP/WebSocket (RPC)"]
        w3c["W3C Trace Context Headers:<br/>traceparent: 00-{trace_id}-{span_id}-{flags}<br/>tracestate: rippled=<state>"]
    end

    subgraph protobuf["Protocol Buffers (P2P)"]
        proto["message TraceContext {<br/>  bytes trace_id = 1;  // 16 bytes<br/>  bytes span_id = 2;   // 8 bytes<br/>  uint32 trace_flags = 3;<br/>  string trace_state = 4;<br/>}"]
    end

    subgraph jobqueue["JobQueue (Internal Async)"]
        job["Context captured at job creation,<br/>restored at execution<br/><br/>class Job {<br/>  opentelemetry::context::Context traceContext_;<br/>};"]
    end

    style http fill:#0d47a1,stroke:#082f6a,color:#ffffff
    style protobuf fill:#1b5e20,stroke:#0d3d14,color:#ffffff
    style jobqueue fill:#bf360c,stroke:#8c2809,color:#ffffff
```

---

## 2.6 Integration with Existing Observability

### 2.6.1 Existing Frameworks Comparison

rippled already has two observability mechanisms. OpenTelemetry complements (not replaces) them:

| Aspect                | PerfLog                       | Beast Insight (StatsD)       | OpenTelemetry             |
| --------------------- | ----------------------------- | ---------------------------- | ------------------------- |
| **Type**              | Logging                       | Metrics                      | Distributed Tracing       |
| **Data**              | JSON log entries              | Counters, gauges, histograms | Spans with context        |
| **Scope**             | Single node                   | Single node                  | **Cross-node**            |
| **Output**            | `perf.log` file               | StatsD server                | OTLP Collector            |
| **Question answered** | "What happened on this node?" | "How many? How fast?"        | "What was the journey?"   |
| **Correlation**       | By timestamp                  | By metric name               | By `trace_id`             |
| **Overhead**          | Low (file I/O)                | Low (UDP packets)            | Low-Medium (configurable) |

### 2.6.2 What Each Framework Does Best

#### PerfLog

- **Purpose**: Detailed local event logging for RPC and job execution
- **Strengths**:
  - Rich JSON output with timing data
  - Already integrated in RPC handlers
  - File-based, no external dependencies
- **Limitations**:
  - Single-node only (no cross-node correlation)
  - No parent-child relationships between events
  - Manual log parsing required

```json
// Example PerfLog entry
{
  "time": "2024-01-15T10:30:00.123Z",
  "method": "submit",
  "duration_us": 1523,
  "result": "tesSUCCESS"
}
```

#### Beast Insight (StatsD)

- **Purpose**: Real-time metrics for monitoring dashboards
- **Strengths**:
  - Aggregated metrics (counters, gauges, histograms)
  - Low overhead (UDP, fire-and-forget)
  - Good for alerting thresholds
- **Limitations**:
  - No request-level detail
  - No causal relationships
  - Single-node perspective

```cpp
// Example StatsD usage in rippled
insight.increment("rpc.submit.count");
insight.gauge("ledger.age", age);
insight.timing("consensus.round", duration);
```

#### OpenTelemetry (NEW)

- **Purpose**: Distributed request tracing across nodes
- **Strengths**:
  - **Cross-node correlation** via `trace_id`
  - Parent-child span relationships
  - Rich attributes per span
  - Industry standard (CNCF)
- **Limitations**:
  - Requires collector infrastructure
  - Higher complexity than logging

```cpp
// Example OpenTelemetry span
auto span = telemetry.startSpan("tx.relay");
span->SetAttribute("tx.hash", hash);
span->SetAttribute("peer.id", peerId);
// Span automatically linked to parent via context
```

### 2.6.3 When to Use Each

| Scenario                                | PerfLog    | StatsD | OpenTelemetry |
| --------------------------------------- | ---------- | ------ | ------------- |
| "How many TXs per second?"              | ❌         | ✅     | ❌            |
| "What's the p99 RPC latency?"           | ❌         | ✅     | ✅            |
| "Why was this specific TX slow?"        | ⚠️ partial | ❌     | ✅            |
| "Which node delayed consensus?"         | ❌         | ❌     | ✅            |
| "What happened on node X at time T?"    | ✅         | ❌     | ✅            |
| "Show me the TX journey across 5 nodes" | ❌         | ❌     | ✅            |

### 2.6.4 Coexistence Strategy

```mermaid
flowchart TB
    subgraph rippled["rippled Process"]
        perflog["PerfLog<br/>(JSON to file)"]
        insight["Beast Insight<br/>(StatsD)"]
        otel["OpenTelemetry<br/>(Tracing)"]
    end

    perflog --> perffile["perf.log"]
    insight --> statsd["StatsD Server"]
    otel --> collector["OTLP Collector"]

    perffile --> grafana["Grafana<br/>(Unified UI)"]
    statsd --> grafana
    collector --> grafana

    style rippled fill:#212121,stroke:#0a0a0a,color:#ffffff
    style grafana fill:#bf360c,stroke:#8c2809,color:#ffffff
```

### 2.6.5 Correlation with PerfLog

Trace IDs can be correlated with existing PerfLog entries for comprehensive debugging:

```cpp
// In RPCHandler.cpp - correlate trace with PerfLog
Status doCommand(RPC::JsonContext& context, Json::Value& result)
{
    // Start OpenTelemetry span
    auto span = context.app.getTelemetry().startSpan(
        "rpc.command." + context.method);

    // Get trace ID for correlation
    auto traceId = span->GetContext().trace_id().IsValid()
        ? toHex(span->GetContext().trace_id())
        : "";

    // Use existing PerfLog with trace correlation
    auto const curId = context.app.getPerfLog().currentId();
    context.app.getPerfLog().rpcStart(context.method, curId);

    // Future: Add trace ID to PerfLog entry
    // context.app.getPerfLog().setTraceId(curId, traceId);

    try {
        auto ret = handler(context, result);
        context.app.getPerfLog().rpcFinish(context.method, curId);
        span->SetStatus(opentelemetry::trace::StatusCode::kOk);
        return ret;
    } catch (std::exception const& e) {
        context.app.getPerfLog().rpcError(context.method, curId);
        span->RecordException(e);
        span->SetStatus(opentelemetry::trace::StatusCode::kError, e.what());
        throw;
    }
}
```

---

_Previous: [Architecture Analysis](./01-architecture-analysis.md)_ | _Next: [Implementation Strategy](./03-implementation-strategy.md)_ | _Back to: [Overview](./OpenTelemetryPlan.md)_
