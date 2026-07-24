# Appendix

> **Parent Document**: [OpenTelemetryPlan.md](./OpenTelemetryPlan.md)
> **Related**: [Observability Backends](./07-observability-backends.md)

---

## 8.1 Glossary

> **OTLP** = OpenTelemetry Protocol | **TxQ** = Transaction Queue

| Term                  | Definition                                                 |
| --------------------- | ---------------------------------------------------------- |
| **Span**              | A unit of work with start/end time, name, and attributes   |
| **Trace**             | A collection of spans representing a complete request flow |
| **Trace ID**          | 128-bit unique identifier for a trace                      |
| **Span ID**           | 64-bit unique identifier for a span within a trace         |
| **Context**           | Carrier for trace/span IDs across boundaries               |
| **Propagator**        | Component that injects/extracts context                    |
| **Sampler**           | Decides which traces to record                             |
| **Exporter**          | Sends spans to backend                                     |
| **Collector**         | Receives, processes, and forwards telemetry                |
| **OTLP**              | OpenTelemetry Protocol (wire format)                       |
| **W3C Trace Context** | Standard HTTP headers for trace propagation                |
| **Baggage**           | Key-value pairs propagated across service boundaries       |
| **Resource**          | Entity producing telemetry (service, host, etc.)           |
| **Instrumentation**   | Code that creates telemetry data                           |

### xrpld-Specific Terms

| Term              | Definition                                                    |
| ----------------- | ------------------------------------------------------------- |
| **Overlay**       | P2P network layer managing peer connections                   |
| **Consensus**     | XRP Ledger consensus algorithm (RCL)                          |
| **Proposal**      | Validator's suggested transaction set for a ledger            |
| **Validation**    | Validator's signature on a closed ledger                      |
| **HashRouter**    | Component for transaction deduplication                       |
| **JobQueue**      | Thread pool for asynchronous task execution                   |
| **PerfLog**       | Existing performance logging system in xrpld                  |
| **Beast Insight** | Existing metrics framework in xrpld                           |
| **PathFinding**   | Payment path computation engine for cross-currency payments   |
| **TxQ**           | Transaction queue managing fee-based prioritization           |
| **LoadManager**   | Dynamic fee escalation based on network load                  |
| **SHAMap**        | SHA-256 hash-based map (Merkle trie variant) for ledger state |

### Phase 9–11 Terms

| Term                        | Definition                                                                |
| --------------------------- | ------------------------------------------------------------------------- |
| **MetricsRegistry**         | Centralized class for OTel async gauge registrations (Phase 9)            |
| **ObservableGauge**         | OTel Metrics SDK async instrument polled via callback at fixed intervals  |
| **PeriodicMetricReader**    | OTel SDK component that invokes gauge callbacks at configurable intervals |
| **CountedObject**           | xrpld template that tracks live instance counts via atomic counters       |
| **TxQ**                     | Transaction queue managing fee escalation and ordering                    |
| **Load Factor**             | Combined multiplier affecting transaction cost (local, cluster, network)  |
| **OTel Collector Receiver** | Custom Go plugin that polls xrpld RPC and emits OTel metrics (Phase 11)   |

---

## 8.2 Span Hierarchy Visualization

The authoritative span-flow diagrams — a master overview plus per-stage
flowcharts (ingress, the shared apply pipeline, the consensus round, ledger
finalize, and the pathfinding / ledger-acquire side flows) — live in the operator
runbook. They map every span onto the **real xrpld control flow and XRPL protocol
order** (verified against code and `docs/consensus.md`, with file:line evidence),
label every node and branch with the span that represents that state or
transition, and call out where the OpenTelemetry span parent links diverge from
that flow.

