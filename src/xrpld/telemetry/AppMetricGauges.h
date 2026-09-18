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
// std::function and std::int64_t by the ObserveFn sink, and the two OTel
// headers by the 19 instrument handles.
#include <opentelemetry/metrics/async_instruments.h>
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
 * |   +-- meter() -- creates all 19 observable instruments
 * |   +-- getValidationTracker() -- read by the agreement instruments
 * |   +-- OTel MeterProvider
 * |       +-- PeriodicExportingMetricReader (~10 s tick, drives the callbacks)
 * +-- ServiceRegistry (borrowed) -- every value the callbacks sample
 * +-- 19 ObservableInstrument handles (owned)
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
 * acquisition stalls, server info, build version, complete ledger ranges,
 * database sizes, validator health, peer quality, reduce-relay efficiency,
 * ledger economy, state tracking, storage detail and validation agreement.
 * Most multiplex their values through a `metric` label, so a new value needs
 * no new instrument; object counts use `type`, build info uses `version`, and
 * complete ledgers uses `bound` and `index`. Sixteen are ObservableGauges and
 * three are ObservableCounters, the latter where the value read is already
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
     * RelationalDatabase, InboundLedgers and FeeTrack.
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
     * @param db       NodeStore to read the counters from.
     * @param observe  Sink for one `metric`-labelled value.
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
     * Server-level health: operating mode, uptime, peers, ledger sequences and
     * the last consensus round.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> serverInfoGauge_;
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
    void
    registerTxqGauge();
    void
    registerObjectCountGauge();
    void
    registerLoadFactorGauge();
    void
    registerNodeStoreGauge();
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
#endif                                   // XRPL_ENABLE_TELEMETRY
};

}  // namespace xrpl::telemetry
