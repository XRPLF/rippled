// cspell:ignore ISTOGRAM
// The all-caps macro name XRPL_METRIC_HISTOGRAM_RECORD trips cspell's
// compound-word splitter, which emits the subword "ISTOGRAM"; ignore it here.

/**
 * MetricsRegistry implementation — OpenTelemetry metric instruments for xrpld.
 *
 * This file contains:
 * - Construction / destruction logic for the OTel MeterProvider pipeline.
 * - Synchronous instrument creation (counters, histograms) for RPC, job
 *   queue, and NodeStore I/O metrics.
 * - Observable gauge callback registration for cache hit rates, TxQ state,
 *   CountedObject instances, load factors, and NodeStore queue depth.
 * - No-op stubs when XRPL_ENABLE_TELEMETRY is not defined.
 */

// On Windows, OTel's spin_lock_mutex.h (transitively included from
// MetricsRegistry.h) defines _WINSOCKAPI_ and includes <windows.h>.
// This poisons the include state for boost/asio/detail/socket_types.hpp,
// which requires winsock2.h to be included first.  Pre-including the
// boost/asio socket types header gets winsock2.h in before the OTel
// headers can interfere.
#ifdef _MSC_VER
#include <boost/asio/detail/socket_types.hpp>
#endif

#include <xrpld/telemetry/MetricsRegistry.h>

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/LoadManager.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/core/TimeKeeper.h>
#include <xrpld/overlay/Overlay.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/UptimeClock.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/JobTypes.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/metrics/aggregation/aggregation_config.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/instruments.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/view_factory.h>
#include <opentelemetry/sdk/metrics/view/view_registry.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/semconv/incubating/service_attributes.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace metric_sdk = opentelemetry::sdk::metrics;
namespace otlp_http = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;

