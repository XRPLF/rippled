#pragma once

/** Central OTel Metrics Registry for rippled.

    Owns all OpenTelemetry metric instruments (counters, histograms,
    observable gauges) that are NOT already covered by the beast::insight
    StatsD pipeline. The instruments are created once at startup and polled
    by the OTel PeriodicExportingMetricReader at a configurable interval
    (default 10 s).

    When XRPL_ENABLE_TELEMETRY is **not** defined, this class compiles to a
    lightweight no-op: every public method is an empty inline.

    Dependency / ownership diagram (ASCII):

        Application
            |
            +-- MetricsRegistry  (unique_ptr, created in setup(), started/stopped with telemetry)
                    |
                    +-- OTel MeterProvider  (owns reader + exporter)
                    |       |
                    |       +-- PeriodicExportingMetricReader
                    |       +-- OtlpHttpMetricExporter
                    |
                    +-- Counters / Histograms   (synchronous instruments)
                    |       +-- rippled_rpc_method_started_total
                    |       +-- rippled_rpc_method_finished_total
                    |       +-- rippled_rpc_method_errored_total
                    |       +-- rippled_rpc_method_duration_us (Histogram)
                    |       +-- rippled_job_queued_total
                    |       +-- rippled_job_started_total
                    |       +-- rippled_job_finished_total
                    |       +-- rippled_job_queued_duration_us (Histogram)
                    |       +-- rippled_job_running_duration_us (Histogram)
                    |       +-- rippled_ledgers_closed_total
                    |       +-- rippled_validations_sent_total
                    |       +-- rippled_validations_checked_total
                    |       +-- rippled_state_changes_total
                    |       +-- rippled_jq_trans_overflow_total
                    |
                    +-- ValidationTracker  (validation agreement tracker)
                    |
                    +-- Observable Gauges  (async callbacks, polled by reader)
                            +-- Cache hit rates  (SLE, ledger, AL)
                            +-- TreeNode / FullBelow sizes
                            +-- TxQ metrics
                            +-- CountedObject counts
                            +-- Load factor breakdown
                            +-- NodeStore I/O gauges
                            +-- Server info (state, uptime, peers, consensus)
                            +-- Build info (version label)
                            +-- Complete ledger ranges (start/end pairs)
                            +-- DB metrics (storage KB, fetch rate)
                            +-- Validator health (amend blocked, UNL, quorum)
                            +-- Peer quality (P90 latency, version spread)
                            +-- Ledger economy (fees, reserves, age)
                            +-- State tracking (mode value, time in state)
                            +-- Storage detail (NuDB sizes)
                            +-- Validation agreement (1h/24h pct, counts)

    Control-flow for async gauges:

        PeriodicExportingMetricReader (background thread, 10 s tick)
            |
            v
        OTel SDK invokes registered ObservableGauge callbacks
            |
            v
        Each callback reads current value from Application services
        (e.g. app.getTxQ().getMetrics(), app.getFeeTrack().getLoadFactor())
            |
            v
        Result set is exported via OTLP/HTTP to the collector

    Control-flow for synchronous instruments:

        PerfLogImp::rpcStart/rpcEnd/jobQueue/jobStart/jobFinish
            |
            v
        MetricsRegistry::recordRpc*(method, ...) / recordJob*(type, ...)
            |
            v
        OTel Counter::Add() or Histogram::Record()
            |
            v
        Periodically flushed by the MetricReader

    Example usage:

    @code
        // In Application::setup(), after telemetry_ is created:
        metricsRegistry_ = std::make_unique<telemetry::MetricsRegistry>(
            telemetry_->isEnabled(), app, journal);
        metricsRegistry_->start(setup.exporterEndpoint);

        // In PerfLogImp::rpcStart():
        if (auto* mr = app_.getMetricsRegistry())
            mr->recordRpcStarted("server_info");

        // In PerfLogImp::rpcEnd():
        if (auto* mr = app_.getMetricsRegistry())
        {
            mr->recordRpcFinished("server_info", durationUs);
            // or: mr->recordRpcErrored("server_info", durationUs);
        }

        // In PerfLogImp::jobQueue():
        if (auto* mr = app_.getMetricsRegistry())
            mr->recordJobQueued("ledgerData");

        // Shutdown:
        metricsRegistry_->stop();
    @endcode

    Caveats:
    - The MetricsRegistry must be created AFTER the Telemetry object because
      it reads isEnabled() to decide whether to initialize the OTel SDK.
    - Observable gauge callbacks capture a reference to the Application; the
      Application must outlive the MetricsRegistry (guaranteed because
      MetricsRegistry is stopped before Application teardown).
    - If a new CountedObject type is added, it will NOT appear automatically
      in the object_count gauge; the callback iterates a fixed list.
    - Adding a new synchronous instrument requires updating both the header
      and the .cpp, then calling the new record*() method from the
      instrumentation site.
*/

