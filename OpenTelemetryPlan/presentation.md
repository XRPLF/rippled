# OpenTelemetry Distributed Tracing for rippled

---

## Slide 1: Introduction

### What is OpenTelemetry?

OpenTelemetry is an open-source, CNCF-backed observability framework for distributed tracing, metrics, and logs.

### Why OpenTelemetry for rippled?

- **End-to-End Transaction Visibility**: Track transactions from submission → consensus → ledger inclusion
- **Cross-Node Correlation**: Follow requests across multiple independent nodes using a unique `trace_id`
- **Consensus Round Analysis**: Understand timing and behavior across validators
- **Incident Debugging**: Correlate events across distributed nodes during issues

```mermaid
flowchart LR
    A["Node A<br/>tx.receive<br/>trace_id: abc123"] --> B["Node B<br/>tx.relay<br/>trace_id: abc123"] --> C["Node C<br/>tx.validate<br/>trace_id: abc123"] --> D["Node D<br/>ledger.apply<br/>trace_id: abc123"]

    style A fill:#1565c0,stroke:#0d47a1,color:#fff
    style B fill:#2e7d32,stroke:#1b5e20,color:#fff
    style C fill:#2e7d32,stroke:#1b5e20,color:#fff
    style D fill:#e65100,stroke:#bf360c,color:#fff
```

> **Trace ID: abc123** — All nodes share the same trace, enabling cross-node correlation.

---

## Slide 2: OpenTelemetry vs Open Source Alternatives

| Feature             | OpenTelemetry    | Jaeger           | Zipkin             | SkyWalking | Pinpoint   | Prometheus |
| ------------------- | ---------------- | ---------------- | ------------------ | ---------- | ---------- | ---------- |
| **Tracing**         | YES              | YES              | YES                | YES        | YES        | NO         |
| **Metrics**         | YES              | NO               | NO                 | YES        | YES        | YES        |
| **Logs**            | YES              | NO               | NO                 | YES        | NO         | NO         |
| **C++ SDK**         | YES Official     | YES (Deprecated) | YES (Unmaintained) | NO         | NO         | YES        |
| **Vendor Neutral**  | YES Primary goal | NO               | NO                 | NO         | NO         | NO         |
| **Instrumentation** | Manual + Auto    | Manual           | Manual             | Auto-first | Auto-first | Manual     |
| **Backend**         | Any (exporters)  | Self             | Self               | Self       | Self       | Self       |
| **CNCF Status**     | Incubating       | Graduated        | NO                 | Incubating | NO         | Graduated  |

> **Why OpenTelemetry?** It's the only actively maintained, full-featured C++ option with vendor neutrality — allowing export to Jaeger, Prometheus, Grafana, or any commercial backend without changing instrumentation.

---

## Slide 3: Comparison with rippled's Existing Solutions

### Current Observability Stack

| Aspect                | PerfLog (JSON)        | StatsD (Metrics)      | OpenTelemetry (NEW)         |
| --------------------- | --------------------- | --------------------- | --------------------------- |
| **Type**              | Logging               | Metrics               | Distributed Tracing         |
| **Scope**             | Single node           | Single node           | **Cross-node**              |
| **Data**              | JSON log entries      | Counters, gauges      | Spans with context          |
| **Correlation**       | By timestamp          | By metric name        | By `trace_id`               |
| **Overhead**          | Low (file I/O)        | Low (UDP)             | Low-Medium (configurable)   |
| **Question Answered** | "What happened here?" | "How many? How fast?" | **"What was the journey?"** |

### Use Case Matrix

| Scenario                         | PerfLog | StatsD | OpenTelemetry |
| -------------------------------- | ------- | ------ | ------------- |
| "How many TXs per second?"       | ❌      | ✅     | ❌            |
| "Why was this specific TX slow?" | ⚠️      | ❌     | ✅            |
| "Which node delayed consensus?"  | ❌      | ❌     | ✅            |
| "Show TX journey across 5 nodes" | ❌      | ❌     | ✅            |

> **Key Insight**: OpenTelemetry **complements** (not replaces) existing systems.

---

## Slide 4: Architecture

### High-Level Integration Architecture

