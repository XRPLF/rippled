#pragma once

/**
 * Compile-time OTel metric name constants for the sync-diagnostics signals.
 *
 *  The metric-side counterpart of the `*SpanNames.h` headers: one constant per
 *  emitted string, so a rename is one edit and a typo is a compile error rather
 *  than a metric that silently never appears. Each instrument name, label key
 *  and bounded label value added by the sync-diagnostics work is declared here
 *  exactly once and referenced from every C++ user of it -- the emit site, the
 *  gauge registration in MetricsRegistry.cpp, and the unit test.
 *
 *  Layer map -- who references these constants:
 *
 *      +-------------------------------------------------------------+
 *      |            MetricNames.h  (this file, L1-metrics)           |
 *      |   namespace metric  namespace label  namespace lval         |
 *      +-------------------------------------------------------------+
 *          ^                     ^                       ^
 *          |                     |                       |
 *   +--------------+   +-------------------+   +---------------------+
 *   | emit sites   |   | MetricsRegistry   |   | unit test           |
 *   | XRPL_METRIC_ |   | Create*Gauge +    |   | tests/libxrpl/      |
 *   | macros under |   | AddCallback       |   |  telemetry/         |
 *   | src/xrpld/   |   | observe(...)      |   |  MetricMacros.cpp   |
 *   +--------------+   +-------------------+   +---------------------+
 *
 *  Layers that CANNOT reference a C++ constant -- the collector config, the
 *  dashboard PromQL, `expected_metrics.json` and the runbook -- are held to
 *  these same strings by `.github/scripts/otel-naming/check_otel_naming.py`
 *  instead.
 *
 *  Where this is documented:
 *  - CONTRIBUTING.md -> "Telemetry metric naming" is the authoritative rule
 *    list, alongside the sibling span-attribute convention.
 *  - `.github/scripts/otel-naming/README.md` describes the enforcing rules
 *    I (no literals), J (suffix conventions) and K (expected_metrics.json).
 *  - docs/telemetry-runbook.md -> "Adding a New Metric" is the walkthrough for
 *    adding one, and shows the declare-then-emit pattern.
 *
 *  Naming rules (enforced by the checker's Rule J):
 *  - Bare `lower_snake_case`. No `xrpld_` prefix in code: the Prometheus
 *    exporter adds the namespace prefix itself, so writing it here would
 *    produce `xrpld_xrpld_*` on the wire.
 *  - A monotonic counter ends in `_total`, so `rate()` over it reads correctly
 *    and a reader can tell it from a gauge at a glance.
 *  - A duration carries its unit as the suffix: `_us`, `_ms` or `_seconds`.
 *    The unit belongs in the name because the OTel `unit` argument is not
 *    surfaced on the Prometheus metric name.
 *  - A gauge that is a snapshot of current state takes no suffix
 *    (`jobq_saturation`, `sync_state`).
 *  - Label VALUES are declared here only when they come from a fixed set that
 *    the code itself writes (`namespace lval`), which is what keeps series
 *    cardinality bounded. A value derived from runtime data -- a site URI, a
 *    peer address, a ledger hash -- is deliberately NOT declared here and must
 *    never become a label on a metric.
 *
 *  Why `constexpr char[]` and not the `makeStr`/`StaticStr` DSL that
 *  `SpanNames.h` uses: the OTel C++ API takes `nostd::string_view`, which in
 *  this build (`OPENTELEMETRY_STL_VERSION` unset, so the back-ported class is
 *  used) constructs only from `char const*`, `std::string` or
 *  `(char const*, size)`. It has NO constructor from `std::string_view`, so
 *  neither `StaticStr` (which converts to `std::string_view`) nor a
 *  `constexpr std::string_view` compiles in an instrument-name or label-key
 *  position -- both were tried and both fail with "no viable conversion".
 *  A `constexpr char[]` decays to `char const*` and binds directly. This also
 *  matches the precedent already in MetricsRegistry.cpp
 *  (`kJobQueuedDurationUs`), which this header absorbs.
 *
 * Example usage -- a labelled counter at an emit site:
 * @code
 * XRPL_METRIC_COUNTER_INC_LABELED(
 *     app_,
 *     metric::dnsResolveTotal,
 *     "Peer hostname resolutions, by outcome",
 *     {{label::outcome,
 *       std::string(
 *           resolved ? lval::dns_resolve::resolved : lval::dns_resolve::empty)}});
 * @endcode
 *
 * Example usage -- an observable gauge and its sub-metric discriminators:
 * @code
 * syncStateGauge_ = meter_->CreateInt64ObservableGauge(
 *     metric::syncState, "Sync-pipeline health signals");
 * // ... inside the callback:
 * observe(lval::sync_state::ledgersBehind, ops.getLedgersBehindNetwork());
 * @endcode
 *
 * Example usage -- edge case: a value that must NOT be a constant. The site
 * URI is runtime data, so only the KEY is named here; declaring the value
 * would imply a bounded set that does not exist:
 * @code
 * XRPL_METRIC_COUNTER_INC_LABELED(
 *     app_, metric::unlFetchTotal, "...",
 *     {{label::site, std::string(sites_[siteIdx].loadedResource->uri)},
 *      {label::outcome, std::string(outcome)}});
 * @endcode
 *
 * @note Header-only and dependency-free: nothing here includes an OTel or an
 *       xrpld header, so `src/tests/libxrpl/telemetry/MetricMacros.cpp` can
 *       include it even though `xrpl_tests` links only `xrpl.libxrpl`. The
 *       constants are `inline constexpr`, so they contribute no symbol to link
 *       against.
 * @note Not guarded by `XRPL_ENABLE_TELEMETRY`, for the same reason
 *       `SpanNames.h` is not: call sites name these constants even where the
 *       macros expand to no-ops, and the compiler elides any constant whose
 *       only uses are in dead code.
 * @note Every constant is a compile-time value with no mutable state, so all
 *       of them are safe to read concurrently from any thread.
 */

