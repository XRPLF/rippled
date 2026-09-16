#pragma once

/**
 * Observable-gauge layer for xrpld — the pull-model half of the OTel metric
 * surface.
 *
 * Declares the class that registers every observable instrument whose callback
 * samples live server state, and that owns the handles those registrations
 * return. The export pipeline itself — provider, meter, exporter and the
 * synchronous counters and histograms — belongs to MetricsRegistry, the
 * sibling class in this namespace. This layer borrows that meter and adds the
 * pull-model instruments on top of it.
 */

// Unguarded because the constructor names beast::Journal and MetricsRegistry in
// both configurations. beast::Journal is taken by value, so it needs a complete
// type even when telemetry is off.
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/telemetry/MetricsRegistry.h>

#ifdef XRPL_ENABLE_TELEMETRY
// Guarded like the members that use them: std::atomic by callbacksDetached_,
// std::function and std::int64_t by the ObserveFn sink, the OTel instrument
// headers by the 31 instrument handles, and observer_result.h by the
// ObserverResult parameter of observeCacheLockHoldPeaks().
#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/nostd/shared_ptr.h>

#include <atomic>
#include <cstdint>
#include <functional>
#endif

namespace xrpl {

class ServiceRegistry;

// Defined in src/xrpld/app/ledger/AcquireStats.h. Forward-declared because
// only one helper signature names it, and pulling an xrpld/app header in here
// would widen the dependencies of every file that includes this one.
class AcquireStats;

namespace node_store {
class Database;
}  // namespace node_store

}  // namespace xrpl

namespace xrpl::telemetry {

/**
 * Registers and owns the pull-model OTel instruments that sample live server
 * state.
 *
 * Each registered callback runs on the OTel reader thread and reads services
 * through the ServiceRegistry reference given at construction. Both the core
 * registry and the ServiceRegistry are borrowed, so both must outlive this
 * object.
 *
 * Collaborator diagram (ASCII):
 *
 * AppMetricGauges
 * +-- MetricsRegistry (borrowed)
 * |   +-- meter() -- creates all 31 observable instruments
 * |   +-- getValidationTracker() -- read by the agreement instruments
 * |   +-- OTel MeterProvider
 * |       +-- PeriodicExportingMetricReader (~10 s tick, drives the callbacks)
 * +-- ServiceRegistry (borrowed) -- every value the callbacks sample
 * +-- 31 ObservableInstrument handles (owned)
 *
 * Callback flow, once startAsyncGauges() has run:
 *
 * Reader thread tick (~10 s)
 * v
 * SDK invokes each callback, passing this object as the state pointer
 * v
 * callbacksDetached_ true? -- yes --> return without observing anything
 * v no
 * read current values from the ServiceRegistry, Observe() each one
 * v
 * the core's pipeline exports them over OTLP/HTTP
 *
 * One instrument per metric domain: cache hit rates and sizes, TxQ state,
 * CountedObject instances, load-factor breakdown, NodeStore I/O and
 * acquisition stalls, online-delete rotation state, server info, build
 * version, complete ledger ranges, database sizes, validator health, peer
 * quality, reduce-relay efficiency, ledger economy, state tracking, storage
 * detail, validation agreement, UNL quorum, clock close offset, sync state,
 * ledger-acquire progress, SHAMap tree-node cache hit rate, worker-pool
 * saturation, peer ledger supply, PeerFinder slot census, amendment block and
 * the ledger quorum/publish gate.
 * Most multiplex their values through a `metric` label, so a new value needs
 * no new instrument; object counts use `type`, build info uses `version`, and
 * complete ledgers uses `bound` and `index`. Twenty-seven are ObservableGauges
 * and four are ObservableCounters, the latter where the value read is already
 * cumulative and must never decrease.
 *
 * Teardown order is a caller contract, in this order: detachCallbacks(), then
 * MetricsRegistry::stop(), then destroy this object. stop() joins the reader
 * thread, so once it returns no callback can run again and destruction is
 * safe.
 *
 * @code
 * // Primary use. Construct after the core registry, and arm only once every
 * // service the callbacks read exists. The overlay is built last, so it
 * // fixes where this call can go.
 * gauges_ = std::make_unique<AppMetricGauges>(
 *     *metricsRegistry_, *this, logs_->journal("MetricsRegistry"));
 * gauges_->startAsyncGauges();
 *
 * // Shutdown, in the required order.
 * gauges_->detachCallbacks();
 * metricsRegistry_->stop();
 *
 * // Edge case: arming without a working pipeline. The core hands out a
 * // no-op meter when the pipeline fails to build, so this logs a warning
 * // and registers nothing rather than reporting a success it cannot keep.
 * // A second startAsyncGauges() behaves the same way.
 * gauges_->startAsyncGauges();
 * @endcode
 *
 * @note Thread safety:
 * - The callbacks run on the OTel reader thread, concurrently with the
 * writers of the state they read. Each reads only lock-protected or
 * atomic state and wraps its body in a catch-all try block, so a
 * transient failure never brings down the reader thread.
 * - startAsyncGauges() and the destructor are NOT thread-safe with each
 * other and belong on the single server lifecycle thread. armed_ is a
 * plain bool because that call is its only reader and writer.
 * - detachCallbacks() may be called from any thread. It is one release
 * store to an atomic that every callback acquire-loads.
 *
 * @note Limitations:
 * - Arms once per object. A second startAsyncGauges() logs a warning and
 * registers nothing, so the instruments are never duplicated.
 * - detachCallbacks() is one-way. Calling it before startAsyncGauges()
 * leaves every instrument registered but permanently silent.
 * - Destroying this object while the core is still exporting is unsafe.
 * The SDK holds this address as its callback state, and the flag the
 * destructor sets dies with the object. Only stop() on the core closes
 * that window, which is why it comes first.
 * - The instrument set is fixed at registration. A pull-model instrument
 * cannot be created lazily, so a new metric domain needs a new helper
 * and a new handle here.
 */
class AppMetricGauges
{
public:
    /**
     * Bind the layer to the core registry and to the services its callbacks
     * will sample. Registers nothing; startAsyncGauges() does that.
     *
     * @param core Registry owning the meter these instruments are created on,
     * and the validation tracker two of them read. Must outlive this object.
     * @param app  Services the callbacks sample. Must outlive this object.
     * @param journal Log output.
     */
    AppMetricGauges(MetricsRegistry& core, ServiceRegistry& app, beast::Journal journal);

