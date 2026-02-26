# Implementation Strategy

> **Parent Document**: [OpenTelemetryPlan.md](./OpenTelemetryPlan.md)
> **Related**: [Code Samples](./04-code-samples.md) | [Configuration Reference](./05-configuration-reference.md)

---

## 3.1 Directory Structure

The telemetry implementation follows rippled's existing code organization pattern:

```
include/xrpl/
├── telemetry/
│   ├── Telemetry.h              # Main telemetry interface
│   ├── TelemetryConfig.h        # Configuration structures
│   ├── TraceContext.h           # Context propagation utilities
│   ├── SpanGuard.h              # RAII span management
│   └── SpanAttributes.h         # Attribute helper functions

src/libxrpl/
├── telemetry/
│   ├── Telemetry.cpp            # Implementation
│   ├── TelemetryConfig.cpp      # Config parsing
│   ├── TraceContext.cpp         # Context serialization
│   └── NullTelemetry.cpp        # No-op implementation

src/xrpld/
├── telemetry/
│   ├── TracingInstrumentation.h # Instrumentation macros
│   └── TracingInstrumentation.cpp
```

---

## 3.2 Implementation Approach

<div align="center">

```mermaid
%%{init: {'flowchart': {'nodeSpacing': 20, 'rankSpacing': 30}}}%%
flowchart TB
    subgraph phase1["Phase 1: Core"]
        direction LR
        sdk["SDK Integration"] ~~~ interface["Telemetry Interface"] ~~~ config["Configuration"]
    end

    subgraph phase2["Phase 2: RPC"]
        direction LR
        http["HTTP Context"] ~~~ rpc["RPC Handlers"]
    end

    subgraph phase3["Phase 3: P2P"]
        direction LR
        proto["Protobuf Context"] ~~~ tx["Transaction Relay"]
    end

    subgraph phase4["Phase 4: Consensus"]
        direction LR
        consensus["Consensus Rounds"] ~~~ proposals["Proposals"]
    end

    phase1 --> phase2 --> phase3 --> phase4

    style phase1 fill:#1565c0,stroke:#0d47a1,color:#ffffff
    style phase2 fill:#2e7d32,stroke:#1b5e20,color:#ffffff
    style phase3 fill:#e65100,stroke:#bf360c,color:#ffffff
    style phase4 fill:#c2185b,stroke:#880e4f,color:#ffffff
```

</div>

### Key Principles

1. **Minimal Intrusion**: Instrumentation should not alter existing control flow
2. **Zero-Cost When Disabled**: Use compile-time flags and no-op implementations
3. **Backward Compatibility**: Protocol Buffer extensions use high field numbers
4. **Graceful Degradation**: Tracing failures must not affect node operation

---

## 3.3 Performance Overhead Summary

| Metric        | Overhead   | Notes                               |
| ------------- | ---------- | ----------------------------------- |
| CPU           | 1-3%       | Span creation and attribute setting |
| Memory        | 2-5 MB     | Batch buffer for pending spans      |
| Network       | 10-50 KB/s | Compressed OTLP export to collector |
| Latency (p99) | <2%        | With proper sampling configuration  |

---

## 3.4 Detailed CPU Overhead Analysis

### 3.4.1 Per-Operation Costs

| Operation             | Time (ns) | Frequency              | Impact     |
| --------------------- | --------- | ---------------------- | ---------- |
| Span creation         | 200-500   | Every traced operation | Low        |
| Span end              | 100-200   | Every traced operation | Low        |
| SetAttribute (string) | 80-120    | 3-5 per span           | Low        |
| SetAttribute (int)    | 40-60     | 2-3 per span           | Negligible |
| AddEvent              | 50-80     | 0-2 per span           | Negligible |
| Context injection     | 150-250   | Per outgoing message   | Low        |
| Context extraction    | 100-180   | Per incoming message   | Low        |
| GetCurrent context    | 10-20     | Thread-local access    | Negligible |

### 3.4.2 Transaction Processing Overhead

<div align="center">

```mermaid
%%{init: {'pie': {'textPosition': 0.75}}}%%
pie showData
    "tx.receive (800ns)" : 800
    "tx.validate (500ns)" : 500
    "tx.relay (500ns)" : 500
    "Context inject (600ns)" : 600
```

**Transaction Tracing Overhead (~2.4μs total)**

</div>

**Overhead percentage**: 2.4 μs / 200 μs (avg tx processing) = **~1.2%**

### 3.4.3 Consensus Round Overhead

