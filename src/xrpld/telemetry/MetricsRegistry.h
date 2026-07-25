#pragma once

/**
 * Central OTel Metrics Registry for xrpld.
 *
 * Owns all OpenTelemetry metric instruments (counters, histograms,
 * observable gauges) that are NOT already covered by the beast::insight
 * StatsD pipeline. The instruments are created once at startup and polled
 * by the OTel PeriodicExportingMetricReader at a configurable interval
 * (default 10 s).
 *
 * When XRPL_ENABLE_TELEMETRY is **not** defined, this class compiles to a
 * lightweight no-op: every public method is an empty inline.
 *
 * Dependency / ownership diagram (ASCII):
 *
 * Application
 * |
 * +-- MetricsRegistry  (unique_ptr, created in setup(), started/stopped with telemetry)
 * |
 * +-- OTel MeterProvider  (owns reader + exporter)
 * |       |
 * |       +-- PeriodicExportingMetricReader
 * |       +-- OtlpHttpMetricExporter
 * |
 * +-- Counters / Histograms   (synchronous instruments)
 * |       +-- rpc_method_started_total
 * |       +-- rpc_method_finished_total
 * |       +-- rpc_method_errored_total
 * |       +-- rpc_method_us (Histogram)
 * |       +-- job_queued_total
 * |       +-- job_started_total
 * |       +-- job_finished_total
 * |       +-- job_queued_us (Histogram)
 * |       +-- job_running_us (Histogram)
 * |       +-- ledgers_closed_total
 * |       +-- validations_sent_total
 * |       +-- validations_checked_total
 * |       +-- ledger_history_mismatch_total{reason}
 * |       +-- txq_expired_total
 * |       +-- txq_dropped_total{reason}
 * |
 * +-- ValidationTracker  (validation agreement tracker)
 * |
 * +-- Observable Gauges  (async callbacks, polled by reader)
 * +-- Cache hit rates  (SLE, ledger, AL)
 * +-- TreeNode / FullBelow sizes
 * +-- TxQ metrics
 * +-- CountedObject counts
 * +-- Load factor breakdown
 * +-- NodeStore I/O gauges
 * +-- Server info (state, uptime, peers, consensus)
 * +-- Build info (version label)
 * +-- Complete ledger ranges (start/end pairs)
 * +-- DB metrics (storage KB, fetch rate)
 * +-- Validator health (amend blocked, UNL, quorum)
 * +-- Peer quality (P90 latency, version spread)
 * +-- Reduce-relay efficiency (selected/suppressed peers)
 * +-- Ledger economy (fees, reserves, age)
 * +-- State tracking (mode value, time in state)
 * +-- Storage detail (NuDB sizes)
 * +-- Validation agreement (1h/24h pct, counts)
 * +-- UNL quorum (trusted keys vs required quorum)
 * +-- Clock close offset (local clock skew)
 * +-- Sync state (time to first FULL, network-ledger gate,
 * |               server stall seconds, ledgers behind network)
 * +-- JobQueue backlog (waiting/running/deferred per job type)
 * +-- JobQueue saturation (running tasks vs worker threads vs backlog)
 * +-- jq_trans_overflow_total (observed from Overlay)
 * +-- server_stall_events_total (observed from LoadManager)
 *
 * Control-flow for async gauges:
 *
 * PeriodicExportingMetricReader (background thread, 10 s tick)
 * |
 * v
 * OTel SDK invokes registered ObservableGauge callbacks
 * |
 * v
 * Each callback reads current value from Application services
 * (e.g. app.getTxQ().getMetrics(), app.getFeeTrack().getLoadFactor())
 * |
 * v
 * Result set is exported via OTLP/HTTP to the collector
 *
 * Control-flow for synchronous instruments:
 *
 * PerfLogImp::rpcStart/rpcEnd/jobQueue/jobStart/jobFinish
 * |
 * v
 * MetricsRegistry::recordRpc*(method, ...) / recordJob*(type, ...)
 * |
 * v
 * OTel Counter::Add() or Histogram::Record()
 * |
 * v
 * Periodically flushed by the MetricReader
 *
 * Example usage:
 *
 * @code
 * // In Application::setup(), after telemetry_ is created:
 * metricsRegistry_ = std::make_unique<telemetry::MetricsRegistry>(
 * telemetry_->isEnabled(), app, journal);
 * metricsRegistry_->start(setup.exporterEndpoint);
 *
 * // In PerfLogImp::rpcStart():
 * if (auto* mr = app_.getMetricsRegistry())
 * mr->recordRpcStarted("server_info");
 *
 * // In PerfLogImp::rpcEnd():
 * if (auto* mr = app_.getMetricsRegistry())
 * {
 * mr->recordRpcFinished("server_info", durationUs);
 * // or: mr->recordRpcErrored("server_info", durationUs);
 * }
 *
 * // In PerfLogImp::jobQueue():
 * if (auto* mr = app_.getMetricsRegistry())
 * mr->recordJobQueued("ledgerData");
 *
 * // Shutdown:
 * metricsRegistry_->stop();
 * @endcode
 *
 * Caveats:
 * - The MetricsRegistry must be created AFTER the Telemetry object because
 * it reads isEnabled() to decide whether to initialize the OTel SDK.
 * - Observable gauge callbacks capture a reference to the Application; the
 * Application must outlive the MetricsRegistry (guaranteed because
 * MetricsRegistry is stopped before Application teardown).
 * - If a new CountedObject type is added, it will NOT appear automatically
 * in the object_count gauge; the callback iterates a fixed list.
 * - Adding a new synchronous instrument requires updating both the header
 * and the .cpp, then calling the new record*() method from the
 * instrumentation site.
 */