    /**
     * Disarms the callbacks, then releases the instrument handles.
     *
     * @note This is a last resort, not the teardown path. See the class note
     * on destruction order.
     */
    ~AppMetricGauges();

    /**
     * Non-copyable, non-movable. The registered callbacks hold this object's
     * address, so it cannot move.
     */
    AppMetricGauges(AppMetricGauges const&) = delete;
    AppMetricGauges&
    operator=(AppMetricGauges const&) = delete;

    /**
     * Create and arm every pull-model instrument — mostly ObservableGauges,
     * plus the ObservableCounters whose source value is already cumulative.
     *
     * Registering an observable also arms the reader thread to invoke its
     * callback on the next tick, so this is an ordering decision and not just
     * tidiness: it cannot run before the services those callbacks read exist.
     *
     * Does nothing but log a warning when the core is disabled, when it has no
     * real pipeline, when this object is already armed, or when the core has
     * already stopped.
     *
     * @pre Every service the callbacks read is constructed. The full set, from
     * the `app.get*()` calls in the registration helpers, is: Overlay, OPs
     * (NetworkOPs), LedgerMaster, OpenLedger, TxQ, NodeStore, NodeFamily,
     * Validators, AcceptedLedgerCache, CachedSLEs, AcquireStats, TimeKeeper,
     * RelationalDatabase, InboundLedgers, FeeTrack, LoadManager, JobQueue and
     * AmendmentTable.
     * Overlay is built last, so it fixes this call's position:
     * `ServiceRegistry::getOverlay()` `XRPL_ASSERT`s that `overlay_` is
     * non-null, and a reader-thread tick before the overlay exists aborts a
     * Debug build. The callbacks' catch-all try block does not catch an
     * assert. `getTxQ()` and `getRelationalDatabase()` assert likewise.
     */
    void
    startAsyncGauges();

    /**
     * Disarm every registered callback so it no-ops on the next reader-thread
     * tick.
     *
     * Must be called BEFORE any service the callbacks read (nodeStore,
     * overlay, networkOPs, ledgerMaster and the rest) is stopped. The flag is
     * checked with acquire ordering at the top of every callback; together
     * with the release store here that guarantees no callback starting after
     * this returns will dereference an already-stopped service.
     *
     * Idempotent: the flag is one-way, only ever set to true, and nothing
     * clears it.
     *
     * @note One-way means this is a shutdown-only call. Calling it before
     * startAsyncGauges() does not "have no effect" — it permanently disarms
     * every instrument that call registers, so they exist but never observe a
     * value.
     */
    void
    detachCallbacks() noexcept;

#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * Sink handed to the nodestore_state helpers below.
     *
     * Every value they publish multiplexes onto the single `nodestore_state`
     * instrument through its `metric` label, so the helpers need no access to
     * the OTel observer result -- just somewhere to put a name and a number.
     */
    using ObserveFn = std::function<void(char const* name, std::int64_t value)>;

