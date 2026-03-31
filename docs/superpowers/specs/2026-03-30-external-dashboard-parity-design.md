# External Dashboard Parity — Design Spec

> **Date**: 2026-03-30
> **Status**: Draft
> **Source**: [realgrapedrop/xrpl-validator-dashboard](https://github.com/realgrapedrop/xrpl-validator-dashboard)
> **Jira Epic**: RIPD-5060

## Summary

Integrate 29 missing metrics, 18 alert rules, and enriched span attributes from the community `xrpl-validator-dashboard` into rippled's native OpenTelemetry instrumentation. Changes are distributed across phases 2, 3, 4, 6, 7, 9, 10, and 11 of the OTel PR chain.

## Gap Analysis

### Coverage Breakdown (86 external metrics)

| Status            | Count | Notes                                                          |
| ----------------- | ----- | -------------------------------------------------------------- |
| Already covered   | 30    | peer_count, load_factor, io_latency, uptime, overlay traffic   |
| Partially covered | 3     | state_value encoding, NuDB granularity, validation_quorum      |
| Missing           | 29    | Validation agreement, ledger economy, peer quality, UNL health |
| N/A (external)    | 24    | Monitor health, realtime duplicates, system metrics            |

### Missing Metrics by Category

| Category             | Metrics                                                                                                                                                                                                                            | Count |
| -------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----- |
| Validation Agreement | `validations_sent_total`, `validations_checked_total`, `validation_agreements_total`, `validation_missed_total`, `validation_agreement_pct_1h/24h`, `validation_agreements_1h/24h`, `validation_missed_1h/24h`, `validation_event` | 11    |
| Ledger Economy       | `ledgers_closed_total`, `ledger_age_seconds`, `base_fee_xrp`, `reserve_base_xrp`, `reserve_inc_xrp`, `transaction_rate`                                                                                                            | 6     |
| State Tracking       | `time_in_current_state_seconds`, `state_changes_total`, `validator_state_info`                                                                                                                                                     | 3     |
| Peer Quality         | `peers_insane`, `peer_latency_p90_ms`                                                                                                                                                                                              | 2     |
| Validator Health     | `amendment_blocked`, `unl_expiry_days`                                                                                                                                                                                             | 2     |
| Upgrade Awareness    | `peers_higher_version_pct`, `upgrade_recommended`                                                                                                                                                                                  | 2     |
| Storage / Other      | `ledger_nudb_bytes`, `jq_trans_overflow_total`, `initial_sync_duration_seconds`                                                                                                                                                    | 3     |

### Alert Rules (18 total, from external dashboard)

| Group       | Count | Rules                                                                                                                   |
| ----------- | ----- | ----------------------------------------------------------------------------------------------------------------------- |
| Critical    | 8     | Agreement <90%, not proposing, unhealthy state, amendment blocked, UNL expiring, IO latency, load factor, peer count <5 |
| Network     | 3     | Peer drop >10%/30%, P90 latency + disconnect correlation                                                                |
| Performance | 7     | CPU >80%, memory >90%, disk >85%, job queue overflow, upgrade recommended, tx rate drop, stale ledger                   |

---

## Branch-to-Change Mapping

### Phase 2 — `pratik/otel-phase2-rpc-tracing`

> **Ref**: Adds to existing Phase 2 task list. Consumed by Phase 7 (MetricsRegistry) and Phase 10 (validation checks).

**Task 2.8: RPC Span Attribute Enrichment**

Add node-level health context to every `rpc.command.*` span so operators can correlate RPC behavior with node state.

New span attributes on `rpc.command.*`:

| Attribute                     | Type   | Source                               | Value Example         |
| ----------------------------- | ------ | ------------------------------------ | --------------------- |
| `xrpl.node.amendment_blocked` | bool   | `app_.getOPs().isAmendmentBlocked()` | `true`                |
| `xrpl.node.server_state`      | string | `app_.getOPs().strOperatingMode()`   | `"full"`, `"syncing"` |

**File**: `src/xrpld/rpc/detail/RPCHandler.cpp` (in the `rpc.command.*` span creation block, after existing setAttribute calls)

**Rationale**: RPC is the operator's primary interaction point. When a node is amendment-blocked or degraded, every RPC response is suspect. Tagging spans with this state enables Tempo TraceQL queries like `{name=~"rpc.command.*" && span.xrpl.node.amendment_blocked = true}` to find all RPCs served during a blocked period.

**Exit Criteria**:

- [ ] `rpc.command.server_info` spans carry `xrpl.node.amendment_blocked` and `xrpl.node.server_state` attributes
- [ ] No measurable latency impact (attribute values are cached atomics, not computed per-call)

---

### Phase 3 — `pratik/otel-phase3-tx-tracing`

> **Ref**: Adds to existing Phase 3 task list. Consumed by Phase 10 (validation checks).

**Task 3.7: Transaction Span Peer Version Attribute**

Add the relaying peer's rippled version to transaction receive spans to enable version-mismatch correlation.

New span attribute on `tx.receive`:

| Attribute           | Type   | Source               | Value Example     |
| ------------------- | ------ | -------------------- | ----------------- |
| `xrpl.peer.version` | string | `peer->getVersion()` | `"rippled-2.4.0"` |

**File**: `src/xrpld/overlay/detail/PeerImp.cpp` (in the `tx.receive` span block, after existing `xrpl.peer.id` setAttribute)

**Rationale**: Transaction relay is where version mismatches cause subtle serialization or validation bugs. Tracing "this tx came from a v2.3.0 peer" helps diagnose compatibility issues during network upgrades.

**Exit Criteria**:

- [ ] `tx.receive` spans carry `xrpl.peer.version` attribute with a non-empty version string
- [ ] Attribute is omitted (not empty-string) when `getVersion()` returns empty

---

### Phase 4 — `pratik/otel-phase4-consensus-tracing`

> **Ref**: Adds to existing Phase 4 task list. Provides the span-level foundation that Phase 7 (ValidationTracker) builds upon. Consumed by Phase 10 (validation checks).

**Task 4.8: Consensus Validation Span Enrichment**

Add ledger hash and validation type to validation spans on both send and receive paths. This enables trace-level agreement analysis — filter by ledger hash to see which validators agreed.

New span attributes on `consensus.validation.send`:

| Attribute                     | Type   | Source                                  | Value Example               |
| ----------------------------- | ------ | --------------------------------------- | --------------------------- |
| `xrpl.validation.ledger_hash` | string | Ledger hash from `validate()` call args | `"A1B2C3..."` (64-char hex) |
| `xrpl.validation.full`        | bool   | Whether this is a full validation       | `true`                      |

New span attributes on `peer.validation.receive`:

| Attribute                          | Type   | Source                                | Value Example               |
| ---------------------------------- | ------ | ------------------------------------- | --------------------------- |
| `xrpl.peer.validation.ledger_hash` | string | From deserialized STValidation object | `"A1B2C3..."` (64-char hex) |
| `xrpl.peer.validation.full`        | bool   | From STValidation flags               | `true`                      |

New span attributes on `consensus.accept`:

| Attribute                            | Type  | Source                                   | Value Example |
| ------------------------------------ | ----- | ---------------------------------------- | ------------- |
| `xrpl.consensus.validation_quorum`   | int64 | `app_.validators().quorum()`             | `28`          |
| `xrpl.consensus.proposers_validated` | int64 | `result.proposers` from consensus result | `35`          |

**Files**:

- `src/xrpld/app/consensus/RCLConsensus.cpp` (validation.send and accept spans)
- `src/xrpld/overlay/detail/PeerImp.cpp` (peer.validation.receive span)

**Rationale**: The external dashboard's most valuable feature is validation agreement tracking. By recording the ledger hash on both outgoing and incoming validation spans, we create the raw data for agreement analysis at the trace level. Phase 7's ValidationTracker builds the metric-level aggregation on top of this.

**Exit Criteria**:

- [ ] `consensus.validation.send` spans carry `xrpl.validation.ledger_hash` and `xrpl.validation.full`
- [ ] `peer.validation.receive` spans carry `xrpl.peer.validation.ledger_hash` and `xrpl.peer.validation.full`
- [ ] `consensus.accept` spans carry `xrpl.consensus.validation_quorum` and `xrpl.consensus.proposers_validated`
- [ ] Ledger hash attributes match between send and receive for the same ledger

---

### Phase 6 — `pratik/otel-phase6-statsd`

> **Ref**: Adds to existing Phase 6 scope. No separate task list file exists for Phase 6 per project convention.

**Addition: Bridge `peerDisconnectsCharges_` metric**

The overlay already tracks resource-limit disconnects via `OverlayImpl::Stats::peerDisconnectsCharges_` (a `beast::insight::Gauge`). This metric is registered but not included in the StatsD bridge mapping.

**What to do**:

- Ensure `rippled_Overlay_Peer_Disconnects_Charges` appears in the StatsD-to-Prometheus metric name mapping
- Verify the metric appears in Prometheus after StatsD bridge is active

**File**: `src/xrpld/overlay/detail/OverlayImpl.cpp`

**Prometheus name**: `rippled_Overlay_Peer_Disconnects_Charges`

---

### Phase 7 — `pratik/otel-phase7-native-metrics`

> **Ref**: Adds to existing Phase 7 task list. This is the largest addition. Depends on Phase 4 span attributes for validation tracking context. Consumed by Phase 9 (dashboards), Phase 10 (validation), Phase 11 (alerts).

**Task 7.8: ValidationTracker — Validation Agreement Computation**

The most valuable missing component. A stateful class that tracks whether our validator's validations agree with network consensus, maintaining rolling 1h and 24h windows.

**Architecture**:

```

  consensus.validation.send ─────> ValidationTracker ──────> MetricsRegistry
  (records our validation          (reconciles after         (exports agreement
   for ledger X)                    8s grace period)          gauges every 10s)

  ledger.validate ───────────────> ValidationTracker
  (records which ledger            (marks ledger X as
   network validated)               agreed or missed)
```

**Design**:

```cpp
/// Tracks validation agreement between this node and network consensus.
///
///  ValidationTracker
///  ├── recordOurValidation(ledgerHash, ledgerSeq)  // called when we send
///  ├── recordNetworkValidation(ledgerHash, seq)     // called on ledger validate
///  ├── reconcile()                                  // called periodically (timer)
///  ├── agreementPct1h() -> double                   // 0.0-100.0
///  ├── agreementPct24h() -> double
///  ├── agreements1h() -> uint64_t
///  ├── missed1h() -> uint64_t
///  ├── agreements24h() -> uint64_t
///  ├── missed24h() -> uint64_t
///  ├── totalAgreements() -> uint64_t
///  ├── totalMissed() -> uint64_t
///  ├── totalValidationsSent() -> uint64_t
///  └── totalValidationsChecked() -> uint64_t        // all network validations seen
class ValidationTracker
{
    // Ring buffer of pending ledger events (max 1000)
    struct LedgerEvent {
        uint256 ledgerHash;
        LedgerIndex seq;
        TimePoint closeTime;
        bool weValidated = false;     // did we send a validation for this ledger?
        bool networkValidated = false; // did network validate this ledger?
        bool reconciled = false;       // has 8s grace period elapsed?
        bool agreed = false;           // after reconciliation: did we agree?
    };

    // Sliding window deques for pre-computed window stats
    struct WindowEvent {
        TimePoint time;
        bool agreed;
    };
    std::deque<WindowEvent> window1h_;  // events in last 1 hour
    std::deque<WindowEvent> window24h_; // events in last 24 hours

    // Reconciliation: 8s grace period after ledger close.
    // If our validation hasn't arrived by then, mark as missed.
    // 5-minute late repair: if a late validation arrives, correct the miss.
    static constexpr auto kGracePeriod = std::chrono::seconds(8);
    static constexpr auto kLateRepairWindow = std::chrono::minutes(5);
};
```

**Recording sites** (modifications to consensus code from Phase 7 branch):

| Hook Point                   | File                | What to Record                                                          |
| ---------------------------- | ------------------- | ----------------------------------------------------------------------- |
| `validate()` in `doAccept()` | RCLConsensus.cpp    | `tracker.recordOurValidation(ledgerHash, seq)`                          |
| `onValidation()` callback    | RCLValidations path | `tracker.recordNetworkValidation(...)` — increment `validationsChecked` |
| LedgerMaster fully-validated | LedgerMaster.cpp    | `tracker.recordNetworkValidation(validatedHash, seq)`                   |

**Key new files**:

- `src/xrpld/telemetry/ValidationTracker.h`
- `src/xrpld/telemetry/detail/ValidationTracker.cpp`

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.h` (add ValidationTracker member)
- `src/xrpld/telemetry/MetricsRegistry.cpp` (add gauge callback reading from tracker)
- `src/xrpld/app/consensus/RCLConsensus.cpp` (add recording hooks)
- `src/xrpld/app/ledger/detail/LedgerMaster.cpp` (add recording hook)

**Exit Criteria**:

- [ ] `ValidationTracker` correctly tracks agreement with 8s grace period
- [ ] 5-minute late repair corrects false-positive misses
- [ ] Thread-safe (atomics + mutex for window deques)
- [ ] Rolling windows correctly evict stale entries
- [ ] Unit tests for: normal agreement, missed validation, late repair, window eviction

---

**Task 7.9: Validator Health Observable Gauges**

New MetricsRegistry observable gauge for amendment, UNL, and quorum health.

| Gauge Name                 | Label `metric=`     | Type   | Source                                            |
| -------------------------- | ------------------- | ------ | ------------------------------------------------- |
| `rippled_validator_health` | `amendment_blocked` | int64  | `app_.getOPs().isAmendmentBlocked()` → 0/1        |
|                            | `unl_blocked`       | int64  | `app_.getOPs().isUNLBlocked()` → 0/1              |
|                            | `unl_expiry_days`   | double | `app_.validators().expires()` → days until expiry |
|                            | `validation_quorum` | int64  | `app_.validators().quorum()`                      |

**File**: `src/xrpld/telemetry/MetricsRegistry.cpp` (new gauge callback in `registerAsyncGauges()`)

**Exit Criteria**:

- [ ] All 4 label values emitted every 10s
- [ ] `unl_expiry_days` is negative when expired, positive when active
- [ ] Values visible in Prometheus

---

**Task 7.10: Peer Quality Observable Gauges**

New MetricsRegistry observable gauge for peer health aggregates.

| Gauge Name             | Label `metric=`            | Type   | Source                                     |
| ---------------------- | -------------------------- | ------ | ------------------------------------------ |
| `rippled_peer_quality` | `peer_latency_p90_ms`      | double | Iterate peers, compute P90 from `latency_` |
|                        | `peers_insane_count`       | int64  | Count peers with `tracking_ == diverged`   |
|                        | `peers_higher_version_pct` | double | Compare `getVersion()` to own version      |
|                        | `upgrade_recommended`      | int64  | 1 if `peers_higher_version_pct > 60%`      |

**Implementation note**: The callback iterates `app_.overlay().foreach(...)` to collect per-peer latency and version data. This runs every 10s on the metrics reader thread — acceptable overhead for ~50-200 peers.

**File**: `src/xrpld/telemetry/MetricsRegistry.cpp`

**Exit Criteria**:

- [ ] P90 latency computed correctly (sort peer latencies, pick 90th percentile)
- [ ] Insane count matches `peers` RPC output
- [ ] Version comparison handles format variations (e.g., "rippled-2.4.0-rc1")
- [ ] Values visible in Prometheus

---

**Task 7.11: Ledger Economy Observable Gauges**

New MetricsRegistry observable gauge for fee and ledger metrics.

| Gauge Name               | Label `metric=`      | Type   | Source                                    |
| ------------------------ | -------------------- | ------ | ----------------------------------------- |
| `rippled_ledger_economy` | `base_fee_xrp`       | double | `app_.getFeeTrack().getBaseFee()` → drops |
|                          | `reserve_base_xrp`   | double | From validated ledger fee settings        |
|                          | `reserve_inc_xrp`    | double | From validated ledger fee settings        |
|                          | `ledger_age_seconds` | double | `now - lastValidatedCloseTime`            |
|                          | `transaction_rate`   | double | Derived: tx count delta / time delta      |

**File**: `src/xrpld/telemetry/MetricsRegistry.cpp`

**Exit Criteria**:

- [ ] Fee values match `server_info` RPC output
- [ ] `ledger_age_seconds` increases monotonically between ledger closes, resets on close
- [ ] `transaction_rate` is smoothed (rolling average, not instantaneous)

---

**Task 7.12: State Tracking Observable Gauges**

New MetricsRegistry observable gauge for node state duration.

| Gauge Name               | Label `metric=`                 | Type   | Source                                           |
| ------------------------ | ------------------------------- | ------ | ------------------------------------------------ |
| `rippled_state_tracking` | `state_value`                   | int64  | 0-7 numeric encoding matching external dashboard |
|                          | `time_in_current_state_seconds` | double | `now - lastModeChangeTime`                       |

**State value encoding**:

rippled's `OperatingMode` enum maps 0-4 (DISCONNECTED through FULL). The external dashboard extends this to 0-6 by combining operating mode with consensus participation:

| Value | State        | Source                                                      |
| ----- | ------------ | ----------------------------------------------------------- |
| 0     | disconnected | `OperatingMode::DISCONNECTED`                               |
| 1     | connected    | `OperatingMode::CONNECTED`                                  |
| 2     | syncing      | `OperatingMode::SYNCING`                                    |
| 3     | tracking     | `OperatingMode::TRACKING`                                   |
| 4     | full         | `OperatingMode::FULL` and not validating                    |
| 5     | validating   | `OperatingMode::FULL` and `mConsensus.validating()` is true |
| 6     | proposing    | `OperatingMode::FULL` and consensus mode is `proposing`     |

**Note**: Values 5-6 require checking both `OperatingMode` and `ConsensusMode`. The callback should derive these from `app_.getOPs().getOperatingMode()` combined with `mConsensus.mode()`. If operating mode is FULL and consensus is proposing → 6; if FULL and validating → 5; otherwise use the raw OperatingMode enum value.

**File**: `src/xrpld/telemetry/MetricsRegistry.cpp`

**Exit Criteria**:

- [ ] `state_value` matches external dashboard encoding
- [ ] `time_in_current_state_seconds` resets on mode change

---

**Task 7.13: Storage Detail Observable Gauge**

| Gauge Name               | Label `metric=` | Type  | Source                                   |
| ------------------------ | --------------- | ----- | ---------------------------------------- |
| `rippled_storage_detail` | `nudb_bytes`    | int64 | NuDB backend file size (filesystem stat) |

**File**: `src/xrpld/telemetry/MetricsRegistry.cpp`

**Exit Criteria**:

- [ ] NuDB file size reported in bytes
- [ ] Gracefully returns 0 if NuDB not configured

---

**Task 7.14: New Synchronous Counters**

New counters incremented at event sites. Declared in MetricsRegistry, recording sites added in consensus/overlay/network code.

| Counter Name                          | Increment Site                   | Source File           |
| ------------------------------------- | -------------------------------- | --------------------- |
| `rippled_ledgers_closed_total`        | `onAccept()` in consensus        | RCLConsensus.cpp      |
| `rippled_validations_sent_total`      | `validate()` in consensus        | RCLConsensus.cpp      |
| `rippled_validations_checked_total`   | Network validation received      | LedgerMaster.cpp      |
| `rippled_validation_agreements_total` | ValidationTracker reconciliation | ValidationTracker.cpp |
| `rippled_validation_missed_total`     | ValidationTracker reconciliation | ValidationTracker.cpp |
| `rippled_state_changes_total`         | `setMode()` in NetworkOPs        | NetworkOPs.cpp        |
| `rippled_jq_trans_overflow_total`     | Job queue overflow path          | JobQueue.cpp          |

**Key modified files**:

- `src/xrpld/telemetry/MetricsRegistry.h/.cpp` (counter declarations)
- `src/xrpld/app/consensus/RCLConsensus.cpp` (recording: ledgers_closed, validations_sent)
- `src/xrpld/app/ledger/detail/LedgerMaster.cpp` (recording: validations_checked)
- `src/xrpld/app/misc/NetworkOPs.cpp` (recording: state_changes)

**Exit Criteria**:

- [ ] All 7 counters monotonically increase during normal operation
- [ ] Counter values match expected rates (e.g., ledgers_closed ≈ 1 per 3-5s)
- [ ] Values visible in Prometheus

---

**Task 7.15: Validation Agreement Observable Gauge**

Reads from the `ValidationTracker` (Task 7.8) to export rolling window stats.

| Gauge Name                     | Label `metric=`     | Type   | Source                      |
| ------------------------------ | ------------------- | ------ | --------------------------- |
| `rippled_validation_agreement` | `agreement_pct_1h`  | double | `tracker.agreementPct1h()`  |
|                                | `agreements_1h`     | int64  | `tracker.agreements1h()`    |
|                                | `missed_1h`         | int64  | `tracker.missed1h()`        |
|                                | `agreement_pct_24h` | double | `tracker.agreementPct24h()` |
|                                | `agreements_24h`    | int64  | `tracker.agreements24h()`   |
|                                | `missed_24h`        | int64  | `tracker.missed24h()`       |

**File**: `src/xrpld/telemetry/MetricsRegistry.cpp`

**Exit Criteria**:

- [ ] Agreement percentages in range [0.0, 100.0]
- [ ] Window stats match manual count from validation counters
- [ ] Percentages stabilize after 1h/24h of operation

---

### Phase 9 — `pratik/otel-phase9-metric-gap-fill`

> **Ref**: Adds to existing Phase 9 task list. Depends on Phase 7 gauges/counters. Consumed by Phase 10 (dashboard load checks).

**Task 9.11: Validator Health Dashboard**

New Grafana dashboard: `rippled-validator-health.json`

| Panel                      | Type       | PromQL                                                           |
| -------------------------- | ---------- | ---------------------------------------------------------------- |
| Agreement % (1h)           | stat       | `rippled_validation_agreement{metric="agreement_pct_1h"}`        |
| Agreement % (24h)          | stat       | `rippled_validation_agreement{metric="agreement_pct_24h"}`       |
| Agreements vs Missed (1h)  | bargauge   | `agreements_1h` and `missed_1h` side by side                     |
| Agreements vs Missed (24h) | bargauge   | `agreements_24h` and `missed_24h` side by side                   |
| Validation Rate            | stat       | `rate(rippled_validations_sent_total[5m]) * 60`                  |
| Validations Checked Rate   | stat       | `rate(rippled_validations_checked_total[5m]) * 60`               |
| Amendment Blocked          | stat       | `rippled_validator_health{metric="amendment_blocked"}`           |
| UNL Expiry (days)          | stat       | `rippled_validator_health{metric="unl_expiry_days"}`             |
| Validation Quorum          | stat       | `rippled_validator_health{metric="validation_quorum"}`           |
| State Value Timeline       | timeseries | `rippled_state_tracking{metric="state_value"}`                   |
| Time in Current State      | stat       | `rippled_state_tracking{metric="time_in_current_state_seconds"}` |
| State Changes Rate         | stat       | `rate(rippled_state_changes_total[1h])`                          |
| Ledgers Closed Rate        | stat       | `rate(rippled_ledgers_closed_total[5m]) * 60`                    |

**Dashboard conventions**: `$node` template variable for `exported_instance` filtering, dark theme, matching existing panel sizes and color schemes.

---

**Task 9.12: Peer Quality Dashboard**

New Grafana dashboard: `rippled-peer-quality.json`

| Panel                  | Type       | PromQL                                                           |
| ---------------------- | ---------- | ---------------------------------------------------------------- |
| P90 Peer Latency       | timeseries | `rippled_peer_quality{metric="peer_latency_p90_ms"}`             |
| Insane/Diverged Peers  | stat       | `rippled_peer_quality{metric="peers_insane_count"}`              |
| Higher Version Peers % | stat       | `rippled_peer_quality{metric="peers_higher_version_pct"}`        |
| Upgrade Recommended    | stat       | `rippled_peer_quality{metric="upgrade_recommended"}`             |
| Resource Disconnects   | timeseries | `rippled_Overlay_Peer_Disconnects_Charges`                       |
| Inbound vs Outbound    | bargauge   | `rippled_Peer_Finder_Active_Inbound_Peers`, `..._Outbound_Peers` |

---

**Task 9.13: Ledger Economy Dashboard Panels**

Add a "Ledger Economy" row to the existing `system-node-health.json` dashboard:

| Panel                | Type       | PromQL                                                |
| -------------------- | ---------- | ----------------------------------------------------- |
| Base Fee (drops)     | stat       | `rippled_ledger_economy{metric="base_fee_xrp"}`       |
| Reserve Base (drops) | stat       | `rippled_ledger_economy{metric="reserve_base_xrp"}`   |
| Reserve Inc (drops)  | stat       | `rippled_ledger_economy{metric="reserve_inc_xrp"}`    |
| Ledger Age           | stat       | `rippled_ledger_economy{metric="ledger_age_seconds"}` |
| Transaction Rate     | timeseries | `rippled_ledger_economy{metric="transaction_rate"}`   |

---

### Phase 10 — `pratik/otel-phase10-workload-validation`

> **Ref**: Adds to existing Phase 10 task list. Validates all additions from Phases 2-9.

**Task 10.6: External Dashboard Parity Validation Checks**

Add checks to `validate_telemetry.py` for all new span attributes and metrics.

**New span attribute checks (~8)**:

| Span Name                   | New Attribute                        |
| --------------------------- | ------------------------------------ |
| `rpc.command.server_info`   | `xrpl.node.amendment_blocked`        |
| `rpc.command.server_info`   | `xrpl.node.server_state`             |
| `tx.receive`                | `xrpl.peer.version`                  |
| `consensus.validation.send` | `xrpl.validation.ledger_hash`        |
| `consensus.validation.send` | `xrpl.validation.full`               |
| `peer.validation.receive`   | `xrpl.peer.validation.ledger_hash`   |
| `consensus.accept`          | `xrpl.consensus.validation_quorum`   |
| `consensus.accept`          | `xrpl.consensus.proposers_validated` |

**New metric existence checks (~13)**:

| Metric Name                                                |
| ---------------------------------------------------------- |
| `rippled_validation_agreement{metric="agreement_pct_1h"}`  |
| `rippled_validation_agreement{metric="agreement_pct_24h"}` |
| `rippled_validator_health{metric="amendment_blocked"}`     |
| `rippled_validator_health{metric="unl_expiry_days"}`       |
| `rippled_peer_quality{metric="peer_latency_p90_ms"}`       |
| `rippled_peer_quality{metric="peers_insane_count"}`        |
| `rippled_ledger_economy{metric="base_fee_xrp"}`            |
| `rippled_ledger_economy{metric="transaction_rate"}`        |
| `rippled_state_tracking{metric="state_value"}`             |
| `rippled_ledgers_closed_total`                             |
| `rippled_validations_sent_total`                           |
| `rippled_state_changes_total`                              |
| `rippled_storage_detail{metric="nudb_bytes"}`              |

**New dashboard load checks (~3)**:

| Dashboard                      |
| ------------------------------ |
| `rippled-validator-health`     |
| `rippled-peer-quality`         |
| `system-node-health` (updated) |

**New metric value sanity checks (~4)**:

| Check                         | Condition         |
| ----------------------------- | ----------------- |
| `validation_agreement_pct_1h` | in [0, 100]       |
| `unl_expiry_days`             | > 0 (not expired) |
| `peer_latency_p90_ms`         | > 0 (peers exist) |
| `state_value`                 | in [0, 7]         |

**Total new checks: ~28** (bringing total from 73 to ~101)

---

### Phase 11 — (future branch)

> **Ref**: Adds to existing Phase 11 task list. Depends on Phase 7 metrics and Phase 9 dashboards.

**Task 11.9: Alert Rules from External Dashboard**

Port 18 alert rules from the external `xrpl-validator-dashboard` to Grafana alerting provisioning.

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
| CPU High            | Per-core CPU > 80%                                             | 2m  |
| Memory Critical     | Memory usage > 90%                                             | 1m  |
| Disk Warning        | Disk usage > 85%                                               | 2m  |
| Job Queue Overflow  | `rate(rippled_jq_trans_overflow_total[5m]) > 0`                | 1m  |
| Upgrade Recommended | `rippled_peer_quality{metric="peers_higher_version_pct"} > 60` | 1m  |
| TX Rate Drop        | Transaction rate dropped > 50% in 5m window                    | 5m  |
| Stale Ledger        | `rippled_ledger_economy{metric="ledger_age_seconds"} > 30`     | 1m  |

**Notification channels**: Template configs for Email/SMTP, Discord, Slack, PagerDuty.

**Files**:

- `docker/telemetry/grafana/alerting/alert-rules.yaml` (new or extend existing)
- `docker/telemetry/grafana/alerting/contact-points.yaml`
- `docker/telemetry/grafana/alerting/notification-policies.yaml`

---

**Task 11.10: Dual-Datasource Architecture Documentation**

Document the external dashboard's "fast path" pattern as a future optimization for real-time panels:

- **Pattern**: A lightweight Prometheus scrape endpoint (separate from OTLP pipeline) that polls critical metrics every 2-5s, bypassing the 10s OTLP metric reader interval and Prometheus scrape interval.
- **Use case**: Real-time state panels (server state, ledger age, peer count) where 10-15s latency is too slow.
- **Decision**: Document as a future option, not implement now. Current 10s interval is acceptable for v1.

**File**: `OpenTelemetryPlan/Phase11_taskList.md` (documentation task, no code)

---

## Documentation Updates

### `docs/telemetry-runbook.md` (on Phase 9 branch)

Add new sections after "Phase 9: OTel Metrics Alerting Rules":

1. **Validator Health Monitoring** — explains agreement tracking, amendment blocked, UNL expiry, with example PromQL queries
2. **Peer Quality Monitoring** — explains P90 latency, insane peers, version awareness
3. **Ledger Economy Monitoring** — explains fee/reserve gauges, transaction rate, ledger age
4. **Validation Agreement Explained** — operator-facing explanation of the reconciliation algorithm (8s grace, 5m late repair), what "missed" means, and when to worry

### `OpenTelemetryPlan/09-data-collection-reference.md` (on Phase 9 branch)

Add new metric tables in a "Phase 7+: External Dashboard Parity" section covering all 29 new metrics with their gauge names, label values, types, and sources.

---

## Cross-Phase Dependency Chain

```
Phase 2 (span attrs: amendment_blocked, server_state)
Phase 3 (span attrs: peer.version)
Phase 4 (span attrs: validation.ledger_hash, validation.full, quorum)
Phase 6 (StatsD bridge: peerDisconnectsCharges)
    │
    ├── all above rebase into ──>
    │
Phase 7 (ValidationTracker + 7 gauges + 7 counters + agreement gauge)
    │
Phase 9 (3 dashboards + ledger economy panels + runbook + data-collection-ref)
    │
Phase 10 (28 new validation checks in validate_telemetry.py)
    │
Phase 11 (18 alert rules + dual-datasource docs)
```

## Rebase Strategy

After committing changes to each branch (starting from Phase 2):

1. Commit on `pratik/otel-phase2-rpc-tracing`
2. Rebase `phase3` onto `phase2`, resolve conflicts (task list files only — low risk)
3. Commit on `phase3`, rebase `phase4` onto `phase3`
4. Continue through chain: 4 → 5 → 5b → 6 → 7 → 8 → 9 → 10
5. Force-push-with-lease all affected branches

Since these are documentation-only changes (task list .md files), merge conflicts should be minimal — each file is unique to its branch.
