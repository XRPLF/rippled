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
 * |       +-- job_queued_total{job_type,handler}
 * |       +-- job_started_total{job_type,handler}
 * |       +-- job_finished_total{job_type,handler}
 * |       +-- job_queued_us{job_type,handler} (Histogram)
 * |       +-- job_running_us{job_type,handler} (Histogram)
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
 * +-- JobQueue saturation (running tasks vs worker threads vs backlog)
 * +-- Peer ledger supply (how many peers can serve the needed sequence)
 * +-- PeerFinder slot census (slots, attempts, fixed peers, address caches)
 * +-- Amendment block (warned flag + seconds until the node stops validating)
 * +-- NodeStore latency (mean us per store and per fetch, with counts)
 * +-- Ledger quorum + publish (validation tally vs quorum target,
 * |                            time to first validated, publish lag)
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
 * // In PerfLogImp::jobQueue(). The second argument is the addJob name;
 * // it is sanitised internally into the bounded `handler` label.
 * if (auto* mr = app_.getMetricsRegistry())
 * mr->recordJobQueued("ledgerData", "ProcessLData");
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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <ranges>
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
     * The `handler` label value used for any job name that fails the
     * sanitiser's all-ASCII-letters rule.
     *
     * Public because both sanitiseHandler() and its unit tests must agree
     * on the exact fallback token; a test asserting against its own copy
     * of the string would not catch a change made here.
     *
     * Declared as std::string_view rather than the `constexpr char k[]`
     * form used for instrument names in MetricsRegistry.cpp: this value is
     * *returned* by sanitiseHandler(), whose return type is
     * std::string_view, and is compared against std::string_view in tests.
     * Matching the type avoids array-to-pointer decay and a needless
     * strlen at each use.
     */
    static constexpr std::string_view kHandlerOther{"other"};

    /**
     * Reduce a job name to a bounded-cardinality `handler` label value.
     *
     * A job type can have several producers — both `RcvGetLedger` and
     * `RcvGetObjByHash` run as `JtLedgerReq` — so `job_type` alone cannot
     * attribute a latency spike to one of them. The job name can, but it
     * cannot be used raw: two names embed a ledger sequence number
     * (`"Pub" + std::to_string(seq)` in LedgerPersistence.cpp and
     * `"OB" + std::to_string(...)` in OrderBookDBImpl.cpp), which would
     * mint a fresh Prometheus series for every ledger.
     *
     * The rule is therefore: keep the name only when it is non-empty and
     * every character is an ASCII letter; otherwise return `"other"`.
     * Both dynamic names always contain digits, so they always fold to
     * `"other"`, while every all-letter name is a compile-time literal.
     * The label domain is thus a function of the literals present in the
     * source — 43 names plus `"other"` at the time of writing — and
     * cannot grow at runtime. A name added later that does not satisfy
     * the rule degrades to `"other"` rather than becoming unbounded,
     * which is a stronger guarantee than an allowlist that would have to
     * be maintained by hand.
     *
     * Defined inline so unit tests can call it without linking the rest
     * of the registry: in a telemetry-enabled build MetricsRegistry.cpp
     * is not compiled into the test binary, so an out-of-line definition
     * would be unreachable from tests. Being inline also makes it usable
     * regardless of XRPL_ENABLE_TELEMETRY.
     *
     * @param name  The job name as passed to JobQueue::addJob.
     * @return @p name when it is non-empty and all ASCII letters, else
     * kHandlerOther.
     *
     * @note Pure and reentrant: holds no state, performs no I/O, and is
     * safe to call concurrently from any thread.
     * @note The letter test is an explicit ASCII range check rather than
     * std::isalpha, which classifies by the current C locale. A
     * locale-dependent test could admit non-ASCII bytes and so
     * weaken the cardinality bound this function exists to provide.
     * @note When the name is kept, the returned view aliases @p name, so
     * it must not outlive the caller's buffer. The kHandlerOther case
     * returns a view of a static constant and is always valid.
     *
     * Example:
     * @code
     * sanitiseHandler("RcvGetObjByHash");  // "RcvGetObjByHash"
     * sanitiseHandler("Pub94512331");      // kHandlerOther  (digits)
     * sanitiseHandler("");                 // kHandlerOther  (empty)
     * @endcode
     */
    [[nodiscard]] static constexpr std::string_view
    sanitiseHandler(std::string_view name) noexcept
    {
        auto const isAsciiLetter = [](char const c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        };

        if (name.empty() || !std::ranges::all_of(name, isAsciiLetter))
            return kHandlerOther;

        return name;
    }

    /**
     * Record a job enqueued event.
     * @param jobType  The job type name (e.g. "ledgerData").
     * @param jobName  The addJob name, reduced to a bounded `handler`
     * label by sanitiseHandler(). Distinguishes producers
     * that share a job type.
     */
    void
    recordJobQueued(std::string_view jobType, std::string_view jobName);

    /**
     * Record a job start event.
     * @param jobType        The job type name.
     * @param jobName        The addJob name; see recordJobQueued().
     * @param queuedDurUs   Time the job spent waiting in the queue (us).
     */
    void
    recordJobStarted(std::string_view jobType, std::string_view jobName, std::int64_t queuedDurUs);

    /**
     * Record a job finish event.
     * @param jobType         The job type name.
     * @param jobName         The addJob name; see recordJobQueued().
     * @param runningDurUs   Execution time in microseconds.
     */
    void
    recordJobFinished(
        std::string_view jobType,
        std::string_view jobName,
        std::int64_t runningDurUs);

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
    // All five carry handler="<sanitised addJob name>" in addition to
    // job_type, so producers that share a job type stay distinguishable.
    /**
     * Counter: job_queued_total{job_type="<name>",handler="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobQueuedCounter_;
    /**
     * Counter: job_started_total{job_type="<name>",handler="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobStartedCounter_;
    /**
     * Counter: job_finished_total{job_type="<name>",handler="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobFinishedCounter_;
    /**
     * Histogram: job_queued_duration_us{job_type="<name>",handler="<name>"}
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        jobQueuedDurationHistogram_;
    /**
     * Histogram: job_running_duration_us{job_type="<name>",handler="<name>"}
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
     * Observable gauge for global worker-pool saturation: tasks in flight,
     * configured worker threads, and total jobs queued.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        jobQueueSaturationGauge_;
    /**
     * Observable gauge for how much of the needed ledger range the connected
     * peer set can actually serve.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        peerLedgerSupplyGauge_;
    /**
     * Observable gauge for PeerFinder slot occupancy, connection attempts,
     * fixed peers and address-cache depth.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> slotCensusGauge_;
    /**
     * Observable gauge for the amendment-block warning flag and the countdown
     * to the amendment activating.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        amendmentBlockGauge_;
    /**
     * Observable gauge for node-store read and write latency, as mean
     * microseconds per operation derived from the cumulative duration and
     * operation-count totals the node store already keeps.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        nodeStoreLatencyGauge_;
    /**
     * Observable gauge for the pre-accept quorum gate and the publish lag:
     * the trusted-validation tally against the quorum it must reach, the
     * time to the first fully-validated ledger, and how far publishing
     * trails validation.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        ledgerQuorumPublishGauge_;
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
     * Register the `nodestore_latency` gauge.
     *
     * Four series under the `metric` attribute, from the node store's own
     * cumulative totals:
     *
     *   `write_mean_us` — **the signal this gauge exists for.** Mean
     *     microseconds per store, `getStoreDurationUs() / getStoreCount()`.
     *     No write-side latency existed anywhere before this:
     *     `storeDurationUs_` was declared in Database.h and never written, and
     *     there was no accessor for it. This is the fingerprint of the
     *     "a node with a large existing DB syncs slower than a fresh one"
     *     symptom, which is write-bound and therefore invisible in every
     *     read-side metric.
     *   `read_mean_us` — mean microseconds per fetch,
     *     `getFetchDurationUs() / getFetchTotalCount()`, so the write mean has
     *     a same-instant, same-derivation counterpart to be compared against.
     *   `write_count` / `read_count` — the denominators, exported so a
     *     dashboard can recover *interval* latency as
     *     `rate(duration) / rate(count)`. Without them the means above are
     *     since-boot averages, which on a long-running node move so slowly
     *     that a current stall is invisible.
     *
     * Gauge, not a histogram — deliberate. A histogram would give true
     * percentiles, which a mean cannot, but it costs one `Record()` per
     * operation on a path that runs per node object: a single ledger write
     * walks thousands of SHAMap nodes, and fetches are more frequent still.
     * That is a per-object synchronous instrument call plus bucket search on
     * the hot store/fetch path. This gauge instead reads four already-existing
     * atomics once per ~10 s collection tick, adding nothing whatsoever to the
     * hot path — the store side pays only the one clock-sample pair per store
     * that the read side has always paid per fetch. For the question this work
     * package answers ("is the write path slow, and slower than the read
     * path?") a rate-derived mean is sufficient, and a tail latency that
     * matters will move the mean. Consequence, stated plainly: p99 is NOT
     * obtainable from this signal. Adding a histogram later would also require
     * an explicit-bucket View registered in initExporterAndProvider() via
     * addMicrosecondHistogramView(), because the SDK's default buckets top out
     * at 10,000 and every microsecond duration above 10 ms would saturate.
     *
     * Distinct from `nodestore_state`, which already carries the raw
     * cumulative `node_reads_duration_us`, `node_reads_total` and
     * `node_writes` fields, and from the Ledger Data Sync dashboard's "NuDB
     * Read Latency" panel that divides the first two in PromQL. Neither has
     * any write-duration input to divide — that quantity did not exist. This
     * gauge adds the missing write numerator and publishes both means from one
     * reading so the two sides are directly comparable.
     *
     * @note Pulled on the OTel reader thread (~10 s tick). Four relaxed atomic
     * loads and two integer divisions; no lock, no allocation, no hot-path
     * cost.
     * @note A mean is observed only when both its count and its duration total
     * are non-zero; otherwise the series is omitted rather than reported as 0,
     * because a 0 would claim the operation is instantaneous. The counts are
     * always observed, so `write_count` still distinguishes "nothing written
     * yet" from "writes are instant".
     * @warning `write_mean_us` is currently produced only by store paths that
     * call `Database::recordStoreDuration()`, which today is
     * `Database::importInternal` (the `[import_db]` admin path). `store()` is
     * pure virtual, and neither `DatabaseNodeImp::store` nor
     * `DatabaseRotatingImp::store` calls it yet, so on an ordinary node
     * `write_count` climbs while `write_mean_us` is absent. That is a
     * deliberate, visible gap: closing it means adding one clock-sample pair to
     * those two concrete store overrides, which live outside this work
     * package's file scope.
     * @note Both totals are monotonic and never reset. A panel wanting current
     * rather than since-boot latency must divide the two rates, which is why
     * the counts are exported alongside the means.
     */
    void
    registerNodeStoreLatencyGauge();  // sync diagnostics: store/fetch latency

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

}  // namespace telemetry
}  // namespace xrpl