| Operation              | Count | Cost (ns) | Total      |
| ---------------------- | ----- | --------- | ---------- |
| consensus.round span   | 1     | ~1000     | ~1 μs      |
| consensus.phase spans  | 3     | ~700      | ~2.1 μs    |
| proposal.receive spans | ~20   | ~600      | ~12 μs     |
| proposal.send spans    | ~3    | ~600      | ~1.8 μs    |
| Context operations     | ~30   | ~200      | ~6 μs      |
| **TOTAL**              |       |           | **~23 μs** |

**Overhead percentage**: 23 μs / 3s (typical round) = **~0.0008%** (negligible)

### 3.4.4 RPC Request Overhead

| Operation        | Cost (ns)    |
| ---------------- | ------------ |
| rpc.request span | ~700         |
| rpc.command span | ~600         |
| Context extract  | ~250         |
| Context inject   | ~200         |
| **TOTAL**        | **~1.75 μs** |

- Fast RPC (1ms): 1.75 μs / 1ms = **~0.175%**
- Slow RPC (100ms): 1.75 μs / 100ms = **~0.002%**

---

## 3.5 Memory Overhead Analysis

### 3.5.1 Static Memory

| Component                | Size        | Allocated  |
| ------------------------ | ----------- | ---------- |
| TracerProvider singleton | ~64 KB      | At startup |
| BatchSpanProcessor       | ~128 KB     | At startup |
| OTLP exporter            | ~256 KB     | At startup |
| Propagator registry      | ~8 KB       | At startup |
| **Total static**         | **~456 KB** |            |

### 3.5.2 Dynamic Memory

| Component            | Size per unit | Max units  | Peak        |
| -------------------- | ------------- | ---------- | ----------- |
| Active span          | ~200 bytes    | 1000       | ~200 KB     |
| Queued span (export) | ~500 bytes    | 2048       | ~1 MB       |
| Attribute storage    | ~50 bytes     | 5 per span | Included    |
| Context storage      | ~64 bytes     | Per thread | ~6.4 KB     |
| **Total dynamic**    |               |            | **~1.2 MB** |

### 3.5.3 Memory Growth Characteristics

```mermaid
---
config:
    xyChart:
        width: 700
        height: 400
---
xychart-beta
    title "Memory Usage vs Span Rate"
    x-axis "Spans/second" [0, 200, 400, 600, 800, 1000]
    y-axis "Memory (MB)" 0 --> 6
    line [1, 1.8, 2.6, 3.4, 4.2, 5]
```

**Notes**:

- Memory increases linearly with span rate
- Batch export prevents unbounded growth
- Queue size is configurable (default 2048 spans)
- At queue limit, oldest spans are dropped (not blocked)

---

## 3.6 Network Overhead Analysis

### 3.6.1 Export Bandwidth

| Sampling Rate | Spans/sec | Bandwidth | Notes            |
| ------------- | --------- | --------- | ---------------- |
| 100%          | ~500      | ~250 KB/s | Development only |
| 10%           | ~50       | ~25 KB/s  | Staging          |
| 1%            | ~5        | ~2.5 KB/s | Production       |
| Error-only    | ~1        | ~0.5 KB/s | Minimal overhead |

### 3.6.2 Trace Context Propagation

| Message Type           | Context Size | Messages/sec | Overhead    |
| ---------------------- | ------------ | ------------ | ----------- |
| TMTransaction          | 32 bytes     | ~100         | ~3.2 KB/s   |
| TMProposeSet           | 32 bytes     | ~10          | ~320 B/s    |
| TMValidation           | 32 bytes     | ~50          | ~1.6 KB/s   |
| **Total P2P overhead** |              |              | **~5 KB/s** |

---

## 3.7 Optimization Strategies

### 3.7.1 Sampling Strategies

```mermaid
flowchart TD
    trace["New Trace"]

    trace --> errors{"Is Error?"}
    errors -->|Yes| sample["SAMPLE"]
    errors -->|No| consensus{"Is Consensus?"}

    consensus -->|Yes| sample
    consensus -->|No| slow{"Is Slow?"}

    slow -->|Yes| sample
    slow -->|No| prob{"Random < 10%?"}

    prob -->|Yes| sample
    prob -->|No| drop["DROP"]

    style sample fill:#4caf50,stroke:#388e3c,color:#fff
    style drop fill:#f44336,stroke:#c62828,color:#fff
```

### 3.7.2 Batch Tuning Recommendations

