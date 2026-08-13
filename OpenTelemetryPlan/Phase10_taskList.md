# Phase 10: Synthetic Workload Generation & Telemetry Validation — Task List

> **Status**: Future Enhancement
>
> **Goal**: Build tools that generate realistic XRPL traffic to validate the full Phases 1-9 telemetry stack end-to-end — all spans, attributes, metrics, dashboards, and log-trace correlation — under controlled load.
>
> **Scope**: Python/shell test harness + multi-node docker-compose environment + automated validation scripts + performance benchmarks.
>
> **Branch**: `pratik/otel-phase10-workload-validation` (from `pratik/otel-phase9-metric-gap-fill`)
>
> **Depends on**: Phase 9 (internal metric gap fill) — validates the full metric surface

### Related Plan Documents

| Document                                                             | Relevance                                                       |
| -------------------------------------------------------------------- | --------------------------------------------------------------- |
| [06-implementation-phases.md](./06-implementation-phases.md)         | Phase 10 plan: motivation, architecture, exit criteria (§6.8.3) |
| [09-data-collection-reference.md](./09-data-collection-reference.md) | Defines the full inventory of spans/metrics to validate         |
| [Phase9_taskList.md](./Phase9_taskList.md)                           | Prerequisite — all internal metrics must be emitting            |

### Why This Phase Exists

Before Phases 1-9 can be considered production-ready, we need proof that:

1. Every emitted span fires with its required attributes under real transaction
   workloads (the "16 spans / 22 attributes" figures below are stale; the harness
   derives both totals from `expected_spans.json`)
2. All 255+ StatsD metrics + ~50 Phase 9 metrics appear in Prometheus with non-zero values
3. Log-trace correlation (Phase 8) produces clickable trace_id links in Loki
4. The 14 harness-asserted Grafana dashboards render meaningful data (no empty
   panels); 15 are on disk
5. Performance overhead stays within bounds (< 3% CPU, < 5MB memory)
6. The telemetry stack survives sustained load without data loss or queue backpressure

---

## Task 10.1: Multi-Node Test Harness

**Objective**: Create a docker-compose environment with 3-5 validator nodes that produces real consensus rounds.

**What to do**:

- Create `docker/telemetry/docker-compose.workload.yaml` — **as shipped this file
  holds only the observability backend**: `otel-collector`, `tempo`,
  `prometheus`, `loki`, `grafana`. It contains **no xrpld services**.
  - Shared network (`workload-net`) with service discovery

- The 5 validators are **native `xrpld` processes**, not containers.
  `docker/telemetry/workload/run-full-validation.sh` (`NUM_NODES=5`) generates
  keys, writes a per-node `xrpld.cfg`, and launches each node on
  `127.0.0.1` with sequential RPC / WS / peer ports. Each node:
  - Gets its validator key from `generate-validator-keys.sh`
  - Lists the other 4 nodes in `ips_fixed`
  - Has all telemetry enabled: `[telemetry] enabled=1`, `[insight] server=otel`
  - Enables all trace categories including `trace_peer=1`
  - Writes logs to a file tailed by the OTel Collector filelog receiver

- ❌ **`make telemetry-workload-up` / `make telemetry-workload-down` were never
  implemented.** There is no `Makefile` anywhere in the repository. The entry
  point is `run-full-validation.sh` (with `--profile`, `--nodes`,
  `--skip-loki`, `--skip-regression`, `--with-benchmark`). The node-count flag is
  spelled `--nodes`, **not** `--num-nodes` — `run-full-validation.sh:80` (usage)
  and `:100` (the `case` arm). `NUM_NODES` is the internal shell variable it
  assigns to.

**Key files**:

- New: `docker/telemetry/docker-compose.workload.yaml` (backend only)
- New: `docker/telemetry/workload/generate-validator-keys.sh`
- New: `docker/telemetry/workload/run-full-validation.sh` — writes each node's
  cfg **inline** via a heredoc at `run-full-validation.sh:242`
  (`cat >"$NODE_DIR/xrpld.cfg" <<EOCFG`)
- New: `docker/telemetry/workload/xrpld-validator.cfg.template` (96 lines) — it
  **was** created and is tracked on the Phase 10 branch, but it is **unused**:
  nothing reads it, and its `{{NODE_INDEX}}` / `{{RPC_PORT}}` / `{{OTEL_ENDPOINT}}`
  placeholders are never substituted, because the inline heredoc above supersedes
  it. Either wire the script to the template or delete the template — keeping both
  guarantees they drift.

