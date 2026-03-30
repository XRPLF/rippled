# Phase 11: Third-Party Data Collection Pipelines — Task List

> **Status**: Future Enhancement
>
> **Goal**: Build a custom OTel Collector receiver that periodically polls rippled's admin RPCs and exports structured metrics for external consumers — making all XRPL health, validator, peer, fee, and DEX data available as Prometheus/OTLP metrics without rippled code changes.
>
> **Scope**: Go-based OTel Collector receiver plugin + Grafana dashboards + Prometheus alerting rules.
>
> **Branch**: `pratik/otel-phase11-third-party-collection` (from `pratik/otel-phase10-workload-validation`)
>
> **Depends on**: Phase 10 (validation harness for testing the new receiver)

### Related Plan Documents

| Document                                                             | Relevance                                                       |
| -------------------------------------------------------------------- | --------------------------------------------------------------- |
| [06-implementation-phases.md](./06-implementation-phases.md)         | Phase 11 plan: motivation, architecture, exit criteria (§6.8.4) |
| [09-data-collection-reference.md](./09-data-collection-reference.md) | Defines full metric inventory including third-party metrics     |
| [Phase10_taskList.md](./Phase10_taskList.md)                         | Prerequisite — validation harness for testing                   |

### Third-Party Consumer Gap Analysis

This phase addresses the cross-cutting gap identified during research: **rippled has no native Prometheus/OTLP metrics export for data accessible only via RPC**. Every consumer (exchanges, payment processors, analytics providers, validators, researchers, compliance firms, custodians) must build custom JSON-RPC polling and conversion. This receiver centralizes that work.

| Consumer Category          | Data Unlocked by This Phase                                        |
| -------------------------- | ------------------------------------------------------------------ |
| **Exchanges**              | Real-time fee estimates, TxQ capacity, server health scores        |
| **Payment Processors**     | Settlement latency percentiles, corridor health, path availability |
| **Analytics Providers**    | Validator metrics, network topology, amendment voting status       |
| **DeFi / AMM**             | AMM pool TVL, DEX order book depth, trade volumes                  |
| **Validators / Operators** | Per-peer latency, version distribution, UNL health, alerting       |
| **Compliance**             | Transaction volume trends, network growth metrics                  |
| **Academic Researchers**   | Consensus performance time-series, decentralization metrics        |
| **CBDC / Tokenization**    | Token supply tracking, trust line adoption, freeze status          |
| **Institutional Custody**  | Multi-sig status, escrow tracking, reserve calculations            |
| **Wallet Providers**       | Server health for node selection, fee prediction data              |

---

## Task 11.1: OTel Collector Receiver Scaffold

**Objective**: Create the Go project structure for a custom OTel Collector receiver that polls rippled JSON-RPC.

**What to do**:

- Create `docker/telemetry/otel-rippled-receiver/`:
  - `receiver.go` — implements `receiver.Metrics` interface
  - `config.go` — configuration struct (endpoint, poll interval, enabled RPCs)
  - `factory.go` — receiver factory registration
  - `go.mod` / `go.sum` — Go module with OTel Collector SDK dependency

- Configuration model:

  ```yaml
  rippled_receiver:
    endpoint: "http://localhost:5005" # rippled admin RPC
    poll_interval: 30s # how often to poll
    enabled_collectors:
      - server_info
      - get_counts
      - fee
      - peers
      - validators
      - feature
      - server_state
    amm_pools: [] # optional: AMM pool IDs to track
    book_offers_pairs: [] # optional: currency pairs for DEX depth
  ```

- Build a custom OTel Collector binary that includes this receiver alongside the standard receivers.

**Key files**:

- New: `docker/telemetry/otel-rippled-receiver/receiver.go`
- New: `docker/telemetry/otel-rippled-receiver/config.go`
- New: `docker/telemetry/otel-rippled-receiver/factory.go`
- New: `docker/telemetry/otel-rippled-receiver/go.mod`
- New: `docker/telemetry/otel-rippled-receiver/Dockerfile`

---

## Task 11.2: server_info / server_state Collector