#include <xrpld/telemetry/ValidationTracker.h>

#include <xrpl/beast/utility/Journal.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#ifdef XRPL_ENABLE_TELEMETRY
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#endif

namespace xrpl {

class ServiceRegistry;

namespace telemetry {

class MetricsRegistry
{
public:
    /** Construct a MetricsRegistry.

        @param enabled  Whether OTel metric export is active. When false,
                        all methods become no-ops.
        @param app      Reference to the ServiceRegistry (Application) for
                        reading current metric values in gauge callbacks.
        @param journal  Journal for log output.
    */
    MetricsRegistry(bool enabled, ServiceRegistry& app, beast::Journal journal);

    ~MetricsRegistry();

    /// Non-copyable, non-movable.
    MetricsRegistry(MetricsRegistry const&) = delete;
    MetricsRegistry&
    operator=(MetricsRegistry const&) = delete;

    /** Initialize the OTel metrics pipeline and register all instruments.

        @param endpoint    OTLP/HTTP endpoint URL for metric export
                           (e.g. "http://localhost:4318/v1/metrics").
        @param instanceId  Value for the service.instance.id resource
                           attribute. When non-empty, Prometheus metrics
                           carry an exported_instance label for per-node
                           filtering.
    */
    void
    start(std::string const& endpoint, std::string const& instanceId = {});

    /** Flush pending metrics and shut down the pipeline. */
    void
    stop();

    /** @return true if the registry is actively exporting metrics. */
    bool
    isEnabled() const noexcept
    {
        return enabled_;
    }

    // -----------------------------------------------------------------
    // Synchronous instrument recording (called from PerfLog hot paths)
    // -----------------------------------------------------------------

    /** Record an RPC method call start.
        @param method  The RPC method name (e.g. "server_info").
    */
    void
    recordRpcStarted(std::string_view method);

    /** Record an RPC method call completion.
        @param method      The RPC method name.
        @param durationUs  Execution time in microseconds.
    */
    void
    recordRpcFinished(std::string_view method, std::int64_t durationUs);

    /** Record an RPC method call error.
        @param method      The RPC method name.
        @param durationUs  Execution time in microseconds.
    */
    void
    recordRpcErrored(std::string_view method, std::int64_t durationUs);

    /** Record a job enqueued event.
        @param jobType  The job type name (e.g. "ledgerData").
    */
    void
    recordJobQueued(std::string_view jobType);

    /** Record a job start event.
        @param jobType        The job type name.
        @param queuedDurUs   Time the job spent waiting in the queue (us).
    */
    void
    recordJobStarted(std::string_view jobType, std::int64_t queuedDurUs);

    /** Record a job finish event.
        @param jobType         The job type name.
        @param runningDurUs   Execution time in microseconds.
    */
    void
    recordJobFinished(std::string_view jobType, std::int64_t runningDurUs);

    // -----------------------------------------------------------------
    // External dashboard parity counters (Tasks 7.9-7.14)
    // -----------------------------------------------------------------

    /** Increment the ledgers_closed_total counter.
        Called from RCLConsensus::Adaptor::doAccept() after a ledger is
        accepted by consensus.
    */
    void
    incrementLedgersClosed();

    /** Increment the validations_sent_total counter.
        Called from RCLConsensus::Adaptor::validate() when a validation
        is produced and broadcast.
    */
    void
    incrementValidationsSent();

    /** Increment the validations_checked_total counter.
        Called from NetworkOPs::recvValidation() when a network validation
        is received and checked.
    */
    void
    incrementValidationsChecked();

    /** Increment the state_changes_total counter.
        Called from NetworkOPsImp::setMode() when the server operating mode
        changes (e.g. CONNECTED -> SYNCING -> TRACKING -> FULL).
    */
    void
    incrementStateChanges();

    /** Increment the jq_trans_overflow_total counter.
        Called when the job queue transaction limit overflows (mirrors
        Overlay::incJqTransOverflow()).
    */
    void
    incrementJqTransOverflow();

    /** Access the validation agreement tracker.
        Used by consensus and ledger hooks to record our validations and
        network validations so the tracker can compute agreement percentages.
        @return Reference to the internal ValidationTracker instance.
    */
    ValidationTracker&
    getValidationTracker()
    {
        return validationTracker_;
    }

private:
    /// Master enable flag; when false all methods are no-ops.
    bool const enabled_;