---

## Task 10.2: RPC Load Generator

**Objective**: Configurable tool that fires all traced RPC commands at controlled rates.

**What to do**:

- Create `docker/telemetry/workload/rpc_load_generator.py`:
  - Connects to one or more xrpld WebSocket endpoints
  - Fires all RPC commands that have trace spans: `server_info`, `ledger`, `tx`, `account_info`, `account_lines`, `fee`, `submit`, etc.
  - Configurable parameters: rate (RPS), duration, command distribution weights
  - Injects `traceparent` HTTP headers to test W3C context propagation
  - Logs progress and errors to stdout

- Command distribution should match realistic production ratios:
  - 40% `server_info` / `fee` (health checks)
  - 30% `account_info` / `account_lines` / `account_objects` (wallet queries)
  - 15% `ledger` / `ledger_data` (explorer queries)
  - 10% `tx` / `account_tx` (transaction lookups)
  - 5% `book_offers` / `amm_info` (DEX queries)

**Key files**:

- New: `docker/telemetry/workload/rpc_load_generator.py`
- New: `docker/telemetry/workload/requirements.txt`

---

## Task 10.3: Transaction Submitter

**Objective**: Generate diverse transaction types to exercise `tx.*` and `ledger.*` spans.

**What to do**:

- Create `docker/telemetry/workload/tx_submitter.py`:
  - Pre-funds test accounts from genesis account
  - Submits a mix of transaction types:
    - `Payment` (XRP and issued currencies) — exercises `tx.process`, `tx.apply`
    - `OfferCreate` / `OfferCancel` — DEX activity
    - `TrustSet` — trust line creation for issued currencies
    - `NFTokenMint` / `NFTokenCreateOffer` / `NFTokenAcceptOffer` — NFT activity
    - `EscrowCreate` / `EscrowFinish` — escrow lifecycle
    - `AMMCreate` / `AMMDeposit` / `AMMWithdraw` — AMM pool operations (if amendment enabled)
  - Configurable: TPS target, transaction mix weights, duration
  - Monitors submission results and tracks success/failure rates

- The transaction mix ensures the telemetry captures the full range of ledger activity that third parties care about.

**Key files**:

- New: `docker/telemetry/workload/tx_submitter.py`
- New: `docker/telemetry/workload/test_accounts.json` (pre-generated keypairs)

---

## Task 10.4: Telemetry Validation Suite

**Objective**: Automated scripts that verify all expected telemetry data exists after a workload run.

**What to do**:

- Create `docker/telemetry/workload/validate_telemetry.py`:

  **Span validation** (queries Tempo API):
  - Assert every span name in `expected_spans.json` appears in traces
  - Assert each span has its required attributes
  - Assert parent-child relationships are correct. `rpc.request` no longer
    exists — it split into `rpc.http_request` (HTTP) and `rpc.ws_message`
    (WebSocket) (`RpcSpanNames.h:135`, `:133`). The two live trees are:
    - HTTP: `rpc.http_request` → `rpc.process` → `rpc.command.*`
    - WebSocket: `rpc.ws_message` → `rpc.command.*` — **there is no
      `rpc.process` on the WS path**. `rpc.process` is created only in
      `ServerHandler::processRequest()` (`ServerHandler.cpp:705`), reached from
      `processSession(Session, coro)`, i.e. HTTP only. Under WS-only load
      `rpc.process` never appears, and `rpc.command.*` parents directly to
      `rpc.ws_message`.
  - Assert span durations are reasonable (> 0, < 60s)

  **Metric validation** (queries Prometheus API):
  - Assert all SpanMetrics-derived metrics are non-zero: `span_calls_total`,
    `span_duration_milliseconds_bucket` (the connector's `namespace` is `span`,
    not `traces_span_metrics` — `otel-collector-config.yaml:113-114`)
  - Assert the insight-sourced metrics are non-zero: `ledgermaster_validated_ledger_age`,
    `peer_finder_active_{inbound,outbound}_peers`, etc. — all lowercase, no
    `xrpld_` prefix (`77f35c03db` removed the prefix and lowercased names)
  - Assert all Phase 9 metrics are non-zero: `nodestore_state`, `cache_metrics`,
    `txq_metrics`, `rpc_method_{started,finished,errored}_total`, `object_count`,
    `load_factor_metrics`
  - Assert metric label cardinality is within bounds

  **Log-trace correlation validation** (queries Loki API):
  - Assert logs contain `trace_id=` and `span_id=` fields
  - Pick a random trace_id from Tempo → query Loki for matching logs → assert results exist
  - Assert Grafana derived field links are functional

  **Dashboard validation**:
  - For each dashboard, query the dashboard API and assert no panels show "No
    data". There are **15 dashboards on disk**; the harness asserts **14** —
    `log-derived-insights` is provisioned but unasserted.

