# Phase 11: Third-Party Data Collection Pipelines — Task List

> **Status**: Not started — 0 of 13 tasks complete (`grep -c '^## Task 11\.'` = 13:
> Tasks 11.1 through 11.13). Verified against the tree:
> no `.go` files exist anywhere, `docker/telemetry/otel-rippled-receiver/` does
> not exist, `docker/telemetry/prometheus/` does not exist (so no
> `prometheus/rippled-alerts.yml`), and no `network-topology` / `dex-amm`
> dashboards are present under `docker/telemetry/grafana/dashboards/`. **No Phase 11 work has
> been done, so no task box below may be ticked.**
>
> One **prerequisite** box is ticked, and only one: Task 11.12's
> "`state_tracking` gauge implemented (Task 7.12)". That is an upstream
> dependency satisfied by Phase 7/9 code, not Phase 11 work — see the citation
> there.
>
> **Goal**: Build a custom OTel Collector receiver that periodically polls xrpld's admin RPCs and exports structured metrics for external consumers — making all XRPL health, validator, peer, fee, and DEX data available as Prometheus/OTLP metrics without xrpld code changes.
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

This phase addresses the cross-cutting gap identified during research: **xrpld has no native Prometheus/OTLP metrics export for data accessible only via RPC**. Every consumer (exchanges, payment processors, analytics providers, validators, researchers, compliance firms, custodians) must build custom JSON-RPC polling and conversion. This receiver centralizes that work.

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

**Objective**: Create the Go project structure for a custom OTel Collector receiver that polls xrpld JSON-RPC.

**What to do**:

- Create `docker/telemetry/otel-rippled-receiver/`:
  - `receiver.go` — implements `receiver.Metrics` interface
  - `config.go` — configuration struct (endpoint, poll interval, enabled RPCs)
  - `factory.go` — receiver factory registration
  - `go.mod` / `go.sum` — Go module with OTel Collector SDK dependency