| Environment        | Batch Size | Batch Delay | Max Queue |
| ------------------ | ---------- | ----------- | --------- |
| Low-latency        | 128        | 1000ms      | 512       |
| High-throughput    | 1024       | 10000ms     | 8192      |
| Memory-constrained | 256        | 2000ms      | 512       |

### 3.7.3 Conditional Instrumentation

```cpp
// Compile-time feature flag
#ifndef XRPL_ENABLE_TELEMETRY
// Zero-cost when disabled
#define XRPL_TRACE_SPAN(t, n) ((void)0)
#endif

// Runtime component filtering
if (telemetry.shouldTracePeer())
{
    XRPL_TRACE_SPAN(telemetry, "peer.message.receive");
    // ... instrumentation
}
// No overhead when component tracing disabled
```

---

## 3.8 Links to Detailed Documentation

- **[Code Samples](./04-code-samples.md)**: Complete implementation code for all components
- **[Configuration Reference](./05-configuration-reference.md)**: Configuration options and collector setup
- **[Implementation Phases](./06-implementation-phases.md)**: Detailed timeline and milestones

---

## 3.9 Code Intrusiveness Assessment

This section provides a detailed assessment of how intrusive the OpenTelemetry integration is to the existing rippled codebase.

### 3.9.1 Files Modified Summary

| Component             | Files Modified | Lines Added | Lines Changed | Architectural Impact |
| --------------------- | -------------- | ----------- | ------------- | -------------------- |
| **Core Telemetry**    | 5 new files    | ~800        | 0             | None (new module)    |
| **Application Init**  | 2 files        | ~30         | ~5            | Minimal              |
| **RPC Layer**         | 3 files        | ~80         | ~20           | Minimal              |
| **Transaction Relay** | 4 files        | ~120        | ~40           | Low                  |
| **Consensus**         | 3 files        | ~100        | ~30           | Low-Medium           |
| **Protocol Buffers**  | 1 file         | ~25         | 0             | Low                  |
| **CMake/Build**       | 3 files        | ~50         | ~10           | Minimal              |
| **Total**             | **~21 files**  | **~1,205**  | **~105**      | **Low**              |

### 3.9.2 Detailed File Impact

```mermaid
pie title Code Changes by Component
    "New Telemetry Module" : 800
    "Transaction Relay" : 160
    "Consensus" : 130
    "RPC Layer" : 100
    "Application Init" : 35
    "Protocol Buffers" : 25
    "Build System" : 60
```

#### New Files (No Impact on Existing Code)

| File                                           | Lines | Purpose              |
| ---------------------------------------------- | ----- | -------------------- |
| `include/xrpl/telemetry/Telemetry.h`           | ~160  | Main interface       |
| `include/xrpl/telemetry/SpanGuard.h`           | ~120  | RAII wrapper         |
| `include/xrpl/telemetry/TraceContext.h`        | ~80   | Context propagation  |
| `src/xrpld/telemetry/TracingInstrumentation.h` | ~60   | Macros               |
| `src/libxrpl/telemetry/Telemetry.cpp`          | ~200  | Implementation       |
| `src/libxrpl/telemetry/TelemetryConfig.cpp`    | ~60   | Config parsing       |
| `src/libxrpl/telemetry/NullTelemetry.cpp`      | ~40   | No-op implementation |

#### Modified Files (Existing Rippled Code)

| File                                              | Lines Added | Lines Changed | Risk Level |
| ------------------------------------------------- | ----------- | ------------- | ---------- |
| `src/xrpld/app/main/Application.cpp`              | ~15         | ~3            | Low        |
| `include/xrpl/app/main/Application.h`             | ~5          | ~2            | Low        |
| `src/xrpld/rpc/detail/ServerHandler.cpp`          | ~40         | ~10           | Low        |
| `src/xrpld/rpc/handlers/*.cpp`                    | ~30         | ~8            | Low        |
| `src/xrpld/overlay/detail/PeerImp.cpp`            | ~60         | ~15           | Medium     |
| `src/xrpld/overlay/detail/OverlayImpl.cpp`        | ~30         | ~10           | Medium     |
| `src/xrpld/app/consensus/RCLConsensus.cpp`        | ~50         | ~15           | Medium     |
| `src/xrpld/app/consensus/RCLConsensusAdaptor.cpp` | ~40         | ~12           | Medium     |
| `src/xrpld/core/JobQueue.cpp`                     | ~20         | ~5            | Low        |
| `src/xrpld/overlay/detail/ripple.proto`           | ~25         | 0             | Low        |
| `CMakeLists.txt`                                  | ~40         | ~8            | Low        |
| `cmake/FindOpenTelemetry.cmake`                   | ~50         | 0             | None (new) |