- Output: JSON report with pass/fail per check, suitable for CI.

**Key files**:

- New: `docker/telemetry/workload/validate_telemetry.py`
- New: `docker/telemetry/workload/expected_spans.json` (span inventory for validation)
- New: `docker/telemetry/workload/expected_metrics.json` (metric inventory for validation)

---

## Task 10.5: Performance Benchmark Suite

**Objective**: Measure CPU/memory/latency overhead of the telemetry stack.

**What to do**:

- Create `docker/telemetry/workload/benchmark.sh`:
  - **Baseline run**: Start cluster with `[telemetry] enabled=0`, run transaction workload for 5 minutes, record metrics
  - **Telemetry run**: Start cluster with full telemetry enabled, run identical workload, record metrics
  - **Comparison**: Calculate deltas for:
    - CPU usage (per-node average)
    - Memory RSS (per-node peak)
    - RPC p99 latency
    - Transaction throughput (TPS)
    - Consensus round time p95
    - Ledger close time p95

- Output: Markdown table comparing baseline vs. telemetry, with pass/fail against targets:
  - CPU overhead < 3%
  - Memory overhead < 5MB
  - RPC latency impact < 2ms p99
  - Throughput impact < 5%
  - Consensus impact < 1%

- Store results in `docker/telemetry/workload/benchmark-results/` for historical tracking.

**Key files**:

- New: `docker/telemetry/workload/benchmark.sh`
- New: `docker/telemetry/workload/collect_system_metrics.sh`

---

## Task 10.6: CI Integration

**Objective**: Wire the validation suite into CI for regression detection.

**What to do**:

- Create a CI workflow (GitHub Actions or equivalent) that:
  1. Builds xrpld with `-DXRPL_ENABLE_TELEMETRY=ON`
  2. Starts the multi-node workload harness
  3. Runs the RPC load generator + transaction submitter for 2 minutes
  4. Runs the validation suite
  5. Runs the benchmark suite
  6. Fails the build if any validation check fails or benchmark exceeds thresholds
  7. Archives the validation report and benchmark results as artifacts

- This should be a separate workflow (not part of the main CI), triggered manually or on telemetry-related branch changes.

**Key files**:

- New: `.github/workflows/telemetry-validation.yml`
- New: `docker/telemetry/workload/run-full-validation.sh` (orchestrator script)

---

## Task 10.7: Documentation

**Objective**: Document the workload tools and validation process.

**What to do**:

- Create `docker/telemetry/workload/README.md`:
  - Quick start guide for running workload harness
  - Configuration options for load generator and tx submitter
  - How to read validation reports
  - How to run benchmarks and interpret results

- Update `docs/telemetry-runbook.md`:
  - Add "Validating Telemetry Stack" section
  - Add "Performance Benchmarking" section

- Update `OpenTelemetryPlan/09-data-collection-reference.md`:
  - Add "Validation" section with expected metric/span counts

---

## Exit Criteria

- [ ] 5-node validator cluster starts and reaches consensus — as native `xrpld`
      processes driven by `run-full-validation.sh`, not from docker-compose
- [ ] RPC load generator fires all traced RPC commands at configurable rates
- [ ] Transaction submitter generates 6+ transaction types at configurable TPS
- [ ] Validation suite confirms the full span / attribute / metric inventory
      (totals computed dynamically from `expected_spans.json` /
      `expected_metrics.json`, not the stale 16 / 22 figures)
- [ ] Log-trace correlation validated end-to-end (Loki ↔ Tempo) — implemented,
      but CI runs with `--skip-loki`, so it is not gated
- [ ] All 14 harness-asserted Grafana dashboards render data (no empty panels);
      15 on disk
- [ ] Benchmark shows < 3% CPU overhead, < 5MB memory overhead
- [ ] CI workflow runs validation on telemetry branch changes
- [ ] Validation report output is CI-parseable (JSON with exit codes)