#include <xrpld/telemetry/ValidationTracker.h>

#include <xrpl/beast/utility/Journal.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#ifdef XRPL_ENABLE_TELEMETRY
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/unique_ptr.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#endif

namespace xrpl {

class ServiceRegistry;

namespace telemetry {

/**
 * Central OpenTelemetry metric registry.
 *
 * Owns all OTel instruments (counters, histograms, observable gauges)
 * that are not covered by the beast::insight StatsD pipeline. See the
 * file-level header comment above for the full dependency diagram,
 * gauge domain list, and usage examples.
 *
 * Class / collaborator diagram (ASCII):
 *
 * +-----------------+        +-------------------+
 * |   Application   |------->|  MetricsRegistry  |
 * +-----------------+        +-------------------+
 * |       |        |
 * creates/owns    v       v        v
 * +-----------+  +---------+  +-------------------+
 * | Meter     |  | Counter |  | ValidationTracker |
 * | Provider  |  | /Hist.  |  | (rolling windows) |
 * +-----------+  +---------+  +-------------------+
 * |
 * v
 * Periodic reader thread (~10 s)
 * -> ObservableGauge callbacks
 * -> OTLP/HTTP export
 *
 * @note Thread safety:
 * - The recordRpc, recordJob, and increment methods are invoked
 * from xrpld hot paths. OTel Counter::Add() and
 * Histogram::Record() are documented thread-safe, and
 * null-guard checks protect uninitialized instruments.
 * - ObservableGauge callbacks run on the OTel SDK background
 * reader thread (~10 s tick), concurrently with writers.
 * Each callback reads only lock-protected or atomic state
 * from Application services and wraps the body in a
 * catch-all try block so a transient failure never crashes
 * the reader thread.
 * - ValidationTracker protects its rolling windows internally.
 * - start() and stop() are NOT thread-safe with each other and
 * must be called from the single Application lifecycle
 * thread.
 *
 * @note Lifetime:
 * - Must be constructed AFTER telemetry_ (reads isEnabled()).
 * - Must be stopped BEFORE Application services it observes are
 * destroyed; the Application owns it via unique_ptr so normal
 * teardown guarantees this.
 *
 * @note Extending:
 * - Adding a new CountedObject type is auto-picked up by the
 * object_count gauge via iteration.
 * - Adding a new SYNCHRONOUS instrument (counter/histogram): prefer the
 * XRPL_METRIC_* call-site macros in MetricMacros.h -- no header/cpp
 * edit needed. Fall back to a dedicated member + init line + record
 * method (the pattern below) only when the metric needs to be read
 * back by other code (e.g. ValidationTracker-style accumulation) or
 * needs a custom histogram bucket View (see MetricMacros.h Limitation
 * 2 in tasks/metric-macro-plan.md).
 * - Adding a new OBSERVABLE gauge still requires eager central
 * registration -- pull-model instruments cannot be lazily created.
 */
class MetricsRegistry
{
public:
    /**
     * Construct a MetricsRegistry.
     *
     * @param enabled  Whether OTel metric export is active. When false,
     * all methods become no-ops.
     * @param app      Reference to the ServiceRegistry (Application) for
     * reading current metric values in gauge callbacks.
     * @param journal  Journal for log output.
     */
    MetricsRegistry(bool enabled, ServiceRegistry& app, beast::Journal journal);