    /// Reference to Application services for gauge callbacks.
    ServiceRegistry& app_;

    /// Journal for logging.
    beast::Journal const journal_;

    /// Tracks validation agreement between this node and the network.
    /// Lives outside the XRPL_ENABLE_TELEMETRY guard because it is
    /// always safe to record events; the gauge callback simply won't
    /// fire when telemetry is disabled.
    ValidationTracker validationTracker_;

#ifdef XRPL_ENABLE_TELEMETRY
    /// The SDK MeterProvider that owns the export pipeline.
    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> provider_;

    /// The Meter used to create all instruments.
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter_;

    // --- Synchronous instruments (RPC) ---
    /// Counter: rpc_method_started_total{method="<name>"}
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> rpcStartedCounter_;
    /// Counter: rpc_method_finished_total{method="<name>"}
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> rpcFinishedCounter_;
    /// Counter: rpc_method_errored_total{method="<name>"}
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> rpcErroredCounter_;
    /// Histogram: rpc_method_duration_us{method="<name>"}
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        rpcDurationHistogram_;

    // --- Synchronous instruments (Job Queue) ---
    /// Counter: job_queued_total{job_type="<name>"}
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobQueuedCounter_;
    /// Counter: job_started_total{job_type="<name>"}
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobStartedCounter_;
    /// Counter: job_finished_total{job_type="<name>"}
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>> jobFinishedCounter_;
    /// Histogram: job_queued_duration_us{job_type="<name>"}
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        jobQueuedDurationHistogram_;
    /// Histogram: job_running_duration_us{job_type="<name>"}
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        jobRunningDurationHistogram_;

    // --- Observable gauges (registered via callbacks) ---
    // Handles are stored so we can remove callbacks on shutdown.
    /// Observable gauges for cache hit rates and sizes.
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        cacheHitRateGauge_;
    /// Observable gauges for TxQ metrics.
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> txqGauge_;
    /// Observable gauges for counted object instances.
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        objectCountGauge_;
    /// Observable gauges for load factor breakdown.
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> loadFactorGauge_;
    /// Observable gauges for NodeStore write_load and read_queue.
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> nodeStoreGauge_;
    /// Observable gauge for server-level health metrics (state, uptime, peers, etc.).
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> serverInfoGauge_;
    /// Observable gauge for build version info (label-based, value=1).
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> buildInfoGauge_;
    /// Observable gauge for complete ledger range start/end pairs.
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        completeLedgersGauge_;
    /// Observable gauge for database sizes and historical fetch rate.
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument> dbMetricsGauge_;

    // --- External dashboard parity gauges (Tasks 7.9-7.13) ---
    /// Observable gauge for validator health indicators (amendment blocked,
    /// UNL blocked, quorum, UNL expiry).
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validatorHealthGauge_;
    /// Observable gauge for peer network quality metrics (P90 latency,
    /// insane peer count, version spread, upgrade recommendation).
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        peerQualityGauge_;
    /// Observable gauge for ledger economy metrics (base fee, reserve,
    /// reserve increment, ledger age).
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        ledgerEconomyGauge_;
    /// Observable gauge for node state tracking (operating mode value,
    /// time in current state).
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        stateTrackingGauge_;
    /// Observable gauge for storage detail metrics (NuDB on-disk size).
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        storageDetailGauge_;
    /// Observable gauge for validation agreement metrics (1h/24h percentages
    /// and counts from ValidationTracker).
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        validationAgreementGauge_;

    // --- External dashboard parity counters (Task 7.14) ---
    /// Counter: rippled_ledgers_closed_total — incremented each consensus round.
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        ledgersClosedCounter_;
    /// Counter: rippled_validations_sent_total — incremented when this node sends a validation.
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        validationsSentCounter_;
    /// Counter: rippled_validations_checked_total — incremented for each network validation
    /// received.
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        validationsCheckedCounter_;
    /// Counter: rippled_state_changes_total — incremented on operating mode transitions.
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        stateChangesCounter_;
    /// Counter: rippled_jq_trans_overflow_total — incremented on job queue transaction overflows.
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        jqTransOverflowCounter_;
    /// Counter: rippled_validation_agreements_total — incremented by ValidationTracker on
    /// agreement.
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        validationAgreementsCounter_;
    /// Counter: rippled_validation_missed_total — incremented by ValidationTracker on miss.
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<uint64_t>>
        validationMissedCounter_;

    /** Register all observable gauge callbacks with the OTel SDK.
        Called once during start().
    */
    void
    registerAsyncGauges();
#endif  // XRPL_ENABLE_TELEMETRY
};

}  // namespace telemetry
}  // namespace xrpl