    // The four helpers below are public because each is a pure transform from
    // a statistics object to a set of name-value pairs. They read only their
    // arguments and need no AppMetricGauges instance, so a test can drive one
    // directly with a recording sink and assert the exact `metric` label
    // values it publishes. Exposing them widens no state.

    /**
     * Observe the NodeStore I/O totals and the means derived from them.
     *
     * Publishes the four cumulative totals (`node_reads_total`,
     * `node_writes`, `node_reads_duration_us`, `node_writes_duration_us`)
     * unconditionally, plus `read_mean_us` and `write_mean_us` derived from
     * them via MetricsRegistry::scaledMean(). `write_mean_us` is the signal
     * for the "a node with a large existing database syncs slower than a fresh
     * one" symptom: back-fill is write-bound, so no read-side reading can show
     * it. All three concrete store paths time themselves through
     * Database::recordStoreDuration(), so the write mean is live on an
     * ordinary node.
     *
     * Gauge rather than histogram, deliberately. A histogram would give true
     * percentiles, but it costs one Record() per node object on the
     * store/fetch path, and one ledger write walks thousands of SHAMap
     * nodes. This reads the existing atomics once per ~10 s tick and adds
     * nothing to the hot path. Consequence, stated plainly: p99 is NOT
     * obtainable from this signal. A histogram added later would also need an
     * explicit-bucket View registered on the core registry's meter provider,
     * because the SDK's default buckets top out at 10,000.
     *
     * @param db       NodeStore to read the counters from.
     * @param observe  Sink for one `metric`-labelled value.
     *
     * @note The totals are monotonic and never reset, so a panel wanting
     * current rather than since-boot latency divides the two rates. That is
     * why the counts and duration totals are exported beside the means.
     * @note A mean is omitted when its count is 0, so a dashboard shows a gap
     * rather than a plausible-looking 0 us.
     */
    static void
    observeNodeStoreTotals(node_store::Database& db, ObserveFn const& observe);

    /**
     * Observe the backend write-path detail, when the backend measures it.
     *
     * Publishes nothing for a backend whose getWriteStats() is std::nullopt,
     * which is every backend except NuDB. Absent labels let a reader tell
     * "not measured" from "measured, and idle"; zeros would read as a
     * perfectly idle write path.
     *
     * @param db       NodeStore whose writable backend is sampled.
     * @param observe  Sink for one `metric`-labelled value.
     */
    static void
    observeWritePathDetail(node_store::Database const& db, ObserveFn const& observe);

    /**
     * Observe the ledger-acquisition progress and stall counters.
     *
     * @param stats    Process-wide acquisition counters.
     * @param observe  Sink for one `metric`-labelled value.
     */
    static void
    observeAcquireStats(AcquireStats const& stats, ObserveFn const& observe);

    /**
     * Observe the read queue depth and the read thread-pool counts.
     *
     * These four have no accessor on Database, so its JSON counters object
     * is still the only way to reach them.
     *
     * @param db       NodeStore to read the JSON counters from.
     * @param observe  Sink for one `metric`-labelled value.
     */
    static void
    observeReadQueue(node_store::Database& db, ObserveFn const& observe);

private:
    /**
     * Registry owning the meter these instruments are created on, the
     * pipeline they export through, and the validation tracker two of them
     * read. Borrowed; it outlives this object.
     */
    MetricsRegistry& core_;

    /**
     * Services the callbacks sample. Borrowed; it outlives this object.
     */
    ServiceRegistry& app_;

    /**
     * Log output. Shares the `MetricsRegistry` journal partition, so one
     * log-level setting covers the whole metric pipeline.
     */
    beast::Journal const journal_;

    /**
     * True once startAsyncGauges() has registered the instruments. Read and
     * written only by that call, from the server lifecycle thread, so it needs
     * no atomic.
     */
    bool armed_{false};

    /**
     * Set by detachCallbacks() during shutdown so every callback returns early
     * before reading services that may already be stopped. Checked with
     * memory_order_acquire at the top of each callback to pair with the
     * memory_order_release store in detachCallbacks().
     */
    std::atomic<bool> callbacksDetached_{false};