> **See**: [docs/telemetry-runbook.md § Protocol Span Flow](../docs/telemetry-runbook.md#protocol-span-flow).

The full span inventory (names, attributes, parents as instrumented) is in
[09-data-collection-reference.md §1](./09-data-collection-reference.md#1-opentelemetry-spans).

---

## 8.3 References

> **OTLP** = OpenTelemetry Protocol

### OpenTelemetry Resources

1. [OpenTelemetry C++ SDK](https://github.com/open-telemetry/opentelemetry-cpp)
2. [OpenTelemetry Specification](https://opentelemetry.io/docs/specs/otel/)
3. [OpenTelemetry Collector](https://opentelemetry.io/docs/collector/)
4. [OTLP Protocol Specification](https://opentelemetry.io/docs/specs/otlp/)

### Standards

5. [W3C Trace Context](https://www.w3.org/TR/trace-context/)
6. [W3C Baggage](https://www.w3.org/TR/baggage/)
7. [Protocol Buffers](https://protobuf.dev/)

### xrpld Resources

8. [xrpld Source Code](https://github.com/XRPLF/rippled)
9. [XRP Ledger Documentation](https://xrpl.org/docs/)
10. [xrpld Overlay README](https://github.com/XRPLF/rippled/blob/develop/src/xrpld/overlay/README.md)
11. [xrpld RPC README](https://github.com/XRPLF/rippled/blob/develop/src/xrpld/rpc/README.md)
12. [xrpld Consensus README](https://github.com/XRPLF/rippled/blob/develop/src/xrpld/app/consensus/README.md)

---

## 8.4 Version History

| Version | Date       | Author | Changes                                                        |
| ------- | ---------- | ------ | -------------------------------------------------------------- |
| 1.0     | 2026-02-12 | -      | Initial implementation plan                                    |
| 1.1     | 2026-02-13 | -      | Refactored into modular documents                              |
| 1.2     | 2026-03-09 | -      | Added Phases 9–11 (future enhancement plans)                   |
| 1.3     | 2026-03-24 | -      | Review fixes: accuracy corrections, cross-document consistency |

---

## 8.5 Document Index

### Plan Documents

| Document                                                             | Description                                        |
| -------------------------------------------------------------------- | -------------------------------------------------- |
| [OpenTelemetryPlan.md](./OpenTelemetryPlan.md)                       | Master overview and executive summary              |
| [00-tracing-fundamentals.md](./00-tracing-fundamentals.md)           | Distributed tracing concepts and OTel primer       |
| [01-architecture-analysis.md](./01-architecture-analysis.md)         | xrpld architecture and trace points                |
| [02-design-decisions.md](./02-design-decisions.md)                   | SDK selection, exporters, span conventions         |
| [03-implementation-strategy.md](./03-implementation-strategy.md)     | Directory structure, performance analysis          |
| [05-configuration-reference.md](./05-configuration-reference.md)     | xrpld config, CMake, Collector configs             |
| [06-implementation-phases.md](./06-implementation-phases.md)         | Timeline, tasks, risks, success metrics            |
| [07-observability-backends.md](./07-observability-backends.md)       | Backend selection and architecture                 |
| [08-appendix.md](./08-appendix.md)                                   | Glossary, references, version history              |
| [secure-OTel.md](./secure-OTel.md)                                   | Threat model and hardening (mTLS, peer validation) |
| [09-data-collection-reference.md](./09-data-collection-reference.md) | Span/metric/dashboard inventory                    |

### Task Lists

| Document                                                                   | Description                                         |
| -------------------------------------------------------------------------- | --------------------------------------------------- |
| [Phase2_taskList.md](./Phase2_taskList.md)                                 | RPC layer trace instrumentation                     |
| [Phase3_taskList.md](./Phase3_taskList.md)                                 | Peer overlay & consensus tracing                    |
| [Phase4_taskList.md](./Phase4_taskList.md)                                 | Transaction lifecycle tracing                       |
| [Phase5_taskList.md](./Phase5_taskList.md)                                 | Ledger processing & advanced tracing                |
| [Phase5_IntegrationTest_taskList.md](./Phase5_IntegrationTest_taskList.md) | Observability stack integration tests               |
| [Phase7_taskList.md](./Phase7_taskList.md)                                 | Native OTel metrics migration                       |
| [Phase8_taskList.md](./Phase8_taskList.md)                                 | Log-trace correlation                               |
| [Phase9_taskList.md](./Phase9_taskList.md)                                 | Internal metric instrumentation gap fill (future)   |
| [Phase10_taskList.md](./Phase10_taskList.md)                               | Synthetic workload generation & validation (future) |
| [Phase11_taskList.md](./Phase11_taskList.md)                               | Third-party data collection pipelines (future)      |

> **Note**: Phases 1 and 6 do not have separate task list files. Phase 1 tasks are documented in [06-implementation-phases.md §6.2](./06-implementation-phases.md). Phase 6 tasks are documented in [06-implementation-phases.md §6.7](./06-implementation-phases.md).

---

## 8.6 Phase 9–11 Cross-Reference Guide

This guide maps Phase 9–11 content to its location across the documentation.

### Phase 9: Internal Metric Instrumentation Gap Fill

| Content                         | Location                                                                 |
| ------------------------------- | ------------------------------------------------------------------------ |
| Plan & architecture             | [06-implementation-phases.md §6.8.2](./06-implementation-phases.md)      |
| Task list (10 tasks)            | [Phase9_taskList.md](./Phase9_taskList.md)                               |
| Future metric definitions (~50) | [09-data-collection-reference.md §5b](./09-data-collection-reference.md) |
| New class: `MetricsRegistry`    | `src/xrpld/telemetry/MetricsRegistry.h/.cpp` (planned)                   |
| New dashboards                  | `fee-market`, `job-queue` (planned)                                      |

**Metric categories**: NodeStore I/O, Cache Hit Rates, TxQ, PerfLog Per-RPC, PerfLog Per-Job, Counted Objects, Fee Escalation & Load Factors.

### Phase 10: Synthetic Workload Generation & Telemetry Validation

| Content              | Location                                                                 |
| -------------------- | ------------------------------------------------------------------------ |
| Plan & architecture  | [06-implementation-phases.md §6.8.3](./06-implementation-phases.md)      |
| Task list (7 tasks)  | [Phase10_taskList.md](./Phase10_taskList.md)                             |
| Validation inventory | [09-data-collection-reference.md §5c](./09-data-collection-reference.md) |
| Test harness         | `docker/telemetry/docker-compose.workload.yaml` (planned)                |
| CI workflow          | `.github/workflows/telemetry-validation.yml` (planned)                   |

**Validates**: 16 spans, 22 attributes, 300+ metrics, 10 dashboards, log-trace correlation.

### Phase 11: Third-Party Data Collection Pipelines

| Content                           | Location                                                                 |
| --------------------------------- | ------------------------------------------------------------------------ |
| Plan & architecture               | [06-implementation-phases.md §6.8.4](./06-implementation-phases.md)      |
| Task list (11 tasks)              | [Phase11_taskList.md](./Phase11_taskList.md)                             |
| External metric definitions (~30) | [09-data-collection-reference.md §5d](./09-data-collection-reference.md) |
| Custom OTel Collector receiver    | `docker/telemetry/otel-rippled-receiver/` (planned)                      |
| Prometheus alerting rules (11)    | [09-data-collection-reference.md §5d](./09-data-collection-reference.md) |
| New dashboards (4)                | Validator Health, Network Topology, Fee Market (External), DEX & AMM     |

**Consumer categories**: Exchanges, Payment Processors, DeFi/AMM, NFT Marketplaces, Analytics Providers, Wallets, Compliance, Academic Researchers, Institutional Custody, CBDC Bridge Operators.

---

_Previous: [Observability Backends](./07-observability-backends.md)_ | _Back to: [Overview](./OpenTelemetryPlan.md)_