namespace {

// Microsecond-valued duration histogram instrument names. Each is
// referenced twice — once to register the explicit-bucket view and once
// to create the instrument — so they are named constants to keep the two
// sites in sync (a mismatch would silently drop the bucket override).
constexpr char kJobQueuedDurationUs[] = "job_queued_us";
constexpr char kJobRunningDurationUs[] = "job_running_us";
constexpr char kRpcMethodDurationUs[] = "rpc_method_us";

// Millisecond-valued duration histogram instrument names. Same
// register-then-create pairing as the microsecond names above, so the same
// reason applies for naming them: the view and the record site must agree.
//
// consensus_round_duration_ms is recorded from RCLConsensus at the call site
// (via XRPL_METRIC_HISTOGRAM_RECORD, which creates the instrument lazily
// there), not created here. Only the VIEW is registered here, because a view
// matches by instrument name and must exist before the instrument is first
// used — the MeterProvider is built with the view registry, and the round
// histogram is not created until the first consensus round completes, well
// after start().
constexpr char kConsensusRoundDurationMs[] = "consensus_round_duration_ms";

/**
 * Register an explicit-bucket histogram view for a duration instrument.
 *
 * The SDK's default histogram boundaries top out at 10,000, so any value
 * above that lands in the overflow bucket and every quantile reads as the
 * top boundary. Both duration scales this project records cross that limit
 * (10,000 µs = 10 ms of job wait; 10,000 ms = 10 s of consensus round), so
 * every duration histogram installs its own boundaries instead.
 *
 * @param views      The registry to add the view to.
 * @param name       Instrument name to match (e.g. "job_running_us").
 * @param boundaries Bucket upper bounds, in the instrument's own unit,
 *                   ascending.
 */
void
addDurationHistogramView(
    metric_sdk::ViewRegistry& views,
    std::string const& name,
    std::vector<double> boundaries)
{
    auto config = std::make_shared<metric_sdk::HistogramAggregationConfig>();
    config->boundaries_ = std::move(boundaries);

    auto selector = metric_sdk::InstrumentSelectorFactory::Create(
        metric_sdk::InstrumentType::kHistogram, name, "");
    auto meterSelector = metric_sdk::MeterSelectorFactory::Create("xrpld", "1.0.0", "");
    auto view =
        metric_sdk::ViewFactory::Create(name, "", metric_sdk::AggregationType::kHistogram, config);

    views.AddView(std::move(selector), std::move(meterSelector), std::move(view));
}

/**
 * Register the explicit-bucket view for a MICROSECOND-valued instrument.
 *
 * Boundaries: 100µs, 500µs, 1ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms,
 * 1s, 2.5s, 5s, 10s, 30s, 60s — sub-millisecond jobs through multi-second
 * stalls, without saturating.
 *
 * @param views The registry to add the view to.
 * @param name  Instrument name to match (e.g. "job_running_us").
 */
void
addMicrosecondHistogramView(metric_sdk::ViewRegistry& views, std::string const& name)
{
    addDurationHistogramView(
        views,
        name,
        {100.0,
         500.0,
         1'000.0,
         5'000.0,
         10'000.0,
         25'000.0,
         50'000.0,
         100'000.0,
         250'000.0,
         500'000.0,
         1'000'000.0,
         2'500'000.0,
         5'000'000.0,
         10'000'000.0,
         30'000'000.0,
         60'000'000.0});
}

/**
 * Register the explicit-bucket view for a consensus-round duration in
 * MILLISECONDS.
 *
 * The round histogram needs its own boundaries for two reasons. The SDK
 * default tops out at 10,000 ms, and a recovering or stalled node routinely
 * rounds slower than that — the consensus parameters themselves allow up to
 * `ledgerAbandonConsensus` = 120 s — so the default would collapse exactly the
 * slow rounds this signal exists to show into one saturated top bucket. And a
 * healthy round is about 3-4 s, which the default's coarse spacing near that
 * value cannot resolve, so a round drifting from 3 s to 5 s would not move any
 * quantile.
 *
 * Boundaries: 500ms, 1s, 2s, 3s, 4s, 5s, 7.5s, 10s, 15s, 20s, 30s, 60s, 120s.
 * Dense across the healthy 2-5 s band, then widening to the 120 s abandon
 * limit so a stalled round still lands in a real bucket.
 *
 * @param views The registry to add the view to.
 * @param name  Instrument name to match ("consensus_round_duration_ms").
 */
void
addRoundDurationHistogramView(metric_sdk::ViewRegistry& views, std::string const& name)
{
    addDurationHistogramView(
        views,
        name,
        {500.0,
         1'000.0,
         2'000.0,
         3'000.0,
         4'000.0,
         5'000.0,
         7'500.0,
         10'000.0,
         15'000.0,
         20'000.0,
         30'000.0,
         60'000.0,
         120'000.0});
}

}  // namespace

#endif  // XRPL_ENABLE_TELEMETRY

namespace xrpl::telemetry {

MetricsRegistry::MetricsRegistry(
    [[maybe_unused]] bool enabled,
    [[maybe_unused]] ServiceRegistry& app,
    [[maybe_unused]] beast::Journal journal)
    : enabled_(enabled)
#ifdef XRPL_ENABLE_TELEMETRY
    , app_(app)
    , journal_(journal)
#endif
{
}

MetricsRegistry::~MetricsRegistry()
{
    stop();
}

void
MetricsRegistry::start(std::string const& endpoint, std::string const& instanceId)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_)
        return;

    JLOG(journal_.info()) << "MetricsRegistry: starting, endpoint=" << endpoint
                          << ", instanceId=" << instanceId;

    initExporterAndProvider(endpoint, instanceId);
    initSyncInstruments();
    registerAsyncGauges();

    JLOG(journal_.info()) << "MetricsRegistry: started successfully";
#else
    (void)endpoint;
    (void)instanceId;
    (void)enabled_;
#endif  // XRPL_ENABLE_TELEMETRY
}

#ifdef XRPL_ENABLE_TELEMETRY
void
MetricsRegistry::initExporterAndProvider(std::string const& endpoint, std::string const& instanceId)
{
    // Configure OTLP/HTTP metric exporter.
    otlp_http::OtlpHttpMetricExporterOptions exporterOpts;
    exporterOpts.url = endpoint;
    auto exporter = otlp_http::OtlpHttpMetricExporterFactory::Create(exporterOpts);

    // Configure periodic reader with 10-second export interval.
    metric_sdk::PeriodicExportingMetricReaderOptions readerOpts;
    readerOpts.export_interval_millis = std::chrono::milliseconds(10000);
    readerOpts.export_timeout_millis = std::chrono::milliseconds(5000);
    auto reader =
        metric_sdk::PeriodicExportingMetricReaderFactory::Create(std::move(exporter), readerOpts);

    // Configure resource attributes so Prometheus service_instance_id labels
    // distinguish metrics from different nodes (matches OTelCollector setup).
    resource::ResourceAttributes attrs;
    // Use std::string, not a string literal: ResourceAttributes stores an
    // OTel AttributeValue variant whose char-const* overload binds to bool,
    // so "xrpld" would be recorded as the boolean true. std::string selects
    // the string alternative and the value round-trips as service.name=xrpld.
    attrs[opentelemetry::semconv::service::kServiceName] = std::string("xrpld");
    if (!instanceId.empty())
        attrs[opentelemetry::semconv::service::kServiceInstanceId] = instanceId;
    auto resourceAttrs = resource::Resource::Create(attrs);

    // Build a view registry with explicit buckets for the duration
    // histograms. Without this they use the SDK default buckets (max 10,000),
    // which saturates every quantile at 10 ms for the µs instruments and at
    // 10 s for the round histogram.
    auto views = std::make_unique<metric_sdk::ViewRegistry>();
    addMicrosecondHistogramView(*views, kJobQueuedDurationUs);
    addMicrosecondHistogramView(*views, kJobRunningDurationUs);
    addMicrosecondHistogramView(*views, kRpcMethodDurationUs);
    // Millisecond-scale: recorded at the RCLConsensus call site, so only the
    // view is declared here (see the constant's comment).
    addRoundDurationHistogramView(*views, kConsensusRoundDurationMs);

    // Create MeterProvider with resource, then attach the metric reader.
    provider_ = metric_sdk::MeterProviderFactory::Create(std::move(views), resourceAttrs);
    provider_->AddMetricReader(std::move(reader));

    // Get a meter for all xrpld instruments.
    meter_ = provider_->GetMeter("xrpld", "1.0.0");
}

void
MetricsRegistry::initSyncInstruments()
{
    // RPC per-method counters and histogram.
    rpcStartedCounter_ =
        meter_->CreateUInt64Counter("rpc_method_started_total", "Total RPC method calls started");
    rpcFinishedCounter_ = meter_->CreateUInt64Counter(
        "rpc_method_finished_total", "Total RPC method calls completed successfully");
    rpcErroredCounter_ = meter_->CreateUInt64Counter(
        "rpc_method_errored_total", "Total RPC method calls that errored");
    rpcDurationHistogram_ = meter_->CreateDoubleHistogram(
        kRpcMethodDurationUs, "RPC method execution time in microseconds");

    // Job queue per-type counters and histograms.
    jobQueuedCounter_ = meter_->CreateUInt64Counter("job_queued_total", "Total jobs enqueued");
    jobStartedCounter_ = meter_->CreateUInt64Counter("job_started_total", "Total jobs started");
    jobFinishedCounter_ = meter_->CreateUInt64Counter("job_finished_total", "Total jobs completed");
    jobQueuedDurationHistogram_ = meter_->CreateDoubleHistogram(
        kJobQueuedDurationUs, "Time jobs spent waiting in the queue (microseconds)");
    jobRunningDurationHistogram_ =
        meter_->CreateDoubleHistogram(kJobRunningDurationUs, "Job execution time in microseconds");

    // --- External dashboard parity counters (Task 7.14) ---
    ledgersClosedCounter_ =
        meter_->CreateUInt64Counter("ledgers_closed_total", "Total ledgers closed by consensus");
    validationsSentCounter_ = meter_->CreateUInt64Counter(
        "validations_sent_total", "Total validations sent by this node");
    validationsCheckedCounter_ = meter_->CreateUInt64Counter(
        "validations_checked_total", "Total network validations received and checked");
    // state_changes_total is NOT created here. It is emitted at its call site
    // (NetworkOPsImp::setMode) through XRPL_METRIC_COUNTER_INC_LABELED so it
    // can carry the {from,to} transition labels; a registry-owned instrument
    // would only give an unlabelled total.
    // jq_trans_overflow_total is observed from Overlay's existing cumulative
    // atomic (Overlay::getJqTransOverflow()) rather than pushed. The overlay
    // owns the only increment site (PeerImp), so an ObservableCounter reads the
    // live total each collection cycle without threading a push path through
    // develop-owned overlay code.
    jqTransOverflowObservable_ = meter_->CreateInt64ObservableCounter(
        "jq_trans_overflow_total", "Total job queue transaction overflows");
    jqTransOverflowObservable_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            try
            {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                    ->Observe(static_cast<int64_t>(self->app_.getOverlay().getJqTransOverflow()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
    ledgerHistoryMismatchCounter_ = meter_->CreateUInt64Counter(
        "ledger_history_mismatch_total", "Total built-vs-validated ledger mismatches by reason");
    txqExpiredCounter_ = meter_->CreateUInt64Counter(
        "txq_expired_total", "Total transactions expired out of the transaction queue");
    txqDroppedCounter_ = meter_->CreateUInt64Counter(
        "txq_dropped_total", "Total transactions refused admission to the queue by reason");
    // Note: validation_agreements_total / validation_missed_total are monotonic
    // ObservableCounters created in registerValidationTotalsCounters() (below).
}
#endif  // XRPL_ENABLE_TELEMETRY

void
MetricsRegistry::detachCallbacks() noexcept
{
#ifdef XRPL_ENABLE_TELEMETRY
    // Release so every subsequent callback acquire-load sees true.
    callbacksDetached_.store(true, std::memory_order_release);
#endif  // XRPL_ENABLE_TELEMETRY
}

void
MetricsRegistry::stop()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!provider_)
        return;

    JLOG(journal_.info()) << "MetricsRegistry: stopping";

    // Belt-and-suspenders: detachCallbacks() should have already been
    // called by Application shutdown before any service the callbacks
    // observe was stopped. Setting the flag here is redundant for a
    // correct caller but protects against a future caller that forgets
    // to detach first.
    callbacksDetached_.store(true, std::memory_order_release);

    // SDK teardown order: Shutdown() stops the PeriodicExportingMetricReader
    // thread (so no further gauge callbacks fire) and performs the final
    // collect-and-export drain itself. The trailing ForceFlush() is a
    // redundant safety net (a no-op once the reader is shut down), then
    // reset() destroys the provider.
    provider_->Shutdown();
    provider_->ForceFlush();
    provider_.reset();

    JLOG(journal_.info()) << "MetricsRegistry: stopped";
#endif  // XRPL_ENABLE_TELEMETRY
}

// -----------------------------------------------------------------
// Synchronous instrument recording — RPC metrics (Task 9.4)
// -----------------------------------------------------------------

void
MetricsRegistry::recordRpcStarted(std::string_view method)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_ || !rpcStartedCounter_)
        return;
    rpcStartedCounter_->Add(1, {{"method", std::string(method)}});
#else
    (void)method;
    (void)enabled_;
#endif
}

void
MetricsRegistry::recordRpcFinished(std::string_view method, std::int64_t durationUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_ || !rpcFinishedCounter_)
        return;
    rpcFinishedCounter_->Add(1, {{"method", std::string(method)}});
    if (rpcDurationHistogram_)
    {
        rpcDurationHistogram_->Record(
            static_cast<double>(durationUs),
            {{"method", std::string(method)}},
            opentelemetry::context::Context{});
    }