**Objective**: Poll `server_info` and `server_state` and export all fields as OTel metrics.

**What to do**:

- Implement `serverInfoCollector` that calls `server_info` (admin) and extracts:

  **Node Health Gauges:**
  - `xrpl_server_state` (enum → int: disconnected=0, connected=1, syncing=2, tracking=3, full=4, proposing=5)
  - `xrpl_server_state_duration_seconds`
  - `xrpl_uptime_seconds`
  - `xrpl_io_latency_ms`
  - `xrpl_amendment_blocked` (0 or 1)
  - `xrpl_peers_count`
  - `xrpl_peer_disconnects_total`
  - `xrpl_peer_disconnects_resources_total`
  - `xrpl_jq_trans_overflow_total`

  **Consensus Gauges:**
  - `xrpl_last_close_proposers`
  - `xrpl_last_close_converge_time_seconds`
  - `xrpl_validation_quorum`

  **Ledger Gauges:**
  - `xrpl_validated_ledger_seq`
  - `xrpl_validated_ledger_age_seconds`
  - `xrpl_validated_ledger_base_fee_drops`
  - `xrpl_validated_ledger_reserve_base_drops`
  - `xrpl_validated_ledger_reserve_inc_drops`
  - `xrpl_close_time_offset_seconds` (0 when absent)

  **Load Factor Gauges:**
  - `xrpl_load_factor`
  - `xrpl_load_factor_server`
  - `xrpl_load_factor_fee_escalation`
  - `xrpl_load_factor_fee_queue`
  - `xrpl_load_factor_local`
  - `xrpl_load_factor_net`
  - `xrpl_load_factor_cluster`

  **State Accounting Gauges** (per state: disconnected, connected, syncing, tracking, full):
  - `xrpl_state_duration_seconds{state="<name>"}`
  - `xrpl_state_transitions_total{state="<name>"}`

  **Validator Info** (when node is a validator):
  - `xrpl_validator_list_count`
  - `xrpl_validator_list_expiration_seconds` (epoch)
  - `xrpl_validator_list_active` (0 or 1)

**Key files**:

- New: `docker/telemetry/otel-rippled-receiver/collectors/server_info.go`

---

## Task 11.3: get_counts Collector

**Objective**: Poll `get_counts` and export internal object counts and NodeStore stats.

**What to do**:

- Implement `getCountsCollector`:

  **Database Gauges:**
  - `xrpl_db_size_kb{db="total"}`, `xrpl_db_size_kb{db="ledger"}`, `xrpl_db_size_kb{db="transaction"}`

  **NodeStore Gauges:**
  - `xrpl_nodestore_reads_total`, `xrpl_nodestore_reads_hit`, `xrpl_nodestore_writes_total`
  - `xrpl_nodestore_read_bytes`, `xrpl_nodestore_written_bytes`
  - `xrpl_nodestore_read_duration_us`, `xrpl_nodestore_write_load`
  - `xrpl_nodestore_read_queue`, `xrpl_nodestore_read_threads_running`

  **Cache Gauges:**
  - `xrpl_cache_hit_rate{cache="SLE"}`, `xrpl_cache_hit_rate{cache="ledger"}`, `xrpl_cache_hit_rate{cache="accepted_ledger"}`
  - `xrpl_cache_size{cache="treenode"}`, `xrpl_cache_size{cache="fullbelow"}`, `xrpl_cache_size{cache="accepted_ledger"}`

  **Object Count Gauges:**
  - `xrpl_object_count{type="<name>"}` for each counted object type (Transaction, Ledger, NodeObject, STTx, STLedgerEntry, InboundLedger, Pathfinder, etc.)

  **Rates:**
  - `xrpl_historical_fetch_per_minute`
  - `xrpl_local_txs`

**Key files**:

- New: `docker/telemetry/otel-rippled-receiver/collectors/get_counts.go`

---

## Task 11.4: Peer Topology Collector

**Objective**: Poll `peers` and export per-peer and aggregate network metrics.

**What to do**:

