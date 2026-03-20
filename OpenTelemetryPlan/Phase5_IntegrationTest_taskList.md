# Phase 5: Integration Test Task List

> **Goal**: End-to-end verification of the complete telemetry pipeline using a
> 6-node consensus network. Proves that RPC, transaction, and consensus spans
> flow through the observability stack (otel-collector, Jaeger, Prometheus,
> Grafana) under realistic conditions.
>
> **Scope**: Integration test script, manual testing plan, 6-node local network
> setup, Jaeger/Prometheus/Grafana verification.
>
> **Branch**: `pratik/otel-phase5-docs-deployment`

### Related Plan Documents

| Document                                                         | Relevance                                  |
| ---------------------------------------------------------------- | ------------------------------------------ |
| [07-observability-backends.md](./07-observability-backends.md)   | Jaeger, Grafana, Prometheus setup          |
| [05-configuration-reference.md](./05-configuration-reference.md) | Collector config, Docker Compose           |
| [06-implementation-phases.md](./06-implementation-phases.md)     | Phase 5 tasks, definition of done          |
| [Phase5_taskList.md](./Phase5_taskList.md)                       | Phase 5 main task list (5.6 = integration) |

---

## Task IT.1: Create Integration Test Script

**Objective**: Automated bash script that stands up a 6-node xrpld network
with telemetry, exercises all span categories, and verifies data in
Jaeger/Prometheus.

**What to do**:

- Create `docker/telemetry/integration-test.sh`:
  - Prerequisites check (docker, xrpld binary, curl, jq)
  - Start observability stack via `docker compose`
  - Generate 6 validator key pairs via temp standalone xrpld
  - Generate 6 node configs + shared `validators.txt`
  - Start 6 xrpld nodes in consensus mode (`--start`, no `-a`)
  - Wait for all nodes to reach `"proposing"` state (120s timeout)

**Key new file**: `docker/telemetry/integration-test.sh`

**Verification**:

- [ ] Script starts without errors
- [ ] All 6 nodes reach "proposing" state
- [ ] Observability stack is healthy (otel-collector, Jaeger, Prometheus, Grafana)

---

## Task IT.2: RPC Span Verification (Phase 2)

**Objective**: Verify RPC spans flow through the telemetry pipeline.

**What to do**:

- Send `server_info`, `server_state`, `ledger` RPCs to node1 (port 5005)
- Wait for batch export (5s)
- Query Jaeger API for:
  - `rpc.request` spans (ServerHandler::onRequest)
  - `rpc.process` spans (ServerHandler::processRequest)
  - `rpc.command.server_info` spans (callMethod)
  - `rpc.command.server_state` spans (callMethod)
  - `rpc.command.ledger` spans (callMethod)
- Verify `xrpl.rpc.command` attribute present on `rpc.command.*` spans

**Verification**:

- [ ] Jaeger shows `rpc.request` traces
- [ ] Jaeger shows `rpc.process` traces
- [ ] Jaeger shows `rpc.command.*` traces with correct attributes

---

## Task IT.3: Transaction Span Verification (Phase 3)

**Objective**: Verify transaction spans flow through the telemetry pipeline.

**What to do**:

- Get genesis account sequence via `account_info` RPC
- Submit Payment transaction using genesis seed (`snoPBrXtMeMyMHUVTgbuqAfg1SUTb`)
- Wait for consensus inclusion (10s)
- Query Jaeger API for:
  - `tx.process` spans (NetworkOPsImp::processTransaction) on submitting node
  - `tx.receive` spans (PeerImp::handleTransaction) on peer nodes
- Verify `xrpl.tx.hash` attribute on `tx.process` spans
- Verify `xrpl.peer.id` attribute on `tx.receive` spans

**Verification**:

- [ ] Jaeger shows `tx.process` traces with `xrpl.tx.hash`
- [ ] Jaeger shows `tx.receive` traces with `xrpl.peer.id`

---

## Task IT.4: Consensus Span Verification (Phase 4)