    // --- Observable instrument handles ---
    // Held so the callbacks stay registered for as long as this object lives.
    /**
     * Cache hit rates and sizes.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        cacheHitRateGauge_;
    /**
     * Transaction queue state.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> txqGauge_;
    /**
     * Live instance counts for every CountedObject type.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        objectCountGauge_;
    /**
     * Fee load-factor breakdown.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> loadFactorGauge_;
    /**
     * Every NodeStore value on one instrument, separated by its `metric`
     * label: I/O totals, the read and write means derived from them, the NuDB
     * write-queue detail, and the ledger-acquisition stall counters.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> nodeStoreGauge_;
    /**
     * Online-delete rotation state and its copy-forward write total. Publishes
     * nothing on a node without `online_delete`, where the node store is not a
     * rotating one.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        rotationStateGauge_;
    /**
     * Server-level health: operating mode, uptime, peers, ledger sequences and
     * the last consensus round.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> serverInfoGauge_;
    /**
     * Trusted UNL key count against the required quorum.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> unlQuorumGauge_;
    /**
     * Network close-time offset (local clock skew).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> clockSkewGauge_;
    /**
     * Sync-pipeline state signals: time to first FULL, the network-ledger gate,
     * the current server stall and how many ledgers behind the network.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> syncStateGauge_;
    /**
     * ObservableCounter: server_stall_events_total — observed from
     * LoadManager::getStallEventCount() (cumulative episode tally owned by the
     * load-monitor thread).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        stallEventsObservable_;
    /**
     * Aggregate ledger-acquire progress: max missing state and tx nodes,
     * received-data stash depth and the in-flight acquire count.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        syncAcquireGauge_;
    /**
     * SHAMap tree-node cache hit rate, the memory layer above the node store's
     * own hit ratio.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        shamapCacheHitRateGauge_;
    /**
     * Global worker-pool saturation: tasks in flight, configured worker threads
     * and total jobs queued.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        jobQueueSaturationGauge_;
    /**
     * How much of the needed ledger range the connected peer set can serve.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        peerLedgerSupplyGauge_;
    /**
     * PeerFinder slot occupancy, connection attempts, fixed peers and
     * address-cache depth.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> slotCensusGauge_;
    /**
     * Amendment-block warning flag and the countdown to the amendment
     * activating.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        amendmentBlockGauge_;
    /**
     * Pre-accept quorum gate and the publish lag: the trusted-validation tally
     * against the quorum it must reach, the time to the first fully-validated
     * ledger, and how far publishing trails validation.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        ledgerQuorumPublishGauge_;
    /**
     * Build version, carried as a label with a constant value of 1.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> buildInfoGauge_;
    /**
     * Complete ledger range start/end pairs.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        completeLedgersGauge_;
    /**
     * Database sizes and the historical fetch rate.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> dbMetricsGauge_;

    // --- External dashboard parity instruments ---
    /**
     * Validator health: amendment blocked, UNL blocked, quorum, UNL expiry.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validatorHealthGauge_;
    /**
     * Peer network quality: P90 latency, diverged peer count, version spread
     * and the upgrade recommendation derived from it.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        peerQualityGauge_;
    /**
     * Transaction reduce-relay efficiency: selected against suppressed peers,
     * feature-disabled peers, missing-tx frequency.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        reduceRelayGauge_;
    /**
     * Ledger economy: base fee, reserves, ledger age and transaction rate.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        ledgerEconomyGauge_;
    /**
     * Node state tracking: operating mode as a number, and time in that mode.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        stateTrackingGauge_;
    /**
     * Storage detail: the cumulative payload bytes handed to the NodeStore.
     * Logical bytes stored, not on-disk file size.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        storageDetailGauge_;
    /**
     * Validation agreement percentages and counts over the 1h, 24h and 7d
     * windows kept by ValidationTracker.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validationAgreementGauge_;
    /**
     * ObservableCounter: jq_trans_overflow_total — observed from
     * Overlay::getJqTransOverflow() (cumulative overflow tally owned by the
     * overlay).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        jqTransOverflowObservable_;
    /**
     * ObservableCounter: validation_agreements_total — observed from
     * ValidationTracker::totalAgreementsEver() (monotonic gross lifetime
     * tally, initial-classification semantics).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validationAgreementsObservable_;
    /**
     * ObservableCounter: validation_missed_total — observed from
     * ValidationTracker::totalMissedEver() (monotonic gross lifetime tally,
     * initial-classification semantics).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validationMissedObservable_;

    /**
     * Create and arm every instrument, one helper per metric domain so that
     * each helper stays well under the 80-line-per-function limit.
     *
     * Called only from startAsyncGauges(), which owns the enable, arm-once,
     * pipeline and service-readiness guards.
     */
    void
    registerAsyncGauges();