- Implement `peersCollector`:

  **Aggregate Gauges:**
  - `xrpl_peers_inbound_count`
  - `xrpl_peers_outbound_count`
  - `xrpl_peers_cluster_count`

  **Per-Peer Gauges** (with labels `peer_key` truncated to 8 chars for cardinality control):
  - `xrpl_peer_latency_ms{peer="<key>", version="<ver>", inbound="<bool>"}`
  - `xrpl_peer_uptime_seconds{peer="<key>"}`
  - `xrpl_peer_load{peer="<key>"}`

  **Distribution Gauges** (aggregated across all peers):
  - `xrpl_peer_latency_p50_ms`, `xrpl_peer_latency_p95_ms`, `xrpl_peer_latency_p99_ms`
  - `xrpl_peer_version_count{version="<semver>"}` — count of peers per software version

  **Tracking Status:**
  - `xrpl_peer_diverged_count` — peers with `track=diverged`
  - `xrpl_peer_unknown_count` — peers with `track=unknown`

**Key files**:

- New: `docker/telemetry/otel-rippled-receiver/collectors/peers.go`

**Cardinality note**: Per-peer metrics use truncated keys. For large peer sets (50+), the aggregate distribution gauges are preferred over per-peer labels.

---

## Task 11.5: Validator & Amendment Collector

**Objective**: Poll `validators` and `feature` to export validator health and amendment voting status.

**What to do**:

- Implement `validatorCollector`:

  **From `validators` RPC:**
  - `xrpl_trusted_validators_count`
  - `xrpl_validator_signing` (0 or 1 — whether local validator is signing)

  **From `feature` RPC:**
  - `xrpl_amendment_enabled_count` — total enabled amendments
  - `xrpl_amendment_majority_count` — amendments with majority but not yet enabled
  - `xrpl_amendment_vetoed_count` — locally vetoed amendments
  - `xrpl_amendment_unsupported_majority` (0 or 1) — any unsupported amendment has majority (critical alert)

  **Per-amendment with majority** (limited cardinality — only amendments with `majority` set):
  - `xrpl_amendment_majority_time{name="<amendment>"}` — epoch time when majority was gained
  - `xrpl_amendment_votes{name="<amendment>"}` — current vote count
  - `xrpl_amendment_threshold{name="<amendment>"}` — votes needed

**Key files**:

- New: `docker/telemetry/otel-rippled-receiver/collectors/validators.go`

---

## Task 11.6: Fee & TxQ Collector

**Objective**: Poll `fee` RPC and export real-time fee market data.

**What to do**:

- Implement `feeCollector` that calls the public `fee` RPC:

  **Fee Level Gauges:**
  - `xrpl_fee_current_ledger_size` — transactions in current open ledger
  - `xrpl_fee_expected_ledger_size` — expected transactions at close
  - `xrpl_fee_max_queue_size` — maximum transaction queue size
  - `xrpl_fee_open_ledger_fee_drops` — minimum fee for open ledger inclusion
  - `xrpl_fee_median_fee_drops` — median fee level
  - `xrpl_fee_minimum_fee_drops` — base reference fee
  - `xrpl_fee_queue_size` — current queue depth

- This overlaps with Phase 9's internal TxQ metrics but provides an external-only collection path that doesn't require rippled code changes.

**Key files**:

- New: `docker/telemetry/otel-rippled-receiver/collectors/fee.go`

---

## Task 11.7: DEX & AMM Collector (Optional)

**Objective**: Periodically poll configured AMM pools and order book pairs for DeFi metrics.

**What to do**:

- Implement `dexCollector` (enabled only when `amm_pools` or `book_offers_pairs` are configured):

  **AMM Pool Gauges** (per configured pool):
  - `xrpl_amm_reserve{pool="<id>", asset="<currency>"}` — pool reserve amount
  - `xrpl_amm_lp_token_supply{pool="<id>"}` — outstanding LP tokens
  - `xrpl_amm_trading_fee{pool="<id>"}` — pool trading fee (basis points)
  - `xrpl_amm_tvl_drops{pool="<id>"}` — total value locked (XRP-denominated)

  **Order Book Gauges** (per configured pair):
  - `xrpl_orderbook_bid_depth{pair="<base>/<quote>"}` — total bid volume
  - `xrpl_orderbook_ask_depth{pair="<base>/<quote>"}` — total ask volume
  - `xrpl_orderbook_spread{pair="<base>/<quote>"}` — best bid-ask spread
  - `xrpl_orderbook_offer_count{pair="<base>/<quote>", side="bid|ask"}` — number of offers