**Objective**: Verify consensus spans flow through the telemetry pipeline.

**What to do**:

- Consensus runs automatically in 6-node network
- Query Jaeger API for:
  - `consensus.proposal.send` (Adaptor::propose)
  - `consensus.ledger_close` (Adaptor::onClose)
  - `consensus.accept` (Adaptor::onAccept)
  - `consensus.validation.send` (Adaptor::validate)
- Verify attributes:
  - `xrpl.consensus.mode` on `consensus.ledger_close`
  - `xrpl.consensus.proposers` on `consensus.accept`
  - `xrpl.consensus.ledger.seq` on `consensus.validation.send`

**Verification**:

- [ ] Jaeger shows `consensus.ledger_close` traces with `xrpl.consensus.mode`
- [ ] Jaeger shows `consensus.accept` traces with `xrpl.consensus.proposers`
- [ ] Jaeger shows `consensus.proposal.send` traces
- [ ] Jaeger shows `consensus.validation.send` traces

---

## Task IT.5: Spanmetrics Verification (Phase 5)

**Objective**: Verify spanmetrics connector derives RED metrics from spans.

**What to do**:

- Query Prometheus for `traces_span_metrics_calls_total`
- Query Prometheus for `traces_span_metrics_duration_milliseconds_count`
- Verify Grafana loads at `http://localhost:3000`

**Verification**:

- [ ] Prometheus returns non-empty results for `traces_span_metrics_calls_total`
- [ ] Prometheus returns non-empty results for duration histogram
- [ ] Grafana UI accessible with dashboards visible

---

## Task IT.6: Manual Testing Plan

**Objective**: Document how to run tests manually for future reference.

**What to do**:

- Create `docker/telemetry/TESTING.md` with:
  - Prerequisites section
  - Single-node standalone test (quick verification)
  - 6-node consensus test (full verification)
  - Expected span catalog (all 12 span names with attributes)
  - Verification queries (Jaeger API, Prometheus API)
  - Troubleshooting guide

**Key new file**: `docker/telemetry/TESTING.md`

**Verification**:

- [ ] Document covers both single-node and multi-node testing
- [ ] All 12 span names documented with source file and attributes
- [ ] Troubleshooting section covers common failure modes

---

## Task IT.7: Run and Verify

**Objective**: Execute the integration test and validate results.

**What to do**:

- Run `docker/telemetry/integration-test.sh` locally
- Debug any failures
- Leave stack running for manual verification
- Share URLs:
  - Jaeger: `http://localhost:16686`
  - Grafana: `http://localhost:3000`
  - Prometheus: `http://localhost:9090`

**Verification**:

- [ ] Script completes with all checks passing
- [ ] Jaeger UI shows rippled service with all expected span names
- [ ] Grafana dashboards load and show data

---

## Task IT.8: Commit

**Objective**: Commit all new files to Phase 5 branch.

**What to do**:

- Run `pcc` (pre-commit checks)
- Commit 3 new files to `pratik/otel-phase5-docs-deployment`

**Verification**:

- [ ] `pcc` passes
- [ ] Commit created on Phase 5 branch

---

## Summary

| Task | Description                   | New Files | Depends On |
| ---- | ----------------------------- | --------- | ---------- |
| IT.1 | Integration test script       | 1         | Phase 5    |
| IT.2 | RPC span verification         | 0         | IT.1       |
| IT.3 | Transaction span verification | 0         | IT.1       |
| IT.4 | Consensus span verification   | 0         | IT.1       |
| IT.5 | Spanmetrics verification      | 0         | IT.1       |
| IT.6 | Manual testing plan           | 1         | --         |
| IT.7 | Run and verify                | 0         | IT.1-IT.6  |
| IT.8 | Commit                        | 0         | IT.7       |

**Exit Criteria**:

- [ ] All 6 xrpld nodes reach "proposing" state
- [ ] All 11 expected span names visible in Jaeger
- [ ] Spanmetrics available in Prometheus
- [ ] Grafana dashboards show data
- [ ] Manual testing plan document complete