```mermaid
flowchart TB
    subgraph rippled["rippled Node"]
        subgraph services["Core Services"]
            direction LR
            RPC["RPC Server<br/>(HTTP/WS)"] ~~~ Overlay["Overlay<br/>(P2P Network)"] ~~~ Consensus["Consensus<br/>(RCLConsensus)"]
        end

        Telemetry["Telemetry Module<br/>(OpenTelemetry SDK)"]

        services --> Telemetry
    end

    Telemetry -->|OTLP/gRPC| Collector["OTel Collector"]

    Collector --> Tempo["Grafana Tempo"]
    Collector --> Jaeger["Jaeger"]
    Collector --> Elastic["Elastic APM"]

    style rippled fill:#424242,stroke:#212121,color:#fff
    style services fill:#1565c0,stroke:#0d47a1,color:#fff
    style Telemetry fill:#2e7d32,stroke:#1b5e20,color:#fff
    style Collector fill:#e65100,stroke:#bf360c,color:#fff
```

### Context Propagation

```mermaid
sequenceDiagram
    participant Client
    participant NodeA as Node A
    participant NodeB as Node B

    Client->>NodeA: Submit TX (no context)
    Note over NodeA: Creates trace_id: abc123<br/>span: tx.receive
    NodeA->>NodeB: Relay TX<br/>(traceparent: abc123)
    Note over NodeB: Links to trace_id: abc123<br/>span: tx.relay
```

- **HTTP/RPC**: W3C Trace Context headers (`traceparent`)
- **P2P Messages**: Protocol Buffer extension fields

---

## Slide 5: Implementation Plan

### 5-Phase Rollout (9 Weeks)

```mermaid
gantt
    title Implementation Timeline
    dateFormat  YYYY-MM-DD
    axisFormat  Week %W

    section Phase 1
    Core Infrastructure    :p1, 2024-01-01, 2w

    section Phase 2
    RPC Tracing           :p2, after p1, 2w

    section Phase 3
    Transaction Tracing   :p3, after p2, 2w

    section Phase 4
    Consensus Tracing     :p4, after p3, 2w

    section Phase 5
    Documentation         :p5, after p4, 1w
```

### Phase Details

| Phase | Focus               | Key Deliverables                             | Effort  |
| ----- | ------------------- | -------------------------------------------- | ------- |
| 1     | Core Infrastructure | SDK integration, Telemetry interface, Config | 10 days |
| 2     | RPC Tracing         | HTTP context extraction, Handler spans       | 10 days |
| 3     | Transaction Tracing | Protobuf context, P2P relay propagation      | 10 days |
| 4     | Consensus Tracing   | Round spans, Proposal/validation tracing     | 10 days |
| 5     | Documentation       | Runbook, Dashboards, Training                | 7 days  |

**Total Effort**: ~47 developer-days (2 developers)

---

## Slide 6: Performance Overhead

### Estimated System Impact

| Metric            | Overhead   | Notes                               |
| ----------------- | ---------- | ----------------------------------- |
| **CPU**           | 1-3%       | Span creation and attribute setting |
| **Memory**        | 2-5 MB     | Batch buffer for pending spans      |
| **Network**       | 10-50 KB/s | Compressed OTLP export to collector |
| **Latency (p99)** | <2%        | With proper sampling configuration  |

### Per-Message Overhead (Context Propagation)

Each P2P message carries trace context with the following overhead:

| Field         | Size          | Description                               |
| ------------- | ------------- | ----------------------------------------- |
| `trace_id`    | 16 bytes      | Unique identifier for the entire trace    |
| `span_id`     | 8 bytes       | Current span (becomes parent on receiver) |
| `trace_flags` | 4 bytes       | Sampling decision flags                   |
| `trace_state` | 0-4 bytes     | Optional vendor-specific data             |
| **Total**     | **~32 bytes** | **Added per traced P2P message**          |