**Key files**:

- New: `docker/telemetry/otel-rippled-receiver/collectors/dex.go`

**Note**: This is optional because it requires explicit configuration of which pools/pairs to track. Default configuration tracks no DEX data.

---

## Task 11.8: Prometheus Alerting Rules

**Objective**: Create production-ready alerting rules for the metrics exported by this receiver.

**What to do**:

- Create `docker/telemetry/prometheus/rippled-alerts.yml`:

  **Tier 1 — Critical (page immediately):**

  ```yaml
  - alert: XRPLServerNotFull
    expr: xrpl_server_state < 4
    for: 15m

  - alert: XRPLAmendmentBlocked
    expr: xrpl_amendment_blocked == 1
    for: 1m

  - alert: XRPLNoPeers
    expr: xrpl_peers_count == 0
    for: 5m

  - alert: XRPLLedgerStale
    expr: xrpl_validated_ledger_age_seconds > 120
    for: 2m

  - alert: XRPLHighIOLatency
    expr: xrpl_io_latency_ms > 100
    for: 5m

  - alert: XRPLUnsupportedAmendmentMajority
    expr: xrpl_amendment_unsupported_majority == 1
    for: 1m
  ```

  **Tier 2 — Warning (investigate within hours):**

  ```yaml
  - alert: XRPLLowPeerCount
    expr: xrpl_peers_count < 10
    for: 15m

  - alert: XRPLHighLoadFactor
    expr: xrpl_load_factor > 10
    for: 10m

  - alert: XRPLSlowConsensus
    expr: xrpl_last_close_converge_time_seconds > 6
    for: 5m

  - alert: XRPLValidatorListExpiring
    expr: (xrpl_validator_list_expiration_seconds - time()) < 86400
    for: 1h

  - alert: XRPLClockDrift
    expr: xrpl_close_time_offset_seconds > 0
    for: 5m

  - alert: XRPLStateFlapping
    expr: rate(xrpl_state_transitions_total{state="full"}[1h]) > 2
    for: 30m
  ```

**Key files**:

- New: `docker/telemetry/prometheus/rippled-alerts.yml`
- Update: `docker/telemetry/prometheus/prometheus.yml` (add rule_files reference)

---

## Task 11.9: New Grafana Dashboards

**Objective**: Create 4 new dashboards for the data exported by the receiver.

**What to do**:

- **Validator Health** (`rippled-validator-health`):
  - Server state timeline, state duration breakdown
  - Proposer count trend, converge time trend, validation quorum
  - Validator list expiration countdown
  - Amendment voting status (majority/enabled/vetoed)

- **Network Topology** (`rippled-network-topology`):
  - Peer count (inbound/outbound/cluster), peer version distribution
  - Peer latency distribution (p50/p95/p99), diverged peer count
  - Geographic distribution (if enriched with GeoIP)
  - Peer uptime distribution

- **Fee Market** (`rippled-fee-market-external`):
  - Current fee levels (open ledger, median, minimum), fee escalation timeline
  - Queue depth vs. capacity, transactions per ledger
  - Load factor breakdown (server/network/cluster/escalation)

- **DEX & AMM Overview** (`rippled-dex-amm`) (only populated when DEX collectors are configured):
  - AMM pool TVL, reserve ratios, LP token supply
  - Order book depth per pair, spread trends
  - Trading fee revenue estimates

**Key files**:

- New: `docker/telemetry/grafana/dashboards/rippled-validator-health.json`
- New: `docker/telemetry/grafana/dashboards/rippled-network-topology.json`
- New: `docker/telemetry/grafana/dashboards/rippled-fee-market-external.json`
- New: `docker/telemetry/grafana/dashboards/rippled-dex-amm.json`