namespace xrpl::telemetry {

/**
 * Instrument names -- the metric name as it reaches the OTel meter.
 *
 * Grouped by the subsystem that emits them, matching how the sync-diagnostics
 * work was staged. A name is declared here whether it is created lazily by an
 * `XRPL_METRIC_*` macro at a call site or eagerly by a `meter_->Create*` call
 * in MetricsRegistry.cpp, because the dashboards cannot tell the two apart.
 */
namespace metric {

// ===== Bootstrap: getting a fresh node its first peers and its first UNL =====

/**
 * Time to resolve one configured peer hostname.
 */
inline constexpr char dnsResolveLatencyMs[] = "dns_resolve_latency_ms";
/**
 * Peer hostname resolutions, split by whether any address came back.
 */
inline constexpr char dnsResolveTotal[] = "dns_resolve_total";
/**
 * Time from starting an outbound dial to its terminal outcome.
 */
inline constexpr char overlayDialLatencyMs[] = "overlay_dial_latency_ms";
/**
 * Outbound peer connection attempts, by terminal outcome.
 */
inline constexpr char overlayConnectTotal[] = "overlay_connect_total";
/**
 * Peer handshakes this node rejected, by reason.
 */
inline constexpr char handshakeNegotiationFailTotal[] = "handshake_negotiation_fail_total";
/**
 * Validator-list fetch attempts, by site and outcome.
 */
inline constexpr char unlFetchTotal[] = "unl_fetch_total";
/**
 * Trusted UNL key count against the required quorum.
 *
 * A gauge, not a counter: both series are current state, and the useful read
 * is the difference between them.
 */
inline constexpr char unlQuorum[] = "unl_quorum";
/**
 * Network close-time offset from the local clock.
 */
inline constexpr char clockCloseOffsetSeconds[] = "clock_close_offset_seconds";

// ===== Sync state: why this node is not FULL yet =============================

/**
 * Operating-mode transitions, labelled with the mode pair.
 */
inline constexpr char stateChangesTotal[] = "state_changes_total";
/**
 * Four sync-pipeline health signals, split by the `metric` label.
 */
inline constexpr char syncState[] = "sync_state";
/**
 * Main-loop stall episodes. Cumulative, so `rate()` gives episodes/sec.
 */
inline constexpr char serverStallEventsTotal[] = "server_stall_events_total";

// ===== Acquire / SHAMap: is ledger data actually arriving? ===================

/**
 * Aggregate ledger-acquire progress across all in-flight acquires.
 */
inline constexpr char syncAcquire[] = "sync_acquire";
/**
 * SHAMap tree-node cache hit rate, 0.0-1.0.
 */
inline constexpr char shamapCacheHitRate[] = "shamap_cache_hit_rate";
/**
 * Ledger acquires by where the data came from (local store vs network).
 */
inline constexpr char syncAcquireSourceTotal[] = "sync_acquire_source_total";
/**
 * Acquire timeouts where not one new node arrived.
 */
inline constexpr char syncAcquireNoProgressTotal[] = "sync_acquire_no_progress_total";
/**
 * SHAMap nodes received during an acquire, by per-node outcome.
 */
inline constexpr char syncAddnodeTotal[] = "sync_addnode_total";

// ===== JobQueue: is the worker pool the bottleneck? ==========================

/**
 * Worker-pool saturation: tasks in flight, threads, and jobs queued.
 */
inline constexpr char jobqSaturation[] = "jobq_saturation";

// ===== Quorum and publish: can this node accept and publish a ledger? =======

/**
 * Pre-accept quorum gate and publish lag, split by the `metric` label.
 */
inline constexpr char ledgerQuorumPublish[] = "ledger_quorum_publish";
/**
 * Pre-accept gate rejections for being below quorum.
 */
inline constexpr char ledgerQuorumShortfallTotal[] = "ledger_quorum_shortfall_total";

// ===== Back-fill persistence: is history repair making progress? =============

/**
 * Replay sub-acquires that fell back to a full ledger acquire, by stage.
 */
inline constexpr char ledgerReplayFallbackTotal[] = "ledger_replay_fallback_total";
/**
 * Ledger replay tasks by terminal outcome.
 */
inline constexpr char ledgerReplayOutcomeTotal[] = "ledger_replay_outcome_total";
/**
 * Forced jumps of the last closed ledger to a divergent chain.
 */
inline constexpr char ledgerJumpTotal[] = "ledger_jump_total";

// ===== Peer supply: what this node's peers can and will serve ===============

/**
 * Peer coverage of the ledger sequence range this node still needs.
 */
inline constexpr char peerLedgerSupply[] = "peer_ledger_supply";
/**
 * Inbound peer connection attempts, by terminal outcome.
 */
inline constexpr char peerAcceptTotal[] = "peer_accept_total";
/**
 * Peer disconnects, by cause and connection direction.
 */
inline constexpr char peerDisconnectTotal[] = "peer_disconnect_total";
/**
 * Peer data requests this node declined to serve, by kind and cause.
 */
inline constexpr char serveRefusedTotal[] = "serve_refused_total";
/**
 * PeerFinder slots, dials in flight, and address-cache sizes.
 */
inline constexpr char peerfinderSlotCensus[] = "peerfinder_slot_census";
/**
 * Amendment-block warning and the countdown to this node ceasing to validate.
 */
inline constexpr char amendmentBlock[] = "amendment_block";
/**
 * NodeStore mean store/fetch latency, with the operation counts.
 */
inline constexpr char nodestoreLatency[] = "nodestore_latency";

// ===== Consensus =============================================================

/**
 * Wall-clock duration of a completed consensus round.
 */
inline constexpr char consensusRoundDurationMs[] = "consensus_round_duration_ms";

// ===== Sweep: what the periodic cache sweep costs ============================
//
// The sweep runs every `SizedItem::SweepInterval` seconds (10 s on a tiny node
// through 120 s on a huge one), so these three emit sites are cold: one
// histogram Record and two counter Adds per sweep is free at that cadence.

/**
 * Wall-clock duration of the `malloc_trim` call that ends every cache sweep.
 *
 * The cost of returning free heap pages to the kernel scales with the resident
 * heap, so this is the signal that a node with a large existing database pays a
 * per-sweep penalty a fresh node does not.
 */
inline constexpr char sweepMallocTrimUs[] = "sweep_malloc_trim_us";
/**
 * Minor page faults taken *inside* the `malloc_trim` call.
 *
 * Cumulative, so `rate()` gives faults/sec. Scoped to the trim call only -- see
 * the limitation noted on the runbook branch: this proves the trim itself
 * faults, not that the trim causes later faults as the caches refill.
 */
inline constexpr char sweepMallocTrimMinorFaultsTotal[] = "sweep_malloc_trim_minor_faults_total";
/**
 * Resident kilobytes the trim actually returned to the kernel.
 *
 * Cumulative and clamped at zero per sweep: a trim that reclaimed nothing, or
 * during which another thread grew the heap faster than the trim shrank it,
 * contributes 0 rather than a negative amount.
 */
inline constexpr char sweepMallocTrimReclaimedKbTotal[] = "sweep_malloc_trim_reclaimed_kb_total";

// ===== Rotation: the extra writes an online_delete rotation performs =========

/**
 * Nodes re-stored by `copyNode` because they were missing from both backends.
 *
 * The genuinely unmeasured extra write of a rotation: a clean node reachable
 * from the validated state map whose only on-disk copy lived in a backend an
 * earlier rotation removed. Was warn-log-only.
 */
inline constexpr char rotationCopyNodeRestoreTotal[] = "rotation_copy_node_restore_total";
/**
 * Rotation state: whether one is running, and the copy-forward write total.
 *
 * A gauge, not a counter, because the two readings are polled from the node
 * store rather than pushed: `in_flight` is current state and `copy_forward` is a
 * cumulative total the nodestore already keeps. Observed from the existing
 * `registerNodeStoreGauge` callback, which is how everything else reads the node
 * store from xrpld without libxrpl having to know about telemetry.
 */
inline constexpr char rotationState[] = "rotation_state";

// ===== Pre-existing instruments pulled in by the family ratchet ==============
//
// These predate the sync-diagnostics work. They are declared here because the
// checker's Rule I enforces literal-freedom per metric FAMILY (first
// underscore segment), and each of these shares a family with a name above --
// `ledger_`, `nodestore_`, `server_`, `peer_`, `state_`. Leaving them as
// literals would either weaken the rule to per-name (letting a typo'd sibling
// through) or require an exemption list. Declaring them is the honest option:
// no behaviour changes, and the next author editing these families finds the
// constant rather than inventing a second spelling.
//
// The remaining unconverted families are reported as Rule L warnings, so the
// outstanding work stays visible rather than silently accepted.

/**
 * Built-vs-validated ledger mismatches, by reason.
 */
inline constexpr char ledgerHistoryMismatchTotal[] = "ledger_history_mismatch_total";
/**
 * Ledger fee and economy readings.
 */
inline constexpr char ledgerEconomy[] = "ledger_economy";
/**
 * NodeStore I/O counters, queue depth and write load.
 */
inline constexpr char nodestoreState[] = "nodestore_state";
/**
 * Server-level health readings.
 */
inline constexpr char serverInfo[] = "server_info";
/**
 * Peer-network quality readings.
 */
inline constexpr char peerQuality[] = "peer_quality";
/**
 * Node state and operating-mode tracking.
 */
inline constexpr char stateTracking[] = "state_tracking";

}  // namespace metric

/**
 * Label keys -- the dimension names attached to a metric datapoint.
 *
 * Every key here is bounded by design: the values it can take are either a
 * fixed set declared in `namespace lval` below, or a small enumeration the
 * code derives (an operating mode, a job type). A key whose values are
 * unbounded runtime data would mint one time series per distinct value, so no
 * such key is declared.
 */
namespace label {

/**
 * Sub-metric discriminator on a multi-series gauge.
 *
 * The pattern every observable gauge in MetricsRegistry.cpp already uses: one
 * instrument carries several related readings, told apart by this label rather
 * than by being separate instruments. Pre-dates the sync-diagnostics work;
 * named here because the new gauges are its heaviest users.
 */
inline constexpr char metric[] = "metric";
/**
 * Job type, as produced by `JobTypes::name()`.
 */
inline constexpr char jobType[] = "job_type";
/**
 * Which producer submitted a job, within its job type.
 *
 * A job type has several producers (`RcvGetLedger` and `RcvGetObjByHash` both
 * run as `JtLedgerReq`), so this is what attributes a latency spike to one of
 * them. Bounded by `MetricsRegistry::sanitiseHandler()`, which folds any job
 * name that is not all ASCII letters -- the ones embedding a ledger sequence
 * -- down to a single `other` value.
 */
inline constexpr char handler[] = "handler";
/**
 * Terminal result of a bounded operation.
 */
inline constexpr char outcome[] = "outcome";
/**
 * Cause of a rejection, refusal or teardown.
 */
inline constexpr char reason[] = "reason";
/**
 * Configured validator-list site URI. The one runtime-valued key here.
 */
inline constexpr char site[] = "site";
/**
 * Operating mode a transition started from.
 */
inline constexpr char from[] = "from";
/**
 * Operating mode a transition ended at.
 */
inline constexpr char to[] = "to";
/**
 * Where acquired ledger data came from.
 */
inline constexpr char source[] = "source";
/**
 * Which stage of a multi-step pipeline the event belongs to.
 */
inline constexpr char stage[] = "stage";
/**
 * Connection direction, inbound or outbound.
 */
inline constexpr char direction[] = "direction";
/**
 * Which kind of peer data request is being described.
 */
inline constexpr char request[] = "request";

}  // namespace label

/**
 * Bounded label values -- the fixed value sets the code itself writes.
 *
 * Nested by the instrument (or the gauge) that owns the set, because the same
 * word means different things in different sets and a flat namespace would let
 * two of them collide. A value is declared here only when the code chooses it
 * from a fixed list; anything derived from runtime data stays out.
 */
namespace lval {

// ===== Shared outcome/direction slugs =======================================

/**
 * Values shared by more than one instrument. Declared once so two instruments
 * that mean the same thing cannot spell it differently.
 */
inline constexpr char timeout[] = "timeout";
inline constexpr char notFound[] = "not_found";
inline constexpr char inbound[] = "inbound";
inline constexpr char outbound[] = "outbound";

/**
 * `dns_resolve_total` outcomes: did the resolver return any address?
 */
namespace dns_resolve {
inline constexpr char resolved[] = "resolved";
inline constexpr char empty[] = "empty";
}  // namespace dns_resolve

/**
 * `peer_accept_total` outcomes -- the nine exits of the inbound-accept path.
 *
 * Every exit records one of these, so the counter's total equals the number of
 * inbound attempts and an unexplained gap is impossible.
 */
namespace peer_accept {
inline constexpr char localEndpointFail[] = "local_endpoint_fail";
inline constexpr char resourceLimit[] = "resource_limit";
inline constexpr char noSlot[] = "no_slot";
inline constexpr char notPeerRequest[] = "not_peer_request";
inline constexpr char protocolMismatch[] = "protocol_mismatch";
inline constexpr char badCookie[] = "bad_cookie";
inline constexpr char slotRefused[] = "slot_refused";
inline constexpr char accepted[] = "accepted";
inline constexpr char handshakeError[] = "handshake_error";
}  // namespace peer_accept

/**
 * `handshake_negotiation_fail_total` reasons -- one per rejection point in
 * the handshake verifier.
 *
 * These separate a peer misconfiguration this node should tolerate
 * (`wrong_network`, `self_connection`) from a local misconfiguration an
 * operator must fix (`clock_skew`, `local_ip_mismatch`), which is the whole
 * point of splitting the counter by reason.
 */
namespace handshake_fail {
inline constexpr char invalidServerDomain[] = "invalid_server_domain";
inline constexpr char invalidNetworkId[] = "invalid_network_id";
inline constexpr char wrongNetwork[] = "wrong_network";
inline constexpr char invalidClockTimestamp[] = "invalid_clock_timestamp";
inline constexpr char clockSkew[] = "clock_skew";
inline constexpr char unsupportedKeyType[] = "unsupported_key_type";
inline constexpr char badPublicKey[] = "bad_public_key";
inline constexpr char noSessionSignature[] = "no_session_signature";
inline constexpr char sessionVerifyFailed[] = "session_verify_failed";
inline constexpr char selfConnection[] = "self_connection";
inline constexpr char invalidLocalIp[] = "invalid_local_ip";
inline constexpr char localIpMismatch[] = "local_ip_mismatch";
inline constexpr char invalidRemoteIp[] = "invalid_remote_ip";
inline constexpr char remoteIpMismatch[] = "remote_ip_mismatch";
}  // namespace handshake_fail

/**
 * `unl_fetch_total` outcomes for the transport-level failures.
 *
 * The success path instead labels with `to_string(ListDisposition)`, whose
 * values are owned by the protocol enum and are therefore not restated here --
 * duplicating them would create a second place to update when a disposition is
 * added.
 */
namespace unl_fetch {
inline constexpr char fetchError[] = "fetch_error";
inline constexpr char badStatus[] = "bad_status";
inline constexpr char parseError[] = "parse_error";
}  // namespace unl_fetch

/**
 * `unl_quorum` sub-metrics: the trusted-key count and the bar it must clear.
 */
namespace unl_quorum {
inline constexpr char trustedKeys[] = "trusted_keys";
inline constexpr char quorum[] = "quorum";
// 1 while the validator list has disabled quorum, 0 otherwise. Carries the
// state that used to be encoded by publishing a sentinel value on `quorum`
// itself: a number that large plotted on a shared axis flattens the
// trusted-key line to the baseline, hiding the very failure it marked.
inline constexpr char quorumDisabled[] = "quorum_disabled";
}  // namespace unl_quorum

/**
 * `clock_close_offset_seconds` sub-metric.
 */
namespace clock_offset {
inline constexpr char offset[] = "offset";
}  // namespace clock_offset

/**
 * `sync_state` sub-metrics -- the four "why am I not FULL" signals.
 *
 * `initial_full_duration_us` reads zero until the node first reaches FULL,
 * which is the state this gauge exists to make visible rather than a missing
 * value.
 */
namespace sync_state {
inline constexpr char initialFullDurationUs[] = "initial_full_duration_us";
inline constexpr char networkLedgerGate[] = "network_ledger_gate";
inline constexpr char serverStallSeconds[] = "server_stall_seconds";
inline constexpr char ledgersBehind[] = "ledgers_behind";
}  // namespace sync_state

/**
 * `sync_acquire` sub-metrics -- aggregate acquire progress.
 *
 * The two `missing_*_max` series are the stuck-detector: flat and non-zero
 * across collection ticks means the acquire will never finish, while shrinking
 * means slow but alive. `in_flight` is the context that tells idle from stuck.
 */
namespace sync_acquire {
inline constexpr char missingStateNodesMax[] = "missing_state_nodes_max";
inline constexpr char missingTxNodesMax[] = "missing_tx_nodes_max";
inline constexpr char receivedDataDepth[] = "received_data_depth";
inline constexpr char inFlight[] = "in_flight";
}  // namespace sync_acquire

/**
 * `shamap_cache_hit_rate` sub-metric: which cache the rate describes.
 */
namespace shamap_cache {
inline constexpr char treenode[] = "treenode";
}  // namespace shamap_cache

/**
 * `sync_acquire_source_total` sources: served locally or fetched from peers.
 */
namespace acquire_source {
inline constexpr char local[] = "local";
inline constexpr char network[] = "network";
}  // namespace acquire_source

/**
 * `sync_addnode_total` outcomes -- the per-node verdict on received SHAMap data.
 *
 * The split is what separates real progress (`good`) from wasted bandwidth
 * (`duplicate`) and a misbehaving peer (`invalid`); traffic-level metrics show
 * all three as healthy throughput.
 */
namespace addnode {
inline constexpr char good[] = "good";
inline constexpr char duplicate[] = "duplicate";
inline constexpr char invalid[] = "invalid";
}  // namespace addnode

/**
 * `jobq_saturation` sub-metrics: the numerator, denominator and the backlog.
 */
namespace jobq_saturation {
inline constexpr char runningTasks[] = "running_tasks";
inline constexpr char workerThreads[] = "worker_threads";
inline constexpr char totalWaiting[] = "total_waiting";
}  // namespace jobq_saturation

/**
 * `peer_ledger_supply` sub-metrics: who can serve what this node needs.
 */
namespace peer_supply {
inline constexpr char peersReporting[] = "peers_reporting";
inline constexpr char peersServingValidated[] = "peers_serving_validated";
inline constexpr char peersServingNext[] = "peers_serving_next";
inline constexpr char supplyMinSeq[] = "supply_min_seq";
inline constexpr char supplyMaxSeq[] = "supply_max_seq";
}  // namespace peer_supply

/**
 * `peerfinder_slot_census` sub-metrics -- slots, dials and address caches.
 *
 * `connecting` non-zero while `out_active` stays under `out_max` is the
 * "starting dials and never completing them" case; both caches at zero on a
 * fresh node means there is nothing left to dial at all.
 */
namespace slot_census {
inline constexpr char outActive[] = "out_active";
inline constexpr char outMax[] = "out_max";
inline constexpr char inActive[] = "in_active";
inline constexpr char inMax[] = "in_max";
inline constexpr char connecting[] = "connecting";
inline constexpr char fixedConfigured[] = "fixed_configured";
inline constexpr char fixedActive[] = "fixed_active";
inline constexpr char bootcache[] = "bootcache";
inline constexpr char livecache[] = "livecache";
}  // namespace slot_census

/**
 * `amendment_block` sub-metrics: the warning flag and the countdown.
 */
namespace amendment_block {
inline constexpr char warned[] = "warned";
inline constexpr char secondsToBlock[] = "seconds_to_block";
}  // namespace amendment_block

/**
 * `rotation_state` sub-metrics: is a rotation running, and how many extra
 * writes have rotations caused.
 *
 * Read together: a `copy_forward` total that climbs while `in_flight` is 1 is
 * the rotation doing its extra writes, which is the expected shape. The same
 * total climbing while `in_flight` is 0 would mean the flag leaked, not that
 * rotation is cheap.
 */
namespace rotation_state {
inline constexpr char inFlight[] = "in_flight";
inline constexpr char copyForward[] = "copy_forward";
}  // namespace rotation_state

/**
 * `nodestore_latency` sub-metrics: mean latency per direction, with counts.
 */
namespace nodestore_latency {
inline constexpr char writeCount[] = "write_count";
inline constexpr char readCount[] = "read_count";
inline constexpr char writeMeanUs[] = "write_mean_us";
inline constexpr char readMeanUs[] = "read_mean_us";
// Cumulative microsecond totals. The means above are convenient to read at a
// glance but cannot be rated: they are already a ratio, and a gauge of a ratio
// has no meaningful derivative. Dividing the rate of these totals by the rate of
// the matching count yields the latency over the panel's own window, which is
// what a dashboard actually wants.
inline constexpr char writeDurationUs[] = "write_duration_us";
inline constexpr char readDurationUs[] = "read_duration_us";
}  // namespace nodestore_latency

/**
 * `ledger_quorum_publish` sub-metrics: the gate, and how late publish is.
 */
namespace quorum_publish {
inline constexpr char trustedValidationTally[] = "trusted_validation_tally";
inline constexpr char quorumTarget[] = "quorum_target";
inline constexpr char timeToFirstValidatedUs[] = "time_to_first_validated_us";
inline constexpr char publishLag[] = "publish_lag";
}  // namespace quorum_publish

/**
 * `ledger_quorum_shortfall_total` stage: which gate did the rejecting.
 */
namespace quorum_shortfall {
inline constexpr char preAccept[] = "pre_accept";
}  // namespace quorum_shortfall

/**
 * `ledger_replay_fallback_total` stages: which sub-acquire gave up.
 */
namespace replay_fallback {
inline constexpr char skiplist[] = "skiplist";
inline constexpr char delta[] = "delta";
}  // namespace replay_fallback

/**
 * `ledger_replay_outcome_total` outcomes -- the four terminal states of a
 * replay task. `timeout` is the shared slug above.
 */
namespace replay_outcome {
inline constexpr char success[] = "success";
inline constexpr char buildFailed[] = "build_failed";
inline constexpr char parameterFailed[] = "parameter_failed";
}  // namespace replay_outcome

/**
 * `peer_disconnect_total` reasons -- why a peer connection closed.
 *
 * The split separates our-fault backpressure (`large_sendq`,
 * `charge_resources`) from a topology or network fault (`not_useful`,
 * `ping_timeout`, `read_error`); the two call for opposite responses.
 * `unknown` is the initial value and appears when a teardown path set no
 * cause, so an unattributed disconnect is visible rather than absent.
 */
namespace disconnect {
inline constexpr char unknown[] = "unknown";
inline constexpr char malformedHandshake[] = "malformed_handshake";
inline constexpr char stopping[] = "stopping";
inline constexpr char chargeResources[] = "charge_resources";
inline constexpr char timerError[] = "timer_error";
inline constexpr char largeSendq[] = "large_sendq";
inline constexpr char notUseful[] = "not_useful";
inline constexpr char pingTimeout[] = "ping_timeout";
inline constexpr char shutdown[] = "shutdown";
inline constexpr char sharedValue[] = "shared_value";
inline constexpr char writeError[] = "write_error";
inline constexpr char graceful[] = "graceful";
inline constexpr char readError[] = "read_error";
}  // namespace disconnect

/**
 * `serve_refused_total` request kinds: what the peer had asked for.
 */
namespace serve_request {
inline constexpr char object[] = "object";
inline constexpr char fetchpack[] = "fetchpack";
inline constexpr char txset[] = "txset";
inline constexpr char ledger[] = "ledger";
}  // namespace serve_request

/**
 * `serve_refused_total` reasons: why this node would not answer.
 * `not_found` is the shared slug above.
 *
 * `empty_reply` is the subtle one: the map WAS found, but the reply loop
 * produced no nodes, so the requester still gets nothing and must ask another
 * peer. Counting it as served would make a node that answers every request
 * with an empty payload look healthy.
 */
namespace serve_refused {
inline constexpr char sendqFull[] = "sendq_full";
inline constexpr char loadShed[] = "load_shed";
inline constexpr char badType[] = "bad_type";
inline constexpr char noMap[] = "no_map";
inline constexpr char emptyReply[] = "empty_reply";
}  // namespace serve_refused

}  // namespace lval

}  // namespace xrpl::telemetry