    // Per-domain registration helpers. Each creates its instrument -- an
    // ObservableGauge, or an ObservableCounter where the underlying value is
    // cumulative -- and attaches a single callback that reads current values
    // from the ServiceRegistry. The callbacks run on the OTel
    // PeriodicExportingMetricReader background thread (~10 s tick).
    void
    registerJqTransOverflowCounter();  // gap-fill: overlay overflow total
    void
    registerCacheHitRateGauge();
    /**
     * Observe the two TaggedCache lock-hold peaks onto the cache_metrics
     * gauge. Split out to keep registerCacheHitRateGauge's callback under
     * the 80-line limit. Static because it touches neither instance state
     * nor telemetry members — it reads through the passed app reference.
     *
     * @param result Observer result the two peaks are published onto.
     * @param app    Services holding the caches whose peaks are read.
     */
    static void
    observeCacheLockHoldPeaks(opentelemetry::metrics::ObserverResult& result, ServiceRegistry& app);
    void
    registerTxqGauge();
    void
    registerObjectCountGauge();
    void
    registerLoadFactorGauge();
    void
    registerNodeStoreGauge();
    void
    registerRotationStateGauge();  // Sync diagnostics: online_delete rotation
    void
    registerServerInfoGauge();
    void
    registerBuildInfoGauge();
    void
    registerCompleteLedgersGauge();
    void
    registerDbMetricsGauge();
    void
    registerValidatorHealthGauge();
    void
    registerPeerQualityGauge();
    void
    registerReduceRelayGauge();  // Reduce-relay efficiency
    void
    registerLedgerEconomyGauge();
    void
    registerStateTrackingGauge();
    void
    registerStorageDetailGauge();
    void
    registerValidationAgreementGauge();
    void
    registerValidationTotalsCounters();  // gap-fill: lifetime agree/miss _total

    /**
     * Register the `unl_quorum` gauge.
     *
     * Observes two series under the `metric` attribute:
     * `trusted_keys` (ValidatorList::trustedKeyCount()) and `quorum`
     * (ValidatorList::quorum()). Both are cheap accessors — one shared
     * lock and one atomic load.
     *
     * `trusted_keys < quorum` means the node can never fully validate a
     * ledger, so it will sit in `syncing` until the UNL is fixed. That
     * makes this the first place to look when a node never leaves
     * `syncing`.
     *
     * @note Pulled on the OTel reader thread (~10 s tick); does no work
     * on any hot path.
     */
    void
    registerUnlQuorumGauge();  // sync diagnostics: UNL vs quorum

    /**
     * Register the `clock_close_offset_seconds` gauge.
     *
     * Observes one series, `offset`, from
     * TimeKeeper::closeOffset(): the seconds this node's notion of
     * network close time is displaced from its own wall clock.
     *
     * The value MAY BE NEGATIVE, meaning the local clock runs ahead of
     * the network. Whole-second resolution is all the signal carries,
     * since that is the unit TimeKeeper stores.
     *
     * @note `server_info` only reports this field once |offset| >= 60 s
     * (NetworkOPs), so this gauge is the first continuous export of it.
     * Pulled on the OTel reader thread (~10 s tick); one atomic load.
     */
    void
    registerClockSkewGauge();  // sync diagnostics: close-time offset

    /**
     * Register the `sync_state` gauge.
     *
     * One instrument fanning out four series under the `metric` attribute,
     * each answering a different "why is this node not FULL yet?" question
     * that is otherwise visible only in a log line or in server_info JSON:
     *
     *   `initial_full_duration_us` — microseconds from process start to the
     *     first FULL transition (NetworkOPs::getInitialSyncDurationUs()).
     *     Stays 0 until FULL is reached, so a flat 0 IS the "never synced"
     *     signal; once set it never changes again.
     *   `network_ledger_gate` — 1 while the node is still waiting to see a
     *     full network ledger (NetworkOPs::isNeedNetworkLedger()), else 0. A
     *     persistent 1 blocks transaction submission and FULL.
     *   `server_stall_seconds` — current main-loop stall duration
     *     (LoadManager::getCurrentStallSeconds()), 0 when healthy.
     *   `ledgers_behind` — network tip minus our validated sequence
     *     (NetworkOPs::getLedgersBehindNetwork()).
     *
     * The monotonic stall-episode count is a separate instrument
     * (`server_stall_events_total`) because a counter and a gauge cannot share
     * one instrument: Prometheus would otherwise see a cumulative total under
     * last-value aggregation and `rate()` would be meaningless.
     *
     * @note Pulled on the OTel reader thread (~10 s tick), never on a hot
     * path. Three of the four reads are a lock or atomic load; `ledgers_behind`
     * additionally walks the connected-peer list, reading each peer's already
     * cached ledger range — bounded by peer count and issuing no network I/O.
     */
    void
    registerSyncStateGauge();  // sync diagnostics: gate, stall, ledgers behind