---

## Task 11.10: Integration with Phase 10 Validation

**Objective**: Extend the Phase 10 validation suite to verify this receiver's metrics.

**What to do**:

- Update `docker/telemetry/workload/validate_telemetry.py`:
  - Add assertions for all `xrpl_*` metrics produced by the receiver
  - Verify metric labels have expected values
  - Verify alerting rules fire correctly (inject a "bad" state and check alert)

- Update `docker/telemetry/docker-compose.workload.yaml`:
  - Add the custom OTel Collector build with the rippled receiver
  - Configure the receiver to poll one of the test nodes

**Key files**:

- Update: `docker/telemetry/workload/validate_telemetry.py`
- Update: `docker/telemetry/docker-compose.workload.yaml`
- Update: `docker/telemetry/workload/expected_metrics.json`

---

## Task 11.11: Documentation

**Objective**: Document the receiver, its metrics, deployment, and alerting.

**What to do**:

- Create `docker/telemetry/otel-rippled-receiver/README.md`:
  - Architecture overview (how the receiver fits into the OTel Collector)
  - Configuration reference (all config options with defaults)
  - Metric reference table (all exported metrics with types and labels)
  - Deployment guide (building custom collector binary, docker-compose integration)

- Update `OpenTelemetryPlan/09-data-collection-reference.md`:
  - Add "Third-Party Metrics (OTel Collector Receiver)" section
  - Add new Grafana dashboard reference (4 dashboards)
  - Add alerting rules reference

- Update `docs/telemetry-runbook.md`:
  - Add "Third-Party Metrics Receiver" troubleshooting section
  - Add alerting playbook (what to do for each Tier 1/Tier 2 alert)

---