### 3.9.3 Risk Assessment by Component

<div align="center">

**Do First** ↖ ↗ **Plan Carefully**

```mermaid
quadrantChart
    title Code Intrusiveness Risk Matrix
    x-axis Low Risk --> High Risk
    y-axis Low Value --> High Value

    RPC Tracing: [0.2, 0.8]
    Transaction Relay: [0.5, 0.9]
    Consensus Tracing: [0.7, 0.95]
    Peer Message Tracing: [0.8, 0.4]
    JobQueue Context: [0.4, 0.5]
    Ledger Acquisition: [0.5, 0.6]
```

**Optional** ↙ ↘ **Avoid**

</div>

#### Risk Level Definitions

| Risk Level | Definition                                                       | Mitigation                         |
| ---------- | ---------------------------------------------------------------- | ---------------------------------- |
| **Low**    | Additive changes only; no modification to existing logic         | Standard code review               |
| **Medium** | Minor modifications to existing functions; clear boundaries      | Comprehensive unit tests           |
| **High**   | Changes to core logic or data structures; potential side effects | Integration tests + staged rollout |

### 3.9.4 Architectural Impact Assessment

| Aspect               | Impact  | Justification                                                         |
| -------------------- | ------- | --------------------------------------------------------------------- |
| **Data Flow**        | None    | Tracing is purely observational; no business logic changes            |
| **Threading Model**  | Minimal | Context propagation uses thread-local storage (standard OTel pattern) |
| **Memory Model**     | Low     | Bounded queues prevent unbounded growth; RAII ensures cleanup         |
| **Network Protocol** | Low     | Optional fields in protobuf (high field numbers); backward compatible |
| **Configuration**    | None    | New config section; existing configs unaffected                       |
| **Build System**     | Low     | Optional CMake flag; builds work without OpenTelemetry                |
| **Dependencies**     | Low     | OpenTelemetry SDK is optional; null implementation when disabled      |

### 3.9.5 Backward Compatibility

| Compatibility   | Status  | Notes                                                 |
| --------------- | ------- | ----------------------------------------------------- |
| **Config File** | ✅ Full | New `[telemetry]` section is optional                 |
| **Protocol**    | ✅ Full | Optional protobuf fields with high field numbers      |
| **Build**       | ✅ Full | `XRPL_ENABLE_TELEMETRY=OFF` produces identical binary |
| **Runtime**     | ✅ Full | `enabled=0` produces zero overhead                    |
| **API**         | ✅ Full | No changes to public RPC or P2P APIs                  |

### 3.9.6 Rollback Strategy

If issues are discovered after deployment:

1. **Immediate**: Set `enabled=0` in config and restart (zero code change)
2. **Quick**: Rebuild with `XRPL_ENABLE_TELEMETRY=OFF`
3. **Complete**: Revert telemetry commits (clean separation makes this easy)

### 3.9.7 Code Change Examples

**Minimal RPC Instrumentation (Low Intrusiveness):**

```cpp
// Before
void ServerHandler::onRequest(...) {
    auto result = processRequest(req);
    send(result);
}

// After (only ~10 lines added)
void ServerHandler::onRequest(...) {
    XRPL_TRACE_RPC(app_.getTelemetry(), "rpc.request");  // +1 line
    XRPL_TRACE_SET_ATTR("xrpl.rpc.command", command);     // +1 line

    auto result = processRequest(req);

    XRPL_TRACE_SET_ATTR("xrpl.rpc.status", status);       // +1 line
    send(result);
}
```

**Consensus Instrumentation (Medium Intrusiveness):**

```cpp
// Before
void RCLConsensusAdaptor::startRound(...) {
    // ... existing logic
}

// After (context storage required)
void RCLConsensusAdaptor::startRound(...) {
    XRPL_TRACE_CONSENSUS(app_.getTelemetry(), "consensus.round");
    XRPL_TRACE_SET_ATTR("xrpl.consensus.ledger.seq", seq);

    // Store context for child spans in phase transitions
    currentRoundContext_ = _xrpl_guard_->context();  // New member variable

    // ... existing logic unchanged
}
```

---

_Previous: [Design Decisions](./02-design-decisions.md)_ | _Next: [Code Samples](./04-code-samples.md)_ | _Back to: [Overview](./OpenTelemetryPlan.md)_