    /**
     * Register the `server_stall_events_total` observable counter.
     *
     * Observes LoadManager::getStallEventCount(): how many distinct stall
     * episodes the monitor thread has reported since process start. Separate
     * from `sync_state` because it is cumulative and monotonic, so it needs
     * counter (not last-value) aggregation for `rate()` to mean anything.
     *
     * Read together with `sync_state{metric="server_stall_seconds"}`: a rising
     * event count means repeated fresh stalls, while a flat count with a large
     * stall-seconds value means one long unresolved stall.
     *
     * @note Pulled on the OTel reader thread (~10 s tick); one atomic load.
     */
    void
    registerStallEventsCounter();  // sync diagnostics: stall episode count

    /**
     * Register the `sync_acquire` gauge.
     *
     * One instrument fanning out four series under the `metric` attribute, all
     * from a single InboundLedgers::acquireProgress() snapshot:
     *
     *   `missing_state_nodes_max` — largest outstanding account-state node count
     *     of any in-flight acquire. THE headline stuck-sync signal: flat and
     *     non-zero across ticks means the acquire will never finish, shrinking
     *     means it is slow but alive.
     *   `missing_tx_nodes_max` — the same for the transaction tree.
     *   `received_data_depth` — peer packets stashed across all acquires waiting
     *     to be applied. Deep means processing, not peer supply, is the limit.
     *   `in_flight` — how many acquires are running, so the three values above
     *     can be read in context: all zero with `in_flight` zero is idle, not
     *     healthy.
     *
     * Deliberately aggregated rather than per-ledger. A `ledger_seq` label would
     * mint a new time series for every ledger the node ever acquires, which is
     * unbounded cardinality; the max/sum keeps the "is it stuck?" answer while
     * the per-ledger identity stays on the `ledger.acquire` span, where
     * high-cardinality identity belongs.
     *
     * @note Pulled on the OTel reader thread (~10 s tick), never on a hot path.
     * The snapshot takes the acquire-collection lock only to copy shared_ptrs,
     * then reads relaxed atomics; the emit sites that feed those atomics all sit
     * outside the per-tree-node loops.
     */
    void
    registerSyncAcquireGauge();  // sync diagnostics: acquire progress

    /**
     * Register the `shamap_cache_hit_rate` gauge.
     *
     * Observes one series, `treenode`, from TreeNodeCache::getHitRate(): the
     * percentage of SHAMap tree-node lookups served from memory instead of the
     * node store. During a fresh sync a low rate means the node re-reads the
     * same subtrees from disk, so sync is disk-bound rather than peer-bound.
     *
     * Distinct from the `NuDB Cache Hit Ratio` panel on the ledger-data-sync
     * dashboard: that one is derived from `nodestore_state` and measures the
     * node-store layer (`node_reads_hit / node_reads_total`). This gauge
     * measures the in-memory tree-node cache that sits ABOVE it, so a request
     * missing here is what produces a node-store read there.
     *
     * The full-below cache is deliberately NOT reported. It is a KeyCache, whose
     * only lookup path is TaggedCache::touchIfExists(), and that method
     * increments `stats_.hits`/`stats_.misses` while `getHitRate()` reads the
     * separate `hits_`/`misses_` members. Its hit rate is therefore hard-wired
     * to 0 regardless of behaviour, so exporting it would ship a permanently
     * empty panel; fixing that accounting belongs in a libxrpl change of its own.
     *
     * @note Pulled on the OTel reader thread (~10 s tick). Takes the cache's
     * mutex for two integer reads and a divide; no hot-path cost.
     */
    void
    registerCacheHitRateDetailGauge();  // sync diagnostics: treenode cache