- Configuration model:

  ```yaml
  xrpld_receiver:
    endpoint: "http://localhost:5005" # xrpld admin RPC
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

- This overlaps with Phase 9's internal TxQ metrics but provides an external-only collection path that doesn't require xrpld code changes.

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

**Objective**: Create production-ready alerting rules for the `xrpl_*` metrics
exported by this receiver.

> **Scope note — do not duplicate Phase 9.** Phase 9 already ships provisioned
> **Grafana** alerting at
> `docker/telemetry/grafana/provisioning/alerting/{rules,contactpoints,policies}.yaml`
> — 13 rules in 5 groups, 2 contact points (`xrpld-default` Slack,
> `xrpld-critical` Slack + email), and a nested notification policy keyed on
> `severity = critical`. Four of the rules below overlap it:
>
> | Rule here           | Addressed by (Phase 9)                            | Coverage                                                                                                         |
> | ------------------- | ------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
> | `XRPLServerNotFull` | `NodeNotFull` (group `xrpld-node-state`)          | Full                                                                                                             |
> | `XRPLLedgerStale`   | `ValidatedLedgerStale` (group `xrpld-consensus`)  | **Partial** — Phase 9: `ledgermaster_validated_ledger_age > 60` for 5m; the external shape is `> 30` for 1m      |
> | `XRPLHighIOLatency` | `NodeStoreIOLatencyHigh` (group `xrpld-jobqueue`) | **Partial** — Phase 9: p95 of `ios_latency_milliseconds_bucket` **> 1000 ms for 10m**; external: **> 50 for 1m** |
> | `XRPLStateFlapping` | `NodeStateFlapping` (group `xrpld-node-state`)    | Full                                                                                                             |
>
> The remaining 8 (`XRPLAmendmentBlocked`, `XRPLNoPeers`,
> `XRPLUnsupportedAmendmentMajority`, `XRPLLowPeerCount`, `XRPLHighLoadFactor`,
> `XRPLSlowConsensus`, `XRPLValidatorListExpiring`, `XRPLClockDrift`) are
> genuinely new. Note the two sets watch different metric surfaces — the Phase 9
> rules fire on xrpld's own OTLP metrics, these on the receiver's `xrpl_*`
> metrics — so if both are kept, dedupe the notification policy to avoid
> double-paging on the same underlying condition.
>
> `docker/telemetry/prometheus/` does not exist today. Prefer extending the
> Phase 9 Grafana provisioning tree over introducing a second, Prometheus-native
> alerting mechanism; if a `prometheus/` tree is added anyway, say explicitly in
> its header which alerts it owns.

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

> **UID COLLISION — pick a different uid.** Phase 9 already ships
> `docker/telemetry/grafana/dashboards/validator-health.json` with
> **uid `validator-health`** (17 panels, backed by xrpld's own
> `validation_agreement` / `validator_health` / `state_tracking` OTLP metrics).
> Provisioning a second dashboard with the same uid makes Grafana overwrite one
> with the other — whichever the provisioner loads last wins, silently. Use a
> distinct uid such as `validator-health-external` (and a distinct filename), the
> same way this task already disambiguates Fee Market as
> `xrpld-fee-market-external` against Phase 9's `fee-market`. Also check
> `peer-quality`, `fee-market`, `job-queue` and `node-health` before adding any
> further uid.

**What to do**:

- **Validator Health** (`validator-health-external` — **not** `validator-health`,
  see the collision note above):
  - Server state timeline, state duration breakdown
  - Proposer count trend, converge time trend, validation quorum
  - Validator list expiration countdown
  - Amendment voting status (majority/enabled/vetoed)

- **Network Topology** (`xrpld-network-topology`):
  - Peer count (inbound/outbound/cluster), peer version distribution
  - Peer latency distribution (p50/p95/p99), diverged peer count
  - Geographic distribution (if enriched with GeoIP)
  - Peer uptime distribution

- **Fee Market** (`xrpld-fee-market-external`):
  - Current fee levels (open ledger, median, minimum), fee escalation timeline
  - Queue depth vs. capacity, transactions per ledger
  - Load factor breakdown (server/network/cluster/escalation)

- **DEX & AMM Overview** (`xrpld-dex-amm`) (only populated when DEX collectors are configured):
  - AMM pool TVL, reserve ratios, LP token supply
  - Order book depth per pair, spread trends
  - Trading fee revenue estimates

**Key files**:

- New: `docker/telemetry/grafana/dashboards/validator-health-external.json`
  (**must not** reuse Phase 9's `validator-health.json` / uid `validator-health`)
- New: `docker/telemetry/grafana/dashboards/network-topology.json`
- New: `docker/telemetry/grafana/dashboards/fee-market-external.json`
  (Phase 9 owns `fee-market.json` / uid `fee-market`)
- New: `docker/telemetry/grafana/dashboards/dex-amm.json`

> Filenames drop the legacy `dashboards/rippled-*` prefix: `145b1469d6` and
> `25868f2740` renamed every dashboard to bare names with bare uids, so no
> `dashboards/rippled-*.json` path exists in the tree.

---

## Task 11.10: Integration with Phase 10 Validation

**Objective**: Extend the Phase 10 validation suite to verify this receiver's metrics.

**What to do**:

- Update `docker/telemetry/workload/validate_telemetry.py`:
  - Add assertions for all `xrpl_*` metrics produced by the receiver
  - Verify metric labels have expected values
  - Verify alerting rules fire correctly (inject a "bad" state and check alert)

- Update `docker/telemetry/docker-compose.workload.yaml`:
  - Add the custom OTel Collector build with the xrpld receiver
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

> **Source**: [External Dashboard Parity](./06-implementation-phases.md#appendix-external-dashboard-parity) — 18 alert rules ported from the community [xrpl-validator-dashboard](https://github.com/realgrapedrop/xrpl-validator-dashboard).
>
> **Upstream**: Phase 7 Tasks 7.9-7.16 (metrics), Phase 9 Tasks 9.11-9.13 (dashboards).
> **Downstream**: None — terminal task in the parity chain.

**Objective**: Add Grafana alerting rules for the Phase 7+ parity metrics (validation agreement, validator health, peer quality, state tracking, ledger economy). These complement Task 11.8's `xrpl_*` alerts by covering the internal metrics.

> **4 of the 18 are addressed by Phase 9** — 2 fully, 2 only partially. Extend,
> do not blindly re-create:
>
> | Rule here          | Addressed by (Phase 9)                                                                      | Coverage                                                                                                                                                                    |
> | ------------------ | ------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
> | Unhealthy State    | `NodeNotFull` (group `xrpld-node-state`)                                                    | Full                                                                                                                                                                        |
> | High IO Latency    | `NodeStoreIOLatencyHigh` (group `xrpld-jobqueue`, p95 of `ios_latency_milliseconds_bucket`) | **Partial** — Phase 9 fires at p95 **> 1000 ms for 10m**; the rule below wants **> 50 for 1m** (20× tighter)                                                                |
> | Job Queue Overflow | `JobQueueTxOverflow` (group `xrpld-jobqueue`, `jq_trans_overflow_total`)                    | Full                                                                                                                                                                        |
> | Stale Ledger       | `ValidatedLedgerStale` (group `xrpld-consensus`, `ledgermaster_validated_ledger_age`)       | **Partial** — different metric: Phase 9 uses `ledgermaster_validated_ledger_age > 60` for 5m; the rule below uses `ledger_economy{metric="ledger_age_seconds"} > 30` for 1m |
>
> The two **Partial** rows are not closed. Either re-baseline the Phase 9
> thresholds or ship the tighter variants here — do not skip them as duplicates.
>
> Remaining open work is **14 rules**, of which **3** (CPU High, Memory Critical,
> Disk Warning) need `node_exporter`, which is not in the stack. Nothing else is
> blocked: "Not Proposing" used to be listed as blocked on an unimplemented
> `state_tracking` gauge, but that gauge **ships** — see the Exit Criteria note
> below.
>
> **Metric-name translation.** Names carry **no** `xrpld_` prefix
> (`77f35c03db`), so as a rule of thumb read every `xrpld_<name>` below as plain
> `<name>`. **Two shapes do not follow that rule:**
>
> - **Multiplexed observable gauges.** Many readings are a `metric` **label
>   value** on a shared instrument, not a metric name. `xrpld_txq_count` is
>   `txq_metrics{metric="txq_count"}`; likewise `load_factor_metrics{…}`,
>   `nodestore_state{…}`, `cache_metrics{…}`. The rows below that already use the
>   `<instrument>{metric="…"}` form (`state_tracking`, `validator_health`,
>   `validation_agreement`, `server_info`, `peer_quality`, `load_factor_metrics`,
>   `ledger_economy`) are correct; only drop the prefix on those.
> - **Unit-suffixed histograms** from `beast::insight`. `OTelCollectorImp` appends
>   the unit, so `xrpld_ios_latency_bucket` is really
>   `ios_latency_milliseconds_bucket` — the spelling used by
>   `node-health.json:577` and `ledger-data-sync.json:1353`.

**Critical Group** (8 rules, eval interval 10s):

| Rule                | Condition                                                        | For |
| ------------------- | ---------------------------------------------------------------- | --- |
| Agreement Below 90% | `xrpld_validation_agreement{metric="agreement_pct_24h"} < 90`    | 30s |
| Not Proposing       | `xrpld_state_tracking{metric="state_value"} < 6`                 | 10s |
| Unhealthy State     | `xrpld_state_tracking{metric="state_value"} < 4`                 | 10s |
| Amendment Blocked   | `xrpld_validator_health{metric="amendment_blocked"} == 1`        | 1m  |
| UNL Expiring        | `xrpld_validator_health{metric="unl_expiry_days"} < 14`          | 1h  |
| High IO Latency     | `histogram_quantile(0.95, ios_latency_milliseconds_bucket) > 50` | 1m  |
| High Load Factor    | `xrpld_load_factor_metrics{metric="load_factor"} > 1000`         | 1m  |
| Peer Count Critical | `xrpld_server_info{metric="peers"} < 5`                          | 1m  |

**Network Group** (3 rules, eval interval 10s):

| Rule                      | Condition                                                         | For |
| ------------------------- | ----------------------------------------------------------------- | --- |
| Peer Drop >10%            | `delta(xrpld_server_info{metric="peers"}[30s]) / ... * 100 < -10` | 30s |
| Peer Drop >30%            | Same formula, threshold -30                                       | 30s |
| P90 Latency + Disconnects | `peer_latency_p90_ms > 500 AND rate(disconnects) > 0`             | 2m  |

**Performance Group** (7 rules, eval interval 10s):

| Rule                | Condition                                                    | For |
| ------------------- | ------------------------------------------------------------ | --- |
| CPU High            | Per-core CPU > 80% (requires node_exporter)                  | 2m  |
| Memory Critical     | Memory usage > 90% (requires node_exporter)                  | 1m  |
| Disk Warning        | Disk usage > 85% (requires node_exporter)                    | 2m  |
| Job Queue Overflow  | `rate(xrpld_jq_trans_overflow_total[5m]) > 0`                | 1m  |
| Upgrade Recommended | `xrpld_peer_quality{metric="peers_higher_version_pct"} > 60` | 1m  |
| TX Rate Drop        | Transaction rate dropped > 50% in 5m window                  | 5m  |
| Stale Ledger        | `xrpld_ledger_economy{metric="ledger_age_seconds"} > 30`     | 1m  |

**Notification channel templates**: Slack and Email/SMTP already ship in Phase
9's `contactpoints.yaml` (`xrpld-default`, `xrpld-critical`). Discord and
PagerDuty templates remain open.

**Key files** — extend the **Phase 9** provisioning tree. The
`docker/telemetry/grafana/alerting/` directory named in the original spec has
never existed in any commit; the real location is
`docker/telemetry/grafana/provisioning/alerting/`:

- Extend: `docker/telemetry/grafana/provisioning/alerting/rules.yaml` (add groups
  alongside the existing `xrpld-consensus`, `xrpld-validator`, `xrpld-jobqueue`,
  `xrpld-node-state`, `xrpld-overlay`)
- Extend: `docker/telemetry/grafana/provisioning/alerting/contactpoints.yaml`
  (add Discord / PagerDuty receivers)
- Extend: `docker/telemetry/grafana/provisioning/alerting/policies.yaml`
  (add routes; the root route and the `severity = critical` child already exist)

**Exit Criteria**:

- [ ] The 14 not-yet-shipped rules evaluate without errors in Grafana alerting UI
- [ ] The 2 rules **fully** covered by Phase 9 (Unhealthy State, Job Queue
      Overflow) are not duplicated; the 2 **partially** covered ones (High IO
      Latency, Stale Ledger) are either re-baselined on the Phase 9 rule or shipped
      as tighter variants — decision recorded either way
- [ ] Critical rules fire within expected timeframe when conditions are met
- [ ] Notification channel templates are documented (not hard-coded to any service)
- [ ] `node_exporter` decision recorded for the 3 host-level rules (CPU, memory, disk)
- [x] `state_tracking` gauge implemented (Task 7.12) before adding "Not Proposing"
      — **prerequisite met upstream**, not Phase 11 work.
      `MetricsRegistry::registerStateTrackingGauge()`
      (`src/xrpld/telemetry/MetricsRegistry.cpp:1461-1510`) creates
      `CreateDoubleObservableGauge("state_tracking", "Node state and mode tracking")`
      at `:1466` and observes `state_value` (`:1497`) and
      `time_in_current_state_seconds` (`:1502`). Already queried by
      `validator-health.json:765,971` and `ledger-data-sync.json:869`, and
      documented in
      [09-data-collection-reference.md](./09-data-collection-reference.md)
      § State Tracking. "Not Proposing" can be written now.

---

## Task 11.13: Dual-Datasource Architecture Documentation

> **Source**: [External Dashboard Parity](./06-implementation-phases.md#appendix-external-dashboard-parity)

**Objective**: Document the external dashboard's "fast path" pattern as a future optimization for real-time panels.

**Pattern**: A lightweight Prometheus scrape endpoint (separate from OTLP pipeline) that polls critical metrics every 2-5s, bypassing the 10s OTLP metric reader interval and Prometheus scrape interval.

**Use case**: Real-time state panels (server state, ledger age, peer count) where 10-15s latency is too slow for operational dashboards.

**Decision**: Document as a future option, not implement now. The current 10s interval is acceptable for v1. The external dashboard achieves 2-5s freshness by polling RPC directly, which is what the Phase 11 receiver already does. Adding a separate scrape endpoint to xrpld would only be needed if sub-second metric freshness is required from the internal metrics pipeline.

**What to document**:

- Architecture comparison: OTLP pipeline (10-15s) vs. direct scrape (2-5s) vs. push gateway
- When to consider: operator feedback indicating 10s is insufficient for alerting SLOs
- How to implement if needed: add `/metrics` HTTP endpoint to xrpld with Prometheus client library
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
- [ ] 4 new Grafana dashboards operational with data, none reusing a Phase 9 uid
      (`validator-health`, `peer-quality`, `fee-market`, `job-queue`, `node-health`)
- [ ] Prometheus alerting rules fire correctly for simulated failure conditions
- [ ] DEX/AMM collector works when configured (optional — not required for base exit criteria)
- [ ] Phase 10 validation suite passes with receiver metrics included
- [ ] Receiver handles xrpld restart/unavailability gracefully (no crash, logs warning, retries)
- [ ] Documentation complete: receiver README, metric reference, alerting playbook
- [ ] Go receiver has unit tests with >80% coverage
- [ ] The 14 not-yet-shipped Grafana alert rules for Phase 7+ parity metrics
      evaluate correctly (Task 11.12); the other 4 of the 18 already ship in Phase 9
- [ ] Dual-datasource architecture documented with trade-offs (Task 11.13)