#else
    (void)method;
    (void)durationUs;
    (void)enabled_;
#endif
}

void
MetricsRegistry::recordRpcErrored(std::string_view method, std::int64_t durationUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_ || !rpcErroredCounter_)
        return;
    rpcErroredCounter_->Add(1, {{"method", std::string(method)}});
    if (rpcDurationHistogram_)
    {
        rpcDurationHistogram_->Record(
            static_cast<double>(durationUs),
            {{"method", std::string(method)}},
            opentelemetry::context::Context{});
    }
#else
    (void)method;
    (void)durationUs;
    (void)enabled_;
#endif
}

// -----------------------------------------------------------------
// Synchronous instrument recording — Job Queue metrics (Task 9.5)
// -----------------------------------------------------------------

void
MetricsRegistry::recordJobQueued(std::string_view jobType)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_ || !jobQueuedCounter_)
        return;
    jobQueuedCounter_->Add(1, {{"job_type", std::string(jobType)}});
#else
    (void)jobType;
    (void)enabled_;
#endif
}

void
MetricsRegistry::recordJobStarted(std::string_view jobType, std::int64_t queuedDurUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_ || !jobStartedCounter_)
        return;
    jobStartedCounter_->Add(1, {{"job_type", std::string(jobType)}});
    if (jobQueuedDurationHistogram_ && queuedDurUs >= 0)
    {
        // Guard against negative queued durations: the caller derives this
        // from a steady-clock delta that can go slightly negative under clock
        // skew or reordering. The OTel SDK rejects negative histogram values
        // (logging a warning per call), so skip them rather than spam.
        jobQueuedDurationHistogram_->Record(
            static_cast<double>(queuedDurUs),
            {{"job_type", std::string(jobType)}},
            opentelemetry::context::Context{});
    }
#else
    (void)jobType;
    (void)queuedDurUs;
    (void)enabled_;
#endif
}

void
MetricsRegistry::recordJobFinished(std::string_view jobType, std::int64_t runningDurUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_ || !jobFinishedCounter_)
        return;
    jobFinishedCounter_->Add(1, {{"job_type", std::string(jobType)}});
    if (jobRunningDurationHistogram_)
    {
        jobRunningDurationHistogram_->Record(
            static_cast<double>(runningDurUs),
            {{"job_type", std::string(jobType)}},
            opentelemetry::context::Context{});
    }
#else
    (void)jobType;
    (void)runningDurUs;
    (void)enabled_;
#endif
}

// -----------------------------------------------------------------
// Observable gauge callbacks (Tasks 9.1, 9.2, 9.3, 9.6, 9.7)
// -----------------------------------------------------------------

#ifdef XRPL_ENABLE_TELEMETRY

void
MetricsRegistry::registerAsyncGauges()
{
    // Each helper creates one observable instrument and attaches one
    // callback. Keeping the registration bodies in separate methods
    // preserves the 80-line-per-function limit enforced by CLAUDE.md.
    registerCacheHitRateGauge();
    registerTxqGauge();
    registerObjectCountGauge();
    registerLoadFactorGauge();
    registerNodeStoreGauge();
    registerServerInfoGauge();
    registerBuildInfoGauge();
    registerCompleteLedgersGauge();
    registerDbMetricsGauge();
    registerValidatorHealthGauge();
    registerPeerQualityGauge();
    registerReduceRelayGauge();
    registerLedgerEconomyGauge();
    registerStateTrackingGauge();
    registerStorageDetailGauge();
    registerValidationAgreementGauge();
    registerValidationTotalsCounters();
    registerUnlQuorumGauge();
    registerClockSkewGauge();
    registerSyncStateGauge();
    registerStallEventsCounter();
    registerSyncAcquireGauge();
    registerCacheHitRateDetailGauge();
    registerJobQueueBacklogGauge();
    registerJobQueueSaturationGauge();
    registerPeerLedgerSupplyGauge();
    registerSlotCensusGauge();
    registerAmendmentBlockGauge();
    registerNodeStoreLatencyGauge();
    registerLedgerQuorumPublishGauge();
}