## Task 11.12: Alert Rules for External Dashboard Parity Metrics

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md) — 18 alert rules ported from the community [xrpl-validator-dashboard](https://github.com/realgrapedrop/xrpl-validator-dashboard).
>
> **Upstream**: Phase 7 Tasks 7.9-7.16 (metrics), Phase 9 Tasks 9.11-9.13 (dashboards).
> **Downstream**: None — terminal task in the parity chain.

**Objective**: Add Grafana alerting rules for the Phase 7+ parity metrics (validation agreement, validator health, peer quality, state tracking, ledger economy). These complement Task 11.8's `xrpl_*` alerts by covering the `rippled_*` internal metrics.

**Critical Group** (8 rules, eval interval 10s):

| Rule                | Condition                                                       | For |
| ------------------- | --------------------------------------------------------------- | --- |
| Agreement Below 90% | `rippled_validation_agreement{metric="agreement_pct_24h"} < 90` | 30s |
| Not Proposing       | `rippled_state_tracking{metric="state_value"} < 6`              | 10s |
| Unhealthy State     | `rippled_state_tracking{metric="state_value"} < 4`              | 10s |
| Amendment Blocked   | `rippled_validator_health{metric="amendment_blocked"} == 1`     | 1m  |
| UNL Expiring        | `rippled_validator_health{metric="unl_expiry_days"} < 14`       | 1h  |
| High IO Latency     | `histogram_quantile(0.95, rippled_ios_latency_bucket) > 50`     | 1m  |
| High Load Factor    | `rippled_load_factor_metrics{metric="load_factor"} > 1000`      | 1m  |
| Peer Count Critical | `rippled_server_info{metric="peers"} < 5`                       | 1m  |

**Network Group** (3 rules, eval interval 10s):

| Rule                      | Condition                                                           | For |
| ------------------------- | ------------------------------------------------------------------- | --- |
| Peer Drop >10%            | `delta(rippled_server_info{metric="peers"}[30s]) / ... * 100 < -10` | 30s |
| Peer Drop >30%            | Same formula, threshold -30                                         | 30s |
| P90 Latency + Disconnects | `peer_latency_p90_ms > 500 AND rate(disconnects) > 0`               | 2m  |

**Performance Group** (7 rules, eval interval 10s):

| Rule                | Condition                                                      | For |
| ------------------- | -------------------------------------------------------------- | --- |
| CPU High            | Per-core CPU > 80% (requires node_exporter)                    | 2m  |
| Memory Critical     | Memory usage > 90% (requires node_exporter)                    | 1m  |
| Disk Warning        | Disk usage > 85% (requires node_exporter)                      | 2m  |
| Job Queue Overflow  | `rate(rippled_jq_trans_overflow_total[5m]) > 0`                | 1m  |
| Upgrade Recommended | `rippled_peer_quality{metric="peers_higher_version_pct"} > 60` | 1m  |
| TX Rate Drop        | Transaction rate dropped > 50% in 5m window                    | 5m  |
| Stale Ledger        | `rippled_ledger_economy{metric="ledger_age_seconds"} > 30`     | 1m  |

**Notification channel templates**: Email/SMTP, Discord, Slack, PagerDuty.

**Key files**:

- New/extend: `docker/telemetry/grafana/alerting/alert-rules-parity.yaml`
- New: `docker/telemetry/grafana/alerting/contact-points.yaml` (template configs)
- New: `docker/telemetry/grafana/alerting/notification-policies.yaml`

**Exit Criteria**:

- [ ] All 18 rules evaluate without errors in Grafana alerting UI
- [ ] Critical rules fire within expected timeframe when conditions are met
- [ ] Notification channel templates are documented (not hard-coded to any service)

---

## Task 11.13: Dual-Datasource Architecture Documentation

> **Source**: [External Dashboard Parity](../docs/superpowers/specs/2026-03-30-external-dashboard-parity-design.md)

**Objective**: Document the external dashboard's "fast path" pattern as a future optimization for real-time panels.

**Pattern**: A lightweight Prometheus scrape endpoint (separate from OTLP pipeline) that polls critical metrics every 2-5s, bypassing the 10s OTLP metric reader interval and Prometheus scrape interval.

**Use case**: Real-time state panels (server state, ledger age, peer count) where 10-15s latency is too slow for operational dashboards.

**Decision**: Document as a future option, not implement now. The current 10s interval is acceptable for v1. The external dashboard achieves 2-5s freshness by polling RPC directly, which is what the Phase 11 receiver already does. Adding a separate scrape endpoint to rippled would only be needed if sub-second metric freshness is required from the internal metrics pipeline.

**What to document**:

- Architecture comparison: OTLP pipeline (10-15s) vs. direct scrape (2-5s) vs. push gateway
- When to consider: operator feedback indicating 10s is insufficient for alerting SLOs
- How to implement if needed: add `/metrics` HTTP endpoint to rippled with Prometheus client library
- Trade-offs: additional port, additional dependency, duplication with OTLP metrics

**Key files**:

- Update: `OpenTelemetryPlan/09-data-collection-reference.md` (add "Future: Dual-Datasource Architecture" section)
- Update: `docs/telemetry-runbook.md` (add brief note in performance tuning section)

**Exit Criteria**:

- [ ] Architecture comparison documented with clear trade-offs
- [ ] Decision rationale recorded (why deferred, when to revisit)

---

## Exit Criteria

- [ ] Custom OTel Collector receiver builds and starts without errors
- [ ] All `xrpl_*` metrics from server_info, get_counts, peers, validators, fee appear in Prometheus
- [ ] Metrics update at configured poll interval (default 30s)
- [ ] 4 new Grafana dashboards operational with data
- [ ] Prometheus alerting rules fire correctly for simulated failure conditions
- [ ] DEX/AMM collector works when configured (optional — not required for base exit criteria)
- [ ] Phase 10 validation suite passes with receiver metrics included
- [ ] Receiver handles rippled restart/unavailability gracefully (no crash, logs warning, retries)
- [ ] Documentation complete: receiver README, metric reference, alerting playbook
- [ ] Go receiver has unit tests with >80% coverage
- [ ] 18 Grafana alert rules for Phase 7+ parity metrics evaluate correctly (Task 11.12)
- [ ] Dual-datasource architecture documented with trade-offs (Task 11.13)