    ~MetricsRegistry();

    /**
     * Non-copyable, non-movable.
     */
    MetricsRegistry(MetricsRegistry const&) = delete;
    MetricsRegistry&
    operator=(MetricsRegistry const&) = delete;

    /**
     * Initialize the OTel metrics pipeline and register all instruments.
     *
     * @param endpoint    OTLP/HTTP endpoint URL for metric export
     * (e.g. "http://localhost:4318/v1/metrics").
     * @param instanceId  Value for the service.instance.id resource
     * attribute. When non-empty, Prometheus metrics
     * carry a service_instance_id label for per-node
     * filtering.
     */
    void
    start(std::string const& endpoint, std::string const& instanceId = {});

    /**
     * Detach all ObservableGauge callbacks so they no-op on the next
     * reader-thread tick.
     *
     * Must be called BEFORE any Application service that the callbacks
     * read (nodeStore, overlay, networkOPs, ledgerMaster, etc.) is
     * stopped. The flag is checked with acquire ordering at the top of
     * every callback; together with the release store here it
     * guarantees that once `detachCallbacks()` returns, no subsequent
     * callback invocation will dereference an already-stopped service.
     *
     * Idempotent. Safe to call multiple times. Safe to call before
     * `start()` (has no effect). The actual SDK-level provider
     * shutdown still happens in `stop()`.
     */
    void
    detachCallbacks() noexcept;

    /**
     * Flush pending metrics and shut down the pipeline.
     *
     * @pre `detachCallbacks()` should have been called earlier in the
     * shutdown sequence; otherwise there is a narrow race between
     * the final reader-thread tick and the destruction of
     * Application services that the gauge callbacks read from.
     */
    void
    stop();

    /**
     * @return true if the registry is actively exporting metrics.
     */
    [[nodiscard]] bool
    isEnabled() const noexcept
    {
        return enabled_;
    }

    // -----------------------------------------------------------------
    // Synchronous instrument recording (called from PerfLog hot paths)
    // -----------------------------------------------------------------

    /**
     * Record an RPC method call start.
     * @param method  The RPC method name (e.g. "server_info").
     */
    void
    recordRpcStarted(std::string_view method);

    /**
     * Record an RPC method call completion.
     * @param method      The RPC method name.
     * @param durationUs  Execution time in microseconds.
     */
    void
    recordRpcFinished(std::string_view method, std::int64_t durationUs);

    /**
     * Record an RPC method call error.
     * @param method      The RPC method name.
     * @param durationUs  Execution time in microseconds.
     */
    void
    recordRpcErrored(std::string_view method, std::int64_t durationUs);

    /**
     * Record a job enqueued event.
     * @param jobType  The job type name (e.g. "ledgerData").
     */
    void
    recordJobQueued(std::string_view jobType);

    /**
     * Record a job start event.
     * @param jobType        The job type name.
     * @param queuedDurUs   Time the job spent waiting in the queue (us).
     */
    void
    recordJobStarted(std::string_view jobType, std::int64_t queuedDurUs);

    /**
     * Record a job finish event.
     * @param jobType         The job type name.
     * @param runningDurUs   Execution time in microseconds.
     */
    void
    recordJobFinished(std::string_view jobType, std::int64_t runningDurUs);

    // -----------------------------------------------------------------
    // External dashboard parity counters (Tasks 7.9-7.14)
    // -----------------------------------------------------------------

    /**
     * Increment the ledgers_closed_total counter.
     *
     * @note Currently has no callers: the ledgers_closed_total counter is
     * incremented at its consensus call site via the XRPL_METRIC_COUNTER_INC
     * macro (see MetricMacros.h). This method and its eagerly-created
     * counter are retained as a fallback and are slated for removal in a
     * separate cleanup once the macro path has proven out.
     */
    void
    incrementLedgersClosed();

    /**
     * Increment the validations_sent_total counter.
     * Called from RCLConsensus::Adaptor::validate() when a validation
     * is produced and broadcast.
     */
    void
    incrementValidationsSent();