    /**
     * Register the `jobq_saturation` gauge.
     *
     * Three series under the `metric` attribute, from one
     * JobQueue::getWorkerSaturation() reading:
     *
     *   `running_tasks` — worker threads currently executing a job.
     *   `worker_threads` — threads the pool is configured to run, the
     *     denominator that makes `running_tasks` legible. Exported rather
     *     than hardcoded in the dashboard because it is derived at startup
     *     from `[workers]`, node size and hardware concurrency.
     *   `total_waiting` — jobs queued across all types.
     *
     * The reason this is separate from the per-job-type gauges JobQueue
     * itself publishes (`jobq_<type>_waiting` / `_running` / `_deferred`):
     * when the pool itself is exhausted, every subsystem waiting behind it
     * looks independently slow, and each per-type panel invites the wrong
     * conclusion. A `running_tasks / worker_threads` ratio at 1.0 with a
     * non-zero `total_waiting` attributes the whole slowdown to pool
     * exhaustion once. Those per-type gauges carry no capacity term at all,
     * so no reading there can say whether the pool is the cause.
     *
     * @note Pulled on the OTel reader thread (~10 s tick). One atomic load,
     * one plain int read, and one pass over the per-type counters under the
     * JobQueue mutex.
     */
    void
    registerJobQueueSaturationGauge();  // sync diagnostics: pool saturation

    /**
     * Register the `peer_ledger_supply` gauge.
     *
     * Five series under the `metric` attribute, from one
     * Overlay::getPeerLedgerSupply() pass over the active peers:
     *
     *   `peers_reporting` — peers that have advertised a ledger range at all.
     *     The denominator that makes the rest readable.
     *   `peers_serving_validated` — peers whose range covers this node's
     *     validated sequence.
     *   `peers_serving_next` — **the signal this gauge exists for.** Peers
     *     whose range covers validated + 1, the next ledger this node must
     *     acquire. Zero here with a non-zero `peers_reporting` means no
     *     connected peer holds what this node needs, so no amount of waiting
     *     will finish the sync; the peer set has to change.
     *   `supply_min_seq` / `supply_max_seq` — the sequence window the peer set
     *     covers, so an operator can see whether the node is asking for
     *     history nobody kept or for a tip nobody has reached.
     *
     * Each peer already caches the range it advertises in mtSTATUS_CHANGE
     * (`PeerImp::minLedger_` / `maxLedger_`, read via `Peer::ledgerRange()`),
     * but those ranges were never compared against each other, so "no peer has
     * what I need" was indistinguishable from "my peers are slow" — the two
     * faults with completely different fixes.
     *
     * Distinct from what already exists. `server_info{metric="peers"}` is a
     * bare connection count with no notion of what those peers hold.
     * `sync_state{metric="ledgers_behind"}` uses the same per-peer maxima but
     * collapses them to a single distance-to-tip number, which cannot say how
     * many peers can serve that distance or whether the range has a hole.
     * `peer_quality{metric="peers_insane_count"}` counts peers on a different
     * chain, which is a correctness signal, not an availability one.
     *
     * Peers advertising [0, 0] have not reported yet and are excluded from
     * every field, so they cannot make a healthy peer set appear to serve from
     * genesis. When nothing has reported, both window fields read 0, which is
     * why `peers_reporting` must be read alongside them.
     *
     * @note Pulled on the OTel reader thread (~10 s tick), never on a message
     * path. O(peers): `getActivePeers()` copies the peer list under the overlay
     * lock and releases it, then each peer's cached range is read under that
     * peer's own short-lived lock.
     */
    void
    registerPeerLedgerSupplyGauge();  // sync diagnostics: peer range coverage

    /**
     * Register the `peerfinder_slot_census` gauge.
     *
     * Nine series under the `metric` attribute, from one
     * Overlay::getSlotCensus() snapshot: `out_active`, `out_max`, `in_active`,
     * `in_max`, `connecting`, `fixed_configured`, `fixed_active`, `bootcache`
     * and `livecache`.
     *
     * All nine are already computed inside PeerFinder (`Counts`, `Bootcache`,
     * `Livecache`, the fixed-peer map) and only two of them are exported
     * today, as the legacy beast::insight gauges
     * `peer_finder_active_inbound_peers` and
     * `peer_finder_active_outbound_peers`. Those two carry no capacity,
     * attempt or cache term, which leaves the three most common bootstrap
     * failures invisible:
     *
     *   - `connecting` non-zero while `out_active` stays below `out_max` —
     *     dials are being started and never completing. Without the attempt
     *     count this looks the same as a node that is not dialling at all.
     *   - `bootcache` at 0 — no seed addresses to dial in the first place.
     *   - `fixed_active` below `fixed_configured` — a peer named in the
     *     configuration is unreachable.
     *
     * The nine fields come from a single acquire of the PeerFinder lock, so
     * they are mutually consistent and share one label set. The two legacy
     * gauges are read at unrelated instants and cannot be joined with each
     * other, let alone with a capacity term.
     *
     * @note Pulled on the OTel reader thread (~10 s tick). One lock acquire,
     * then integer and container-size reads.
     */
    void
    registerSlotCensusGauge();  // sync diagnostics: peerfinder slot census