```mermaid
flowchart LR
    subgraph msg["P2P Message with Trace Context"]
        A["Original Message<br/>(variable size)"] --> B["+ TraceContext<br/>(~32 bytes)"]
    end

    subgraph breakdown["Context Breakdown"]
        C["trace_id<br/>16 bytes"]
        D["span_id<br/>8 bytes"]
        E["flags<br/>4 bytes"]
        F["state<br/>0-4 bytes"]
    end

    B --> breakdown

    style A fill:#424242,stroke:#212121,color:#fff
    style B fill:#2e7d32,stroke:#1b5e20,color:#fff
    style C fill:#1565c0,stroke:#0d47a1,color:#fff
    style D fill:#1565c0,stroke:#0d47a1,color:#fff
    style E fill:#e65100,stroke:#bf360c,color:#fff
    style F fill:#4a148c,stroke:#2e0d57,color:#fff
```

> **Note**: 32 bytes is negligible compared to typical transaction messages (hundreds to thousands of bytes)

### Mitigation Strategies

```mermaid
flowchart LR
    A["Head Sampling<br/>10% default"] --> B["Tail Sampling<br/>Keep errors/slow"] --> C["Batch Export<br/>Reduce I/O"] --> D["Conditional Compile<br/>XRPL_ENABLE_TELEMETRY"]

    style A fill:#1565c0,stroke:#0d47a1,color:#fff
    style B fill:#2e7d32,stroke:#1b5e20,color:#fff
    style C fill:#e65100,stroke:#bf360c,color:#fff
    style D fill:#4a148c,stroke:#2e0d57,color:#fff
```

### Kill Switches (Rollback Options)

1. **Config Disable**: Set `enabled=0` in config → instant disable, no restart needed for sampling
2. **Rebuild**: Compile with `XRPL_ENABLE_TELEMETRY=OFF` → zero overhead (no-op)
3. **Full Revert**: Clean separation allows easy commit reversion

---

## Slide 7: Data Collection & Privacy

### What Data is Collected

| Category        | Attributes Collected                                                               | Purpose                     |
| --------------- | ---------------------------------------------------------------------------------- | --------------------------- |
| **Transaction** | `tx.hash`, `tx.type`, `tx.result`, `tx.fee`, `ledger_index`                        | Trace transaction lifecycle |
| **Consensus**   | `round`, `phase`, `mode`, `proposers`(public key or public node id), `duration_ms` | Analyze consensus timing    |
| **RPC**         | `command`, `version`, `status`, `duration_ms`                                      | Monitor RPC performance     |
| **Peer**        | `peer.id`(public key), `latency_ms`, `message.type`, `message.size`                | Network topology analysis   |
| **Ledger**      | `ledger.hash`, `ledger.index`, `close_time`, `tx_count`                            | Ledger progression tracking |
| **Job**         | `job.type`, `queue_ms`, `worker`                                                   | JobQueue performance        |

### What is NOT Collected (Privacy Guarantees)

```mermaid
flowchart LR
    subgraph notCollected["❌ NOT Collected"]
        direction LR
        A["Private Keys"] ~~~ B["Account Balances"] ~~~ C["Transaction Amounts"]
    end

    subgraph alsoNot["❌ Also Excluded"]
        direction LR
        D["IP Addresses<br/>(configurable)"] ~~~ E["Personal Data"] ~~~ F["Raw TX Payloads"]
    end

    style A fill:#c62828,stroke:#8c2809,color:#fff
    style B fill:#c62828,stroke:#8c2809,color:#fff
    style C fill:#c62828,stroke:#8c2809,color:#fff
    style D fill:#c62828,stroke:#8c2809,color:#fff
    style E fill:#c62828,stroke:#8c2809,color:#fff
    style F fill:#c62828,stroke:#8c2809,color:#fff
```

### Privacy Protection Mechanisms

| Mechanism                  | Description                                                   |
| -------------------------- | ------------------------------------------------------------- |
| **Account Hashing**        | `xrpl.tx.account` is hashed at collector level before storage |
| **Configurable Redaction** | Sensitive fields can be excluded via config                   |
| **Sampling**               | Only 10% of traces recorded by default (reduces exposure)     |
| **Local Control**          | Node operators control what gets exported                     |
| **No Raw Payloads**        | Transaction content is never recorded, only metadata          |

> **Key Principle**: Telemetry collects **operational metadata** (timing, counts, hashes) — never **sensitive content** (keys, balances, amounts).

---

_End of Presentation_