    /**
     * Increment the validations_checked_total counter.
     * Called from NetworkOPs::recvValidation() when a network validation
     * is received and checked.
     */
    void
    incrementValidationsChecked();

    /**
     * Increment the ledger_history_mismatch_total counter for a reason.
     * Called from LedgerHistory::handleMismatch() once the mismatch has
     * been classified. The reason label turns fork diagnosis from a
     * log-grep into a queryable time series.
     * @param reason Classified mismatch cause (e.g. "prior_ledger",
     * "close_time", "consensus_txset", "same_txset_diff_result",
     * "unknown").
     */
    void
    incrementLedgerHistoryMismatch(std::string_view reason);

    /**
     * Increment the txq_expired_total counter.
     * Called from TxQ::processClosedLedger() for each queued transaction
     * removed because its LastLedgerSequence has passed — submitters who
     * under-bid the escalating fee and were never included.
     */
    void
    incrementTxqExpired();

    /**
     * Increment the txq_dropped_total{reason} counter.
     * Called from TxQ::apply() when a transaction is refused admission to
     * the queue (e.g. the queue is full). Distinct from expiry (already
     * queued) and from jq_trans_overflow (job queue, not TxQ).
     * @param reason Admission-control rejection cause (e.g. "queue_full").
     */
    void
    incrementTxqDropped(std::string_view reason);

    /**
     * Access the validation agreement tracker.
     * Used by consensus and ledger hooks to record our validations and
     * network validations so the tracker can compute agreement percentages.
     * @return Reference to the internal ValidationTracker instance.
     */
    ValidationTracker&
    getValidationTracker()
    {
        return validationTracker_;
    }

#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * Access the shared OTel Meter for call-site instrument creation.
     * Used by the XRPL_METRIC_* macros (MetricMacros.h) so new synchronous
     * counters/histograms can be declared at their call site instead of as
     * MetricsRegistry members. Returns an empty (falsy) shared_ptr before
     * start() has run or when disabled.
     * @return The shared Meter, or empty if not yet started.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
    meter() const noexcept
    {
        return meter_;
    }
#endif

private:
    /**
     * Master enable flag; when false all methods are no-ops.
     */
    bool const enabled_;

    /**
     * Tracks validation agreement between this node and the network.
     * Lives outside the XRPL_ENABLE_TELEMETRY guard because it is
     * always safe to record events; the gauge callback simply won't
     * fire when telemetry is disabled.
     */
    ValidationTracker validationTracker_;

#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * Reference to Application services for gauge callbacks.
     * Only needed when OTel is compiled in, since observable gauge
     * callbacks live entirely inside the XRPL_ENABLE_TELEMETRY guard.
     */
    ServiceRegistry& app_;

    /**
     * Journal for logging.
     */
    beast::Journal const journal_;

    /**
     * Set by detachCallbacks() during shutdown so every ObservableGauge
     * callback returns early before reading Application services that
     * may already be stopped. Checked with memory_order_acquire at the
     * top of each callback to pair with the memory_order_release store
     * in detachCallbacks().
     */
    std::atomic<bool> callbacksDetached_{false};

    /**
     * The SDK MeterProvider that owns the export pipeline.
     */
    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> provider_;