    /**
     * Register the `amendment_block` gauge.
     *
     * Two series under the `metric` attribute:
     *
     *   `warned` — 1 once an unsupported amendment has reached majority, from
     *     NetworkOPs::isAmendmentWarned().
     *   `seconds_to_block` — **the leading indicator.** Seconds until that
     *     amendment activates, derived from
     *     `AmendmentTable::firstUnsupportedExpected()` against the network
     *     close time. `-1` when nothing is pending, matching the sentinel
     *     `validator_health{metric="unl_expiry_days"}` already uses, so the
     *     healthy state is a distinct value rather than a missing series.
     *     Clamped at 0 rather than going negative, because past-due means the
     *     block is imminent, not overdue by some amount.
     *
     * Amendment-blocked is a terminal sync blocker: the node stops validating
     * and never resumes without a software upgrade. The existing
     * `validator_health{metric="amendment_blocked"}` reports that state after
     * it has happened, when nothing can be done about it. This gauge is the
     * window before it, which is the only actionable part.
     *
     * The blocking amendment's identity is deliberately NOT a label. The
     * network can vote on an arbitrary 256-bit amendment id — the set is not
     * drawn from this build's known features — so an id label would be
     * unbounded cardinality and would mint a permanent new series per
     * amendment. The id is already logged by
     * `AmendmentTableImpl::doValidatedLedger` ("Unsupported amendment <hash>
     * reached majority at ..."), so it is available through logs, correlated
     * to this series by node and time.
     *
     * @note Pulled on the OTel reader thread (~10 s tick). One mutex acquire
     * inside the amendment table plus one clock read.
     * @note The subtraction is done in `std::int64_t`, not in NetClock's
     * unsigned representation, so a past-due activation cannot wrap to a huge
     * positive count.
     */
    void
    registerAmendmentBlockGauge();  // sync diagnostics: amendment countdown

    /**
     * Register the `ledger_quorum_publish` gauge.
     *
     * Four series under the `metric` attribute, read from LedgerMaster:
     *
     *   `trusted_validation_tally` — trusted validations counted at the last
     *     pre-accept gate in `LedgerMaster::checkAccept`.
     *   `quorum_target` — validations that gate required. **The pair is the
     *     signal.** The tally alone cannot separate a node accumulating
     *     validations toward quorum (slow, will finish) from one whose tally
     *     plateaus below the target (stuck, never will); with the target
     *     beside it, the two shapes are unmistakable.
     *   `time_to_first_validated_us` — how long the node took to get its
     *     first ledger through that gate. One-shot, like the
     *     `sync_state{initial_full_duration_us}` milestone: a value means it
     *     happened and this is how long it took, 0 means it never has.
     *   `publish_lag` — validated sequence minus published sequence. Non-zero
     *     and growing means validation is fine and the publish pipeline is
     *     behind, which no other signal distinguishes.
     *
     * All four are grouped under one instrument because they answer one
     * question in sequence — did enough validations arrive, did the gate pass,
     * how long did that take, and did the result reach clients — so an
     * operator reads them from a single consistent poll.
     *
     * Distinct from what already exists. `unl_quorum{quorum}` is the quorum
     * the validator list *configures*, a static property of the trusted set;
     * `quorum_target` is what an actual gate evaluation *required*, and the
     * tally beside it is the live count that must reach it — neither existed
     * anywhere before. `server_info{validated_ledger_seq}` publishes the
     * validated sequence but nothing published the pubLedgerSeq_ counterpart,
     * so the lag between them was not derivable at all.
     *
     * @note `quorum_target` reports int64 max when the validator list has
     * switched quorum off (`ValidatorList::quorum()` returns SIZE_MAX). The
     * clamp lives in `LedgerMaster::checkAccept`, so the wrap to -1 that would
     * invert a tally-versus-target panel cannot happen here.
     * @note Pulled on the OTel reader thread (~10 s tick). Five relaxed atomic
     * loads through lock-free LedgerMaster accessors: no lock is taken, which
     * is what keeps an OTel callback from ever contending with, or inverting
     * lock order against, the LedgerMaster mutex held by the emit path.
     */
    void
    registerLedgerQuorumPublishGauge();  // sync diagnostics: quorum + publish
#endif                                   // XRPL_ENABLE_TELEMETRY
};

}  // namespace xrpl::telemetry