void
MetricsRegistry::registerCacheHitRateGauge()
{
    // --- Task 9.2: Cache hit rate and size gauges ---
    cacheHitRateGauge_ =
        meter_->CreateDoubleObservableGauge("cache_metrics", "Cache hit rates and sizes");
    cacheHitRateGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                // SLE cache hit rate (0.0 - 1.0).
                auto sleRate = app.getCachedSLEs().rate();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(sleRate, {{"metric", "SLE_hit_rate"}});

                // Ledger cache hit rate.
                // TaggedCache::getHitRate() returns 0-100; normalize to
                // 0.0-1.0 so the Grafana panel using "percentunit" renders
                // correctly.
                auto ledgerRate = app.getLedgerMaster().getCacheHitRate() / 100.0;
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(ledgerRate, {{"metric", "ledger_hit_rate"}});

                // AcceptedLedger cache hit rate (also 0-100 from
                // TaggedCache; normalize to 0.0-1.0).
                auto alRate = app.getAcceptedLedgerCache().getHitRate() / 100.0;
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(alRate, {{"metric", "AL_hit_rate"}});

                // TreeNode cache size.
                auto tnCacheSize = app.getNodeFamily().getTreeNodeCache()->getCacheSize();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(
                        static_cast<double>(tnCacheSize), {{"metric", "treenode_cache_size"}});

                // TreeNode track size.
                auto tnTrackSize = app.getNodeFamily().getTreeNodeCache()->getTrackSize();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(
                        static_cast<double>(tnTrackSize), {{"metric", "treenode_track_size"}});

                // FullBelow cache size.
                auto fbSize = app.getNodeFamily().getFullBelowCache()->size();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(static_cast<double>(fbSize), {{"metric", "fullbelow_size"}});

                // AcceptedLedger cache size (entry count).
                auto alSize = app.getAcceptedLedgerCache().size();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(static_cast<double>(alSize), {{"metric", "AL_size"}});
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerTxqGauge()
{
    // --- Task 9.3: TxQ metrics gauges ---
    txqGauge_ = meter_->CreateDoubleObservableGauge("txq_metrics", "Transaction queue metrics");
    txqGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto const metrics = app.getTxQ().getMetrics(*app.getOpenLedger().current());

                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                observe("txq_count", static_cast<double>(metrics.txCount));
                observe(
                    "txq_max_size",
                    metrics.txQMaxSize ? static_cast<double>(*metrics.txQMaxSize) : 0.0);
                observe("txq_in_ledger", static_cast<double>(metrics.txInLedger));
                observe("txq_per_ledger", static_cast<double>(metrics.txPerLedger));
                observe(
                    "txq_reference_fee_level",
                    static_cast<double>(metrics.referenceFeeLevel.fee()));
                observe(
                    "txq_min_processing_fee_level",
                    static_cast<double>(metrics.minProcessingFeeLevel.fee()));
                observe("txq_med_fee_level", static_cast<double>(metrics.medFeeLevel.fee()));
                observe(
                    "txq_open_ledger_fee_level",
                    static_cast<double>(metrics.openLedgerFeeLevel.fee()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if TxQ or OpenLedger are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerObjectCountGauge()
{
    // --- Task 9.6: Counted object instance gauges ---
    objectCountGauge_ = meter_->CreateInt64ObservableGauge(
        "object_count", "Live instance counts for key internal object types");
    objectCountGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            try
            {
                // Iterate through all CountedObject types via the linked
                // list in CountedObjects.  We report all types with count
                // > 0, filtering to the key types of interest.
                auto counts = CountedObjects::getInstance().getCounts(0);
                for (auto const& [name, count] : counts)
                {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(static_cast<int64_t>(count), {{"type", name}});
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
MetricsRegistry::registerLoadFactorGauge()
{
    // --- Task 9.7: Load factor breakdown gauges ---
    loadFactorGauge_ =
        meter_->CreateDoubleObservableGauge("load_factor_metrics", "Fee load factor breakdown");
    loadFactorGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto& feeTrack = app.getFeeTrack();
                auto const loadBase = static_cast<double>(feeTrack.getLoadBase());

                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // Combined load factor (server component).
                observe(
                    "load_factor_server", static_cast<double>(feeTrack.getLoadFactor()) / loadBase);

                // Individual factor components.
                observe(
                    "load_factor_local", static_cast<double>(feeTrack.getLocalFee()) / loadBase);
                observe("load_factor_net", static_cast<double>(feeTrack.getRemoteFee()) / loadBase);
                observe(
                    "load_factor_cluster",
                    static_cast<double>(feeTrack.getClusterFee()) / loadBase);

                // Fee escalation factors from TxQ.
                auto const metrics = app.getTxQ().getMetrics(*app.getOpenLedger().current());
                auto refLevel = static_cast<double>(metrics.referenceFeeLevel.fee());
                if (refLevel > 0)
                {
                    observe(
                        "load_factor_fee_escalation",
                        static_cast<double>(metrics.openLedgerFeeLevel.fee()) / refLevel);
                    observe(
                        "load_factor_fee_queue",
                        static_cast<double>(metrics.minProcessingFeeLevel.fee()) / refLevel);
                }

                // Combined load factor (max of server and fee escalation).
                auto const loadFactorServer = feeTrack.getLoadFactor();
                auto const loadBaseServer = feeTrack.getLoadBase();
                double combined = static_cast<double>(loadFactorServer) / loadBase;
                if (refLevel > 0)
                {
                    double const feeEscalation =
                        static_cast<double>(metrics.openLedgerFeeLevel.fee()) * loadBaseServer /
                        refLevel;
                    if (feeEscalation > static_cast<double>(loadFactorServer))
                    {
                        combined = feeEscalation / loadBase;
                    }
                }
                observe("load_factor", combined);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerNodeStoreGauge()
{
    // --- Task 9.1: NodeStore I/O gauges ---
    // The cumulative counters (reads, writes, bytes) are also exposed here
    // as observable gauges.  This avoids adding an xrpld dependency into the
    // libxrpl nodestore code — the MetricsRegistry reads the existing atomic
    // counters from Database via its public accessors.
    nodeStoreGauge_ = meter_->CreateInt64ObservableGauge(
        "nodestore_state", "NodeStore I/O counters, queue depth, and write load");
    nodeStoreGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto& db = app.getNodeStore();

                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // Cumulative counters (monotonically increasing).
                observe("node_reads_total", static_cast<int64_t>(db.getFetchTotalCount()));
                observe("node_reads_hit", static_cast<int64_t>(db.getFetchHitCount()));
                observe("node_writes", static_cast<int64_t>(db.getStoreCount()));
                observe("node_written_bytes", static_cast<int64_t>(db.getStoreSize()));
                observe("node_read_bytes", static_cast<int64_t>(db.getFetchSize()));

                // Write load score (instantaneous).
                observe("write_load", static_cast<int64_t>(db.getWriteLoad()));

                // Read queue depth (instantaneous).
                json::Value obj(json::ValueType::Object);
                db.getCountsJson(obj);
                if (obj.isMember("read_queue"))
                {
                    observe("read_queue", static_cast<int64_t>(obj["read_queue"].asUInt()));
                }

                // Cumulative read duration (stored as JSON string, not int).
                if (obj.isMember(jss::node_reads_duration_us))
                {
                    auto durStr = obj[jss::node_reads_duration_us].asString();
                    if (!durStr.empty())
                    {
                        observe("node_reads_duration_us", static_cast<int64_t>(std::stoll(durStr)));
                    }
                }

                // Read thread pool stats (native JSON ints, no jss:: constants).
                if (obj.isMember("read_request_bundle"))
                {
                    observe(
                        "read_request_bundle",
                        static_cast<int64_t>(obj["read_request_bundle"].asInt()));
                }
                if (obj.isMember("read_threads_running"))
                {
                    observe(
                        "read_threads_running",
                        static_cast<int64_t>(obj["read_threads_running"].asInt()));
                }
                if (obj.isMember("read_threads_total"))
                {
                    observe(
                        "read_threads_total",
                        static_cast<int64_t>(obj["read_threads_total"].asInt()));
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
MetricsRegistry::registerServerInfoGauge()
{
    // --- Task 9.7a: Server info gauges ---
    serverInfoGauge_ =
        meter_->CreateInt64ObservableGauge("server_info", "Server-level health metrics");
    serverInfoGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // Server operating mode (DISCONNECTED=0 .. FULL=4).
                observe("server_state", static_cast<int64_t>(app.getOPs().getOperatingMode()));

                // Uptime in seconds since server start.
                observe(
                    "uptime", static_cast<int64_t>(UptimeClock::now().time_since_epoch().count()));

                // Total peer count (inbound + outbound).
                observe("peers", static_cast<int64_t>(app.getOverlay().size()));

                // Validated ledger sequence (0 if none yet).
                observe(
                    "validated_ledger_seq",
                    static_cast<int64_t>(app.getLedgerMaster().getValidLedgerIndex()));

                // Current open ledger sequence.
                observe(
                    "ledger_current_index",
                    static_cast<int64_t>(app.getLedgerMaster().getCurrentLedgerIndex()));

                // Cumulative resource-related peer disconnects.
                observe(
                    "peer_disconnects_resources",
                    static_cast<int64_t>(app.getOverlay().getPeerDisconnectCharges()));

                // Last consensus round data (from JSON — only public API).
                auto const consensusInfo = app.getOPs().getConsensusInfo();
                if (consensusInfo.isMember("previous_proposers"))
                {
                    observe(
                        "last_close_proposers",
                        static_cast<int64_t>(consensusInfo["previous_proposers"].asUInt()));
                }
                if (consensusInfo.isMember("previous_mseconds"))
                {
                    observe(
                        "last_close_converge_time_ms",
                        static_cast<int64_t>(consensusInfo["previous_mseconds"].asUInt()));
                }

                // Network close time of the last closed ledger, as NetClock
                // seconds since the XRPL epoch (2000-01-01). Unlike a span
                // timestamp, a gauge value survives as a queryable time series,
                // so dashboards can show last-close age (staleness) via
                // now - value. The close interval comes from the
                // ledgers_closed_total counter, not a delta of this gauge
                // (a timestamp gauge's delta aliases to the scrape period).
                // Skip until a ledger has closed.
                if (auto const closed = app.getLedgerMaster().getClosedLedger())
                {
                    observe(
                        "last_close_time",
                        static_cast<int64_t>(
                            closed->header().closeTime.time_since_epoch().count()));
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerBuildInfoGauge()
{
    // --- Task 9.7b: Build info gauge ---
    buildInfoGauge_ = meter_->CreateInt64ObservableGauge("build_info", "Build version information");
    buildInfoGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* /* state */) {
            try
            {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                    ->Observe(1, {{"version", std::string(BuildInfo::getVersionString())}});
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
            }
        },
        nullptr);
}

void
MetricsRegistry::registerCompleteLedgersGauge()
{
    // --- Task 9.7c: Complete ledgers range gauge ---
    completeLedgersGauge_ = meter_->CreateInt64ObservableGauge(
        "complete_ledgers", "Complete ledger range start/end pairs");
    completeLedgersGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto const rangeStr = app.getLedgerMaster().getCompleteLedgers();
                if (rangeStr.empty() || rangeStr == "empty")
                    return;

                // Parse comma-separated ranges like
                // "32570-50000,50005-75891421".
                std::size_t rangeIndex = 0;
                std::istringstream stream(rangeStr);
                std::string segment;
                while (std::getline(stream, segment, ','))
                {
                    auto const dashPos = segment.find('-');
                    if (dashPos == std::string::npos || dashPos == 0 ||
                        dashPos == segment.size() - 1)
                        continue;

                    auto const startStr = segment.substr(0, dashPos);
                    auto const endStr = segment.substr(dashPos + 1);

                    auto const idxStr = std::to_string(rangeIndex);

                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(
                            static_cast<int64_t>(std::stoll(startStr)),
                            {{"bound", "start"}, {"index", idxStr}});

                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(
                            static_cast<int64_t>(std::stoll(endStr)),
                            {{"bound", "end"}, {"index", idxStr}});

                    ++rangeIndex;
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on parse error or if services not ready.
            }
        },
        this);
}

void
MetricsRegistry::registerDbMetricsGauge()
{
    // --- Task 9.7d: Database size and fetch rate gauges ---
    dbMetricsGauge_ =
        meter_->CreateInt64ObservableGauge("db_metrics", "Database storage sizes and fetch rates");
    dbMetricsGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                auto& rdb = app.getRelationalDatabase();
                observe("db_kb_total", static_cast<int64_t>(rdb.getKBUsedAll()));
                observe("db_kb_ledger", static_cast<int64_t>(rdb.getKBUsedLedger()));
                observe("db_kb_transaction", static_cast<int64_t>(rdb.getKBUsedTransaction()));

                // Historical ledger fetches per minute.
                observe(
                    "historical_perminute",
                    static_cast<int64_t>(app.getInboundLedgers().fetchRate()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerValidatorHealthGauge()
{
    // --- Task 7.9: Validator health gauges ---
    validatorHealthGauge_ =
        meter_->CreateDoubleObservableGauge("validator_health", "Validator health indicators");
    validatorHealthGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                observe("amendment_blocked", app.getOPs().isAmendmentBlocked() ? 1.0 : 0.0);
                observe("unl_blocked", app.getOPs().isUNLBlocked() ? 1.0 : 0.0);
                observe("validation_quorum", static_cast<double>(app.getValidators().quorum()));

                // Days until UNL list expiry (-1 if no expiry known).
                auto const expiry = app.getValidators().expires();
                if (expiry)
                {
                    auto const now = app.getTimeKeeper().closeTime();
                    auto const diffHours =
                        std::chrono::duration_cast<std::chrono::hours>(*expiry - now).count();
                    observe("unl_expiry_days", static_cast<double>(diffHours) / 24.0);
                }
                else
                {
                    observe("unl_expiry_days", -1.0);
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerPeerQualityGauge()
{
    // --- Task 7.10: Peer quality gauges ---
    // Uses Peer::json() to read latency and version since those accessors
    // are not on the abstract Peer interface (they live on PeerImp).
    peerQualityGauge_ =
        meter_->CreateDoubleObservableGauge("peer_quality", "Peer network quality metrics");
    peerQualityGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // Collect latencies, version info, and tracking state from
                // each peer's JSON.
                std::vector<int> latencies;
                int higherVersionCount = 0;
                int totalPeers = 0;
                int divergedCount = 0;

                // Encode a version string into BuildInfo's comparable numeric
                // form. Peers report the full "xrpld-3.3.0-b0" string while
                // our baseline is the bare "3.3.0-b0", and SemanticVersion
                // requires a leading digit, so strip any non-digit prefix
                // first. Numeric encoding avoids the lexicographic bug where
                // "2.3.0" > "2.10.0" and "xrpld-..." > "3...".
                auto const encodeVersion = [](std::string_view v) -> std::uint64_t {
                    auto const firstDigit = v.find_first_of("0123456789");
                    if (firstDigit == std::string_view::npos)
                        return 0;
                    return BuildInfo::encodeSoftwareVersion(v.substr(firstDigit));
                };
                auto const ownEncoded = encodeVersion(BuildInfo::getVersionString());

                app.getOverlay().foreach([&](std::shared_ptr<Peer> const& peer) {
                    ++totalPeers;
                    auto const pj = peer->json();
                    if (pj.isMember(jss::latency))
                    {
                        latencies.push_back(pj[jss::latency].asInt());
                    }
                    if (pj.isMember(jss::version))
                    {
                        // Unparseable peer versions encode below ownEncoded, so
                        // the comparison correctly leaves them uncounted.
                        auto const pv = pj[jss::version].asString();
                        if (encodeVersion(pv) > ownEncoded)
                            ++higherVersionCount;
                    }
                    // PeerImp::json() sets "track" to "diverged" when the peer's
                    // tracking state is Tracking::Diverged (i.e. it is following
                    // a different ledger chain than us).
                    if (pj.isMember(jss::track) && pj[jss::track].asString() == "diverged")
                        ++divergedCount;
                });

                // P90 latency across connected peers.
                if (!latencies.empty())
                {
                    std::ranges::sort(latencies);
                    auto p90idx = static_cast<std::size_t>(latencies.size() * 0.9);
                    if (p90idx >= latencies.size())
                        p90idx = latencies.size() - 1;
                    observe("peer_latency_p90_ms", static_cast<double>(latencies[p90idx]));
                }
                else
                {
                    observe("peer_latency_p90_ms", 0.0);
                }

                // Percentage of peers running a higher version.
                double const higherPct = totalPeers > 0
                    ? (static_cast<double>(higherVersionCount) / totalPeers * 100.0)
                    : 0.0;
                observe("peers_higher_version_pct", higherPct);

                // Count peers diverged from our ledger chain, read from the
                // peer's "track" JSON field (set by PeerImp::json()). Diverged
                // peers are following a different chain and are a leading
                // indicator of local sync trouble.
                observe("peers_insane_count", static_cast<double>(divergedCount));

                // Binary flag: recommend upgrade if >60% run a newer version.
                observe("upgrade_recommended", higherPct > 60.0 ? 1.0 : 0.0);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerReduceRelayGauge()
{
    // Transaction reduce-relay efficiency. Overlay::txMetrics() exposes the
    // rolling averages as a JSON object with string values (std::to_string),
    // so parse each field. A high suppressed:selected ratio proves the
    // feature is saving bandwidth; a high not_enabled count means stale peers
    // force full relay.
    reduceRelayGauge_ = meter_->CreateInt64ObservableGauge(
        "reduce_relay_metrics", "Transaction reduce-relay efficiency metrics");
    reduceRelayGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto const tm = app.getOverlay().txMetrics();

                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // Each field is a decimal string; emit when present and parseable.
                auto observeField = [&](auto const& field, char const* name) {
                    if (tm.isMember(field))
                    {
                        auto const s = tm[field].asString();
                        if (!s.empty())
                            observe(name, static_cast<int64_t>(std::stoll(s)));
                    }
                };

                observeField(jss::txr_selected_cnt, "selected_peers");
                observeField(jss::txr_suppressed_cnt, "suppressed_peers");
                observeField(jss::txr_not_enabled_cnt, "not_enabled_peers");
                observeField(jss::txr_missing_tx_freq, "missing_tx_freq");
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready or a value is
                // not parseable.
            }
        },
        this);
}

void
MetricsRegistry::registerLedgerEconomyGauge()
{
    // --- Task 7.11: Ledger economy gauges ---
    ledgerEconomyGauge_ =
        meter_->CreateDoubleObservableGauge("ledger_economy", "Ledger fee and economy metrics");
    ledgerEconomyGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // Local fee (drops).
                observe("base_fee_xrp", static_cast<double>(app.getFeeTrack().getLocalFee()));

                // Reserve values from the validated ledger.
                auto const ledger = app.getLedgerMaster().getValidatedLedger();
                if (ledger)
                {
                    auto const& fees = ledger->fees();
                    // Base reserve = one account, zero owned objects:
                    // accountReserve(ownerCount=0, accountCount=1) == reserve.
                    observe(
                        "reserve_base_xrp", static_cast<double>(fees.accountReserve(0, 1).drops()));
                    observe("reserve_inc_xrp", static_cast<double>(fees.increment.drops()));
                }

                // Seconds since the last validated ledger closed.
                auto const age = app.getLedgerMaster().getValidatedLedgerAge();
                observe("ledger_age_seconds", static_cast<double>(age.count()));

                // Transaction rate from the open ledger's tx count.
                // OpenView::txCount() tracks transactions in the current
                // open ledger; dividing by the ledger age gives an
                // approximate rate.
                auto const& openLedger = app.getOpenLedger();
                auto const txInLedger = openLedger.current()->txCount();
                auto const ageVal = age.count();
                if (ageVal > 0)
                {
                    observe(
                        "transaction_rate",
                        static_cast<double>(txInLedger) / static_cast<double>(ageVal));
                }
                else
                {
                    observe("transaction_rate", 0.0);
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerStateTrackingGauge()
{
    // --- Task 7.12: State tracking gauges ---
    stateTrackingGauge_ =
        meter_->CreateDoubleObservableGauge("state_tracking", "Node state and mode tracking");
    stateTrackingGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // State value: 0-4 from OperatingMode, 5=validating, 6=proposing.
                auto const mode = app.getOPs().getOperatingMode();
                auto stateValue = static_cast<double>(mode);

                // If FULL, refine using consensus info for validating/proposing.
                if (mode == OperatingMode::FULL)
                {
                    auto const info = app.getOPs().getConsensusInfo();
                    if (info.isMember("proposing") && info["proposing"].asBool())
                    {
                        stateValue = 6.0;
                    }
                    else if (info.isMember("validating") && info["validating"].asBool())
                    {
                        stateValue = 5.0;
                    }
                }
                observe("state_value", stateValue);

                // Time spent in the current operating mode, sourced from
                // NetworkOPs' StateAccounting via a lightweight accessor.
                auto const stateDurUs = app.getOPs().getServerStateDurationUs();
                observe(
                    "time_in_current_state_seconds", static_cast<double>(stateDurUs.count()) / 1e6);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerStorageDetailGauge()
{
    // --- Task 7.13: Storage detail gauges ---
    // Reports NuDB on-disk size via the NodeStore JSON counters interface.
    storageDetailGauge_ =
        meter_->CreateInt64ObservableGauge("storage_detail", "Storage detail metrics");
    storageDetailGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // NuDB on-disk size reported by the NodeStore backend.
                // getStoreSize() returns the total bytes stored.
                observe("nudb_bytes", static_cast<int64_t>(app.getNodeStore().getStoreSize()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
MetricsRegistry::registerValidationAgreementGauge()
{
    // --- Task 7.15: Validation agreement gauges ---
    // Reports rolling-window agreement percentages and counts from
    // ValidationTracker.  reconcile() is called at the start of the
    // callback so that pending ledger events are resolved before the
    // window data is read (the callback fires every ~10 s from the
    // PeriodicExportingMetricReader thread).
    validationAgreementGauge_ = meter_->CreateDoubleObservableGauge(
        "validation_agreement", "Validation agreement percentages and counts (1h/24h windows)");
    validationAgreementGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;

            try
            {
                // Reconcile pending events before reading window data.
                self->validationTracker_.reconcile();

                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                observe("agreement_pct_1h", self->validationTracker_.agreementPct1h());
                observe("agreement_pct_24h", self->validationTracker_.agreementPct24h());
                observe(
                    "agreements_1h", static_cast<double>(self->validationTracker_.agreements1h()));
                observe("missed_1h", static_cast<double>(self->validationTracker_.missed1h()));
                observe(
                    "agreements_24h",
                    static_cast<double>(self->validationTracker_.agreements24h()));
                observe("missed_24h", static_cast<double>(self->validationTracker_.missed24h()));

                // 7-day window (matches external xrpl-validator-dashboard).
                observe("agreement_pct_7d", self->validationTracker_.agreementPct7d());
                observe(
                    "agreements_7d", static_cast<double>(self->validationTracker_.agreements7d()));
                observe("missed_7d", static_cast<double>(self->validationTracker_.missed7d()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
MetricsRegistry::registerValidationTotalsCounters()
{
    // Lifetime validation agreement/miss counters.
    //
    // These are monotonic ObservableCounters (not the sync Counters they used
    // to be): a Prometheus _total must never decrease, but ValidationTracker's
    // NET totals are non-monotonic (a late repair decrements the net miss
    // count). We therefore observe the tracker's GROSS lifetime tallies, which
    // count each ledger once at first classification and are never adjusted on
    // repair (initial-classification semantics — see ValidationTracker). The
    // repaired/agreement view remains available from validation_agreement.
    //
    // reconcile() is called first so pending events are resolved before the
    // tallies are read; the callback fires every ~10 s from the
    // PeriodicExportingMetricReader thread.
    validationAgreementsObservable_ = meter_->CreateInt64ObservableCounter(
        "validation_agreements_total",
        "Lifetime validations that initially agreed with network consensus");
    validationAgreementsObservable_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            try
            {
                self->validationTracker_.reconcile();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                    ->Observe(static_cast<int64_t>(self->validationTracker_.totalAgreementsEver()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);

    validationMissedObservable_ = meter_->CreateInt64ObservableCounter(
        "validation_missed_total", "Lifetime validations that initially missed network consensus");
    validationMissedObservable_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            try
            {
                self->validationTracker_.reconcile();
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                    ->Observe(static_cast<int64_t>(self->validationTracker_.totalMissedEver()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

void
MetricsRegistry::registerUnlQuorumGauge()
{
    // --- Sync diagnostics: trusted UNL size against required quorum ---
    // validator_health already exports the quorum on its own; pairing it
    // with the trusted-key count in one instrument is what makes the
    // "can this node ever validate?" comparison a single query.
    unlQuorumGauge_ = meter_->CreateInt64ObservableGauge(
        "unl_quorum", "Trusted UNL key count vs required quorum");
    unlQuorumGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                auto& validators = app.getValidators();

                // Trusted master keys currently in effect. Zero means no
                // usable UNL: quorum can never be met.
                observe("trusted_keys", static_cast<int64_t>(validators.trustedKeyCount()));

                // Validations required for a ledger to be fully validated.
                // ValidatorList disables quorum by returning SIZE_MAX when too
                // many publishers are unavailable. Casting that straight to
                // int64_t would wrap to -1 and make the headroom
                // (trusted_keys - quorum) read positive on a node that can
                // never validate, so report the disabled state as int64 max
                // instead: headroom then goes strongly negative, which is the
                // truthful signal.
                auto const quorum = validators.quorum();
                observe(
                    "quorum",
                    quorum == std::numeric_limits<std::size_t>::max()
                        ? std::numeric_limits<int64_t>::max()
                        : static_cast<int64_t>(quorum));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerClockSkewGauge()
{
    // --- Sync diagnostics: network close-time offset ---
    // A persistent offset shows the local clock disagrees with the
    // network, which delays consensus participation. server_info hides
    // this below 60 s, so export it continuously instead.
    clockSkewGauge_ = meter_->CreateInt64ObservableGauge(
        "clock_close_offset_seconds", "Network close time offset from the local clock, in seconds");
    clockSkewGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // Negative when the local clock runs ahead of the network.
                observe("offset", static_cast<int64_t>(app.getTimeKeeper().closeOffset().count()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerSyncStateGauge()
{
    // --- Sync diagnostics: why a fresh node is not FULL yet ---
    // Four values that previously lived only in a log line or in server_info
    // JSON. All four are cheap reads pulled on the ~10 s reader tick.
    syncStateGauge_ =
        meter_->CreateInt64ObservableGauge("sync_state", "Sync-pipeline health signals");
    syncStateGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                auto& ops = app.getOPs();

                // Time to first FULL. Zero means the node has not synced yet,
                // which is exactly the case this signal exists to expose.
                observe(
                    "initial_full_duration_us",
                    static_cast<int64_t>(ops.getInitialSyncDurationUs()));

                // 1 = still waiting for a full network ledger. While this is
                // set the node refuses transactions and cannot reach FULL.
                observe("network_ledger_gate", ops.isNeedNetworkLedger() ? 1 : 0);

                // Current main-loop stall duration; 0 when healthy.
                observe(
                    "server_stall_seconds",
                    static_cast<int64_t>(app.getLoadManager().getCurrentStallSeconds()));

                // Distance from the network tip, floored at zero by the
                // accessor.
                observe("ledgers_behind", static_cast<int64_t>(ops.getLedgersBehindNetwork()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerStallEventsCounter()
{
    // --- Sync diagnostics: stall episode count ---
    // Observed rather than pushed: LoadManager's monitor thread already owns
    // the cumulative tally, and an ObservableCounter reads it each collection
    // cycle without threading a push path through the load-monitor loop.
    // Kept out of the sync_state gauge because a cumulative total needs
    // counter aggregation for rate() to be meaningful.
    stallEventsObservable_ = meter_->CreateInt64ObservableCounter(
        "server_stall_events_total", "Total server main-loop stall episodes");
    stallEventsObservable_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            try
            {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                    ->Observe(
                        static_cast<int64_t>(self->app_.getLoadManager().getStallEventCount()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerSyncAcquireGauge()
{
    // --- Sync diagnostics: is ledger acquisition actually progressing? ---
    // Aggregated on purpose: a per-ledger label would add one series per ledger
    // acquired, which is unbounded. The per-ledger view lives on the
    // ledger.acquire span instead.
    syncAcquireGauge_ = meter_->CreateInt64ObservableGauge(
        "sync_acquire", "Aggregate ledger-acquire progress across in-flight acquires");
    syncAcquireGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // One snapshot feeds all four series, so they are mutually
                // consistent rather than read at four different instants.
                auto const progress = app.getInboundLedgers().acquireProgress();

                // Flat and non-zero across ticks = this acquire will never
                // finish. Shrinking = slow but alive.
                observe(
                    "missing_state_nodes_max", static_cast<int64_t>(progress.maxMissingStateNodes));
                observe("missing_tx_nodes_max", static_cast<int64_t>(progress.maxMissingTxNodes));

                // Deep stash = arriving data outpaces processing.
                observe("received_data_depth", static_cast<int64_t>(progress.receivedDataDepth));

                // Context for the three above: zero everywhere with zero
                // in-flight acquires is idle, not healthy.
                observe("in_flight", static_cast<int64_t>(progress.inFlight));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerCacheHitRateDetailGauge()
{
    // --- Sync diagnostics: SHAMap tree-node cache hit rate ---
    // The memory layer above the node store: a miss here is what causes a
    // node-store read, which the NuDB hit-ratio panel then measures.
    shamapCacheHitRateGauge_ = meter_->CreateDoubleObservableGauge(
        "shamap_cache_hit_rate", "SHAMap tree-node cache hit rate (0.0-1.0), by cache");
    shamapCacheHitRateGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                // TaggedCache::getHitRate() returns 0-100; normalize to 0.0-1.0
                // so the panel can use Grafana's "percentunit" unit, matching
                // how cache_metrics already reports its rates.
                auto const rate = app.getNodeFamily().getTreeNodeCache()->getHitRate() / 100.0F;
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<double>>>(result)
                    ->Observe(static_cast<double>(rate), {{"metric", "treenode"}});
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerJobQueueBacklogGauge()
{
    // --- Sync diagnostics: which job types are starved right now? ---
    // The existing job_* counters and histograms describe jobs that already
    // moved. This is instantaneous occupancy, and `deferred` in particular
    // has no other exposure: a job held back by its type's concurrency limit
    // counts as neither waiting nor running anywhere else.
    jobQueueBacklogGauge_ = meter_->CreateInt64ObservableGauge(
        "jobq_backlog", "JobQueue occupancy per job type (waiting/running/deferred)");
    jobQueueBacklogGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, std::string const& jobType, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", field}, {"job_type", jobType}});
                };

                // One snapshot under one lock acquire, so the three fields of
                // a type are mutually consistent rather than read at three
                // different instants.
                for (auto const& count : app.getJobQueue().getJobTypeCounts())
                {
                    // The name helper is the single source of the label value,
                    // the same one the job_*_total counters already use, so the
                    // two label sets join.
                    auto const& jobType = JobTypes::name(count.type);
                    observe("waiting", jobType, count.waiting);
                    observe("running", jobType, count.running);
                    observe("deferred", jobType, count.deferred);
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerJobQueueSaturationGauge()
{
    // --- Sync diagnostics: is the whole worker pool exhausted? ---
    // Attributes a broad multi-stage slowdown to the pool once, instead of
    // leaving it to look like an independent fault in every subsystem whose
    // jobs are queued behind it.
    jobQueueSaturationGauge_ = meter_->CreateInt64ObservableGauge(
        "jobq_saturation", "Worker-pool saturation: tasks in flight, worker threads, jobs queued");
    jobQueueSaturationGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", field}});
                };

                // One reading feeds all three series so the ratio and the
                // backlog describe the same instant.
                auto const saturation = app.getJobQueue().getWorkerSaturation();
                observe("running_tasks", saturation.runningTasks);

                // The denominator for the ratio panel. Derived at startup from
                // [workers], node size and hardware concurrency, so it cannot
                // be hardcoded in a dashboard.
                observe("worker_threads", saturation.workerThreads);

                // Ratio at 1.0 alone is a busy pool; ratio at 1.0 with a
                // non-zero backlog is an exhausted one.
                observe("total_waiting", saturation.totalWaiting);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerPeerLedgerSupplyGauge()
{
    // --- Sync diagnostics: can the network even serve what I need? ---
    // Each peer advertises its ledger range and the connection caches it, but
    // nothing ever compared those ranges, so "no peer holds the sequence I
    // want" looked exactly like "my peers are slow" -- two faults with
    // completely different fixes.
    peerLedgerSupplyGauge_ = meter_->CreateInt64ObservableGauge(
        "peer_ledger_supply", "Peer coverage of the ledger sequence this node needs");
    peerLedgerSupplyGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", field}});
                };

                // One pass over the peers, so all five series describe the
                // same peer set at the same instant.
                auto const supply = app.getOverlay().getPeerLedgerSupply(
                    app.getLedgerMaster().getValidLedgerIndex());

                // The denominator. Zero serving out of zero reporting is
                // silence; zero out of many is a real supply gap.
                observe("peers_reporting", supply.peersReporting);
                observe("peers_serving_validated", supply.peersServingValidated);

                // The verdict: zero here while peers_reporting is non-zero
                // means waiting cannot finish the sync.
                observe("peers_serving_next", supply.peersServingNext);

                // The window the peer set covers, so an operator can tell a
                // request for discarded history from one for an unreached tip.
                observe("supply_min_seq", supply.supplyMinSeq);
                observe("supply_max_seq", supply.supplyMaxSeq);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerSlotCensusGauge()
{
    // --- Sync diagnostics: why can this node not get peers? ---
    // All nine numbers already exist inside PeerFinder; only the two active
    // counts are exported today, which cannot distinguish "not dialling",
    // "dialling and failing" and "nothing to dial".
    slotCensusGauge_ = meter_->CreateInt64ObservableGauge(
        "peerfinder_slot_census", "PeerFinder slots, connection attempts and address caches");
    slotCensusGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", field}});
                };

                // One snapshot under one PeerFinder lock acquire, so occupancy
                // and capacity can be compared against each other.
                auto const census = app.getOverlay().getSlotCensus();

                observe("out_active", census.outActive);
                observe("out_max", census.outMax);
                observe("in_active", census.inActive);
                observe("in_max", census.inMax);

                // Dials in flight. Non-zero while out_active stays under
                // out_max is the "starting and never completing" case.
                observe("connecting", census.connecting);

                // fixed_active below fixed_configured names a configured peer
                // that cannot be reached.
                observe("fixed_configured", census.fixedConfigured);
                observe("fixed_active", census.fixedActive);

                // Both at zero on a fresh node means there is nothing to dial.
                observe("bootcache", census.bootcache);
                observe("livecache", census.livecache);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerAmendmentBlockGauge()
{
    // --- Sync diagnostics: how long until this node stops validating? ---
    // The existing validator_health{metric="amendment_blocked"} reports the
    // terminal state, when nothing can be done. This is the window before it.
    amendmentBlockGauge_ = meter_->CreateInt64ObservableGauge(
        "amendment_block", "Amendment-block warning and seconds until the node stops validating");
    amendmentBlockGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", field}});
                };

                // An unsupported amendment has reached majority. Until now this
                // only surfaced as an admin-only server_info warning.
                observe("warned", app.getOPs().isAmendmentWarned() ? 1 : 0);

                // Seconds until that amendment activates. -1 means nothing is
                // pending: a distinct healthy value rather than an absent
                // series, matching validator_health{metric="unl_expiry_days"}.
                std::int64_t secondsToBlock = -1;
                if (auto const expected = app.getAmendmentTable().firstUnsupportedExpected())
                {
                    // NetClock's representation is unsigned, so the difference
                    // is taken in int64_t: subtracting the time_points directly
                    // would wrap once the activation time has passed.
                    auto const expectedSecs =
                        static_cast<std::int64_t>(expected->time_since_epoch().count());
                    auto const nowSecs = static_cast<std::int64_t>(
                        app.getTimeKeeper().closeTime().time_since_epoch().count());

                    // Clamped at 0: past due means the block is imminent, not
                    // overdue by an amount worth charting.
                    secondsToBlock = std::max<std::int64_t>(expectedSecs - nowSecs, 0);
                }
                observe("seconds_to_block", secondsToBlock);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerNodeStoreLatencyGauge()
{
    // --- Sync diagnostics: is the node store slow, and on which side? ---
    // The write mean is the new signal. storeDurationUs_ was declared and
    // never written, so no write latency existed anywhere; only the read side
    // had a duration total. A node with a large existing DB back-fills slower
    // than a fresh one, and back-fill is write-bound, so the read-side
    // metrics cannot show it. Exporting both means from one reading also makes
    // the two sides directly comparable.
    //
    // Gauge rather than histogram: a histogram would cost one Record() per
    // node object on the store/fetch path, which runs thousands of times per
    // ledger write. This reads four atomics per ~10 s tick instead. The
    // trade-off is that percentiles are unavailable -- see the header comment.
    nodeStoreLatencyGauge_ = meter_->CreateInt64ObservableGauge(
        "nodestore_latency", "NodeStore mean store/fetch latency in microseconds, with counts");
    nodeStoreLatencyGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", field}});
                };

                auto& db = app.getNodeStore();

                // One reading of each pair, so a mean and its own denominator
                // describe the same instant.
                auto const storeCount = db.getStoreCount();
                auto const storeDurationUs = db.getStoreDurationUs();
                auto const fetchCount = db.getFetchTotalCount();
                auto const fetchDurationUs = db.getFetchDurationUs();

                // Counts are always observed, including zero: that is what
                // separates "nothing written yet" from "writes are instant".
                observe("write_count", static_cast<int64_t>(storeCount));
                observe("read_count", static_cast<int64_t>(fetchCount));

                // A mean needs a non-zero denominator, and it needs a
                // numerator that was actually measured. Both are required, and
                // the series is omitted rather than observed as 0 when either
                // is missing: a reported 0 us would claim writes are
                // instantaneous, which is worse than no reading at all.
                //
                // The numerator guard is load-bearing, not defensive.
                // Database::store() is pure virtual, so only the store paths
                // that call recordStoreDuration() contribute. Today that is
                // Database::importInternal (the [import_db] path). The two
                // concrete runtime databases -- DatabaseNodeImp::store and
                // DatabaseRotatingImp::store -- do not call it yet, so on an
                // ordinary node write_count climbs while the duration total
                // stays 0. Omitting the mean makes that a visible data gap
                // instead of a false "writes take 0 us" line on the panel.
                if (storeCount > 0 && storeDurationUs > 0)
                    observe("write_mean_us", static_cast<int64_t>(storeDurationUs / storeCount));
                if (fetchCount > 0 && fetchDurationUs > 0)
                    observe("read_mean_us", static_cast<int64_t>(fetchDurationUs / fetchCount));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

void
MetricsRegistry::registerLedgerQuorumPublishGauge()
{
    // --- Sync diagnostics: the quorum gate and the publish pipeline ---
    // The last two stages of a fresh sync, and the two whose failures were
    // invisible: a node can hold every ledger it needs and still never declare
    // one validated (quorum short), or validate correctly and never publish
    // (pipeline behind). Both used to be trace-log-only or not derivable at all.
    ledgerQuorumPublishGauge_ = meter_->CreateInt64ObservableGauge(
        "ledger_quorum_publish",
        "Pre-accept quorum gate and publish lag (tally vs quorum, first-validated, lag)");
    ledgerQuorumPublishGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* field, int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", field}});
                };

                auto const& ledgerMaster = app.getLedgerMaster();

                // The pair that separates "slow" from "stuck". A tally climbing
                // toward the target will get there; a tally flat below it never
                // will, and no acquire or peer panel says which is happening.
                observe("trusted_validation_tally", ledgerMaster.getTrustedValidationTally());

                // What the last gate evaluation actually required, as opposed to
                // unl_quorum{quorum} which is what the trusted list configures.
                // Already clamped against the SIZE_MAX "quorum disabled"
                // sentinel by LedgerMaster, so this never wraps negative.
                observe("quorum_target", ledgerMaster.getQuorumTarget());

                // One-shot: a value is the time the first ledger took to pass
                // the gate, and 0 means it never has. Not a trend.
                observe("time_to_first_validated_us", ledgerMaster.getTimeToFirstValidatedUs());

                // Validated but not yet published. pubLedgerSeq_ was never
                // exported, so this gap was not derivable from any other series.
                observe("publish_lag", ledgerMaster.getPublishLag());
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);
}

#endif  // XRPL_ENABLE_TELEMETRY

// -----------------------------------------------------------------
// External dashboard parity counter increments (Task 7.14)
// -----------------------------------------------------------------

void
MetricsRegistry::incrementLedgersClosed()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (enabled_ && ledgersClosedCounter_)
        ledgersClosedCounter_->Add(1);
#endif
}

void
MetricsRegistry::incrementValidationsSent()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (enabled_ && validationsSentCounter_)
        validationsSentCounter_->Add(1);
#endif
}

void
MetricsRegistry::incrementValidationsChecked()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (enabled_ && validationsCheckedCounter_)
        validationsCheckedCounter_->Add(1);
#endif
}

void
MetricsRegistry::incrementLedgerHistoryMismatch(std::string_view reason)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (enabled_ && ledgerHistoryMismatchCounter_)
        ledgerHistoryMismatchCounter_->Add(1, {{"reason", std::string(reason)}});
#endif
}

void
MetricsRegistry::incrementTxqExpired()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (enabled_ && txqExpiredCounter_)
        txqExpiredCounter_->Add(1);
#endif
}

void
MetricsRegistry::incrementTxqDropped(std::string_view reason)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (enabled_ && txqDroppedCounter_)
        txqDroppedCounter_->Add(1, {{"reason", std::string(reason)}});
#endif
}

}  // namespace xrpl::telemetry