    /**
     * The Meter used to create all instruments.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter_;

    // --- Synchronous instruments (RPC) ---
    /**
     * Counter: rpc_method_started_total{method="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> rpcStartedCounter_;
    /**
     * Counter: rpc_method_finished_total{method="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> rpcFinishedCounter_;
    /**
     * Counter: rpc_method_errored_total{method="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> rpcErroredCounter_;
    /**
     * Histogram: rpc_method_duration_us{method="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        rpcDurationHistogram_;

    // --- Synchronous instruments (Job Queue) ---
    /**
     * Counter: job_queued_total{job_type="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobQueuedCounter_;
    /**
     * Counter: job_started_total{job_type="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobStartedCounter_;
    /**
     * Counter: job_finished_total{job_type="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobFinishedCounter_;
    /**
     * Histogram: job_queued_duration_us{job_type="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        jobQueuedDurationHistogram_;
    /**
     * Histogram: job_running_duration_us{job_type="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        jobRunningDurationHistogram_;

    // --- Observable gauges (registered via callbacks) ---
    // Handles are stored so we can remove callbacks on shutdown.
    /**
     * Observable gauges for cache hit rates and sizes.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        cacheHitRateGauge_;
    /**
     * Observable gauges for TxQ metrics.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> txqGauge_;
    /**
     * Observable gauges for counted object instances.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        objectCountGauge_;
    /**
     * Observable gauges for load factor breakdown.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> loadFactorGauge_;
    /**
     * Observable gauges for NodeStore write_load and read_queue.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> nodeStoreGauge_;
    /**
     * Observable gauge for server-level health metrics (state, uptime, peers, etc.).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> serverInfoGauge_;
    /**
     * Observable gauge for trusted UNL key count against the required quorum.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> unlQuorumGauge_;
    /**
     * Observable gauge for the network close-time offset (local clock skew).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> clockSkewGauge_;
    /**
     * Observable gauge for the sync-pipeline state signals (time to first
     * FULL, network-ledger gate, server stall, ledgers behind the network).
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
     * Observable gauge for aggregate ledger-acquire progress (max missing state
     * and tx nodes, received-data stash depth, in-flight acquire count).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        syncAcquireGauge_;
    /**
     * Observable gauge for the SHAMap tree-node cache hit rate, which is the
     * memory layer above the node store's own hit ratio.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        shamapCacheHitRateGauge_;
    /**
     * Observable gauge for per-job-type JobQueue occupancy: waiting, running
     * and deferred counts, keyed by job type.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        jobQueueBacklogGauge_;
    /**
     * Observable gauge for global worker-pool saturation: tasks in flight,
     * configured worker threads, and total jobs queued.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        jobQueueSaturationGauge_;
    /**
     * Observable gauge for build version info (label-based, value=1).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> buildInfoGauge_;
    /**
     * Observable gauge for complete ledger range start/end pairs.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        completeLedgersGauge_;
    /**
     * Observable gauge for database sizes and historical fetch rate.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> dbMetricsGauge_;

    // --- External dashboard parity gauges (Tasks 7.9-7.13) ---
    /**
     * Observable gauge for validator health indicators (amendment blocked,
     * UNL blocked, quorum, UNL expiry).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validatorHealthGauge_;
    /**
     * Observable gauge for peer network quality metrics (P90 latency,
     * insane peer count, version spread, upgrade recommendation).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        peerQualityGauge_;
    /**
     * Observable gauge for transaction reduce-relay efficiency (selected vs
     * suppressed peers, feature-disabled peers, missing-tx frequency).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        reduceRelayGauge_;
    /**
     * Observable gauge for ledger economy metrics (base fee, reserve,
     * reserve increment, ledger age).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        ledgerEconomyGauge_;
    /**
     * Observable gauge for node state tracking (operating mode value,
     * time in current state).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        stateTrackingGauge_;
    /**
     * Observable gauge for storage detail metrics (NuDB on-disk size).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        storageDetailGauge_;
    /**
     * Observable gauge for validation agreement metrics (1h/24h percentages
     * and counts from ValidationTracker).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validationAgreementGauge_;

    // --- External dashboard parity counters (Task 7.14) ---
    /**
     * Counter: ledgers_closed_total — incremented each consensus round.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        ledgersClosedCounter_;
    /**
     * Counter: validations_sent_total — incremented when this node sends a validation.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        validationsSentCounter_;
    /**
     * Counter: validations_checked_total — incremented for each network validation
     * received.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        validationsCheckedCounter_;
    /**
     * ObservableCounter: jq_trans_overflow_total — observed from
     * Overlay::getJqTransOverflow() (cumulative overflow tally owned by the overlay).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        jqTransOverflowObservable_;
    /**
     * Counter: ledger_history_mismatch_total{reason} — incremented per classified
     * built-vs-validated ledger mismatch.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        ledgerHistoryMismatchCounter_;
    /**
     * Counter: txq_expired_total — incremented per transaction expired out of the
     * transaction queue.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> txqExpiredCounter_;
    /**
     * Counter: txq_dropped_total{reason} — incremented when a transaction is refused
     * admission to the queue.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> txqDroppedCounter_;
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
     * Build the OTLP/HTTP exporter, periodic reader, resource attributes and
     * histogram views, then create the MeterProvider and meter. Extracted
     * from start() to keep each function under the 80-line limit.
     *
     * @param endpoint OTLP/HTTP metrics endpoint URL.
     * @param instanceId service.instance.id resource attribute (may be empty).
     */
    void
    initExporterAndProvider(std::string const& endpoint, std::string const& instanceId);

    /**
     * Create the synchronous instruments (RPC and job-queue counters and
     * histograms, plus the external dashboard parity counters). Extracted
     * from start() to keep each function under the 80-line limit.
     */
    void
    initSyncInstruments();

    /**
     * Register all observable gauge callbacks with the OTel SDK.
     * Dispatches to one helper per metric domain so that each helper
     * stays well under the 80-line-per-function limit.
     */
    void
    registerAsyncGauges();

    // Per-domain gauge registration helpers. Each creates its instrument
    // and attaches a single ObservableGauge callback that reads current
    // values from Application services. The callbacks run on the OTel
    // PeriodicExportingMetricReader background thread (~10 s tick).
    void
    registerCacheHitRateGauge();  // Task 9.2
    void
    registerTxqGauge();  // Task 9.3
    void
    registerObjectCountGauge();  // Task 9.6
    void
    registerLoadFactorGauge();  // Task 9.7
    void
    registerNodeStoreGauge();  // Task 9.1
    void
    registerServerInfoGauge();  // Task 9.7a
    void
    registerBuildInfoGauge();  // Task 9.7b
    void
    registerCompleteLedgersGauge();  // Task 9.7c
    void
    registerDbMetricsGauge();  // Task 9.7d
    void
    registerValidatorHealthGauge();  // Task 7.9
    void
    registerPeerQualityGauge();  // Task 7.10
    void
    registerReduceRelayGauge();  // Reduce-relay efficiency
    void
    registerLedgerEconomyGauge();  // Task 7.11
    void
    registerStateTrackingGauge();  // Task 7.12
    void
    registerStorageDetailGauge();  // Task 7.13
    void
    registerValidationAgreementGauge();  // Task 7.15
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
     * that previously existed only in a log line or in server_info JSON:
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
     * Register the `jobq_backlog` gauge.
     *
     * Three series per job type, from one JobQueue::getJobTypeCounts()
     * snapshot, under the `metric` and `job_type` attributes:
     *
     *   `waiting` — jobs enqueued and not yet dispatched to a worker.
     *   `running` — jobs executing on a worker.
     *   `deferred` — **the signal this gauge exists for.** Jobs held back
     *     because the type is already at its concurrency limit. The
     *     sync-critical types run at limits of 3 (`JtLedgerReq`,
     *     `JtLedgerData` in JobTypes.h), so during a fresh sync those types
     *     routinely have work denied a worker, and that state appears in
     *     neither `waiting` nor `running`.
     *
     * Distinct from the job metrics that already exist. `job_queued_total` /
     * `job_started_total` / `job_finished_total` and the `job_queued_us` /
     * `job_running_us` histograms are all event-driven and come from
     * PerfLogImp: they describe jobs that already moved. This gauge is
     * instantaneous occupancy — what is sitting in the queue right now, which
     * a rate or a latency quantile cannot express. The StatsD
     * `jobq_job_count` gauge is queue-wide only, with no per-type split and
     * no deferred count at all.
     *
     * `job_type` is the JobTypes::name() string, matching the label the job
     * counters already use so the two can be joined. Cardinality is bounded
     * by the JobType enum (~46 values), and every type is observed on every
     * tick, so an idle type reports 0 rather than dropping its series.
     *
     * @note Pulled on the OTel reader thread (~10 s tick), never on a hot
     * path. Takes the JobQueue mutex once per tick for three integer reads
     * per type; no per-job cost is added anywhere.
     */
    void
    registerJobQueueBacklogGauge();  // sync diagnostics: per-type backlog

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
     * The reason this is separate from `jobq_backlog`: when the pool itself
     * is exhausted, every subsystem waiting behind it looks independently
     * slow, and each per-type panel invites the wrong conclusion. A
     * `running_tasks / worker_threads` ratio at 1.0 with a non-zero
     * `total_waiting` attributes the whole slowdown to pool exhaustion once.
     *
     * @note Pulled on the OTel reader thread (~10 s tick). One atomic load,
     * one plain int read, and one pass over the per-type counters under the
     * JobQueue mutex.
     */
    void
    registerJobQueueSaturationGauge();  // sync diagnostics: pool saturation
#endif                                  // XRPL_ENABLE_TELEMETRY
};

}  // namespace telemetry
}  // namespace xrpl
