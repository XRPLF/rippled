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

#include <xrpld/app/ledger/AcquireStats.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/core/TimeKeeper.h>
#include <xrpld/overlay/Overlay.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/UptimeClock.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/telemetry/GetObjectMetricNames.h>

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
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace metric_sdk = opentelemetry::sdk::metrics;
namespace otlp_http = opentelemetry::exporter::otlp;
// Not `resource`: that would collide with xrpl::resource (the resource-accounting
// namespace), which encloses every use site below. Inner-scope lookup would find
// that namespace instead of this file-scope alias.
namespace otel_resource = opentelemetry::sdk::resource;

namespace {

// Microsecond-valued duration histogram instrument names. Each is
// referenced twice — once to register the explicit-bucket view and once
// to create the instrument — so they are named constants to keep the two
// sites in sync (a mismatch would silently drop the bucket override).
constexpr char kJobQueuedDurationUs[] = "job_queued_us";
constexpr char kJobRunningDurationUs[] = "job_running_us";
constexpr char kRpcMethodDurationUs[] = "rpc_method_us";

// Attribute (label) keys for the job instruments. Each is referenced from
// several record sites, and a counter and its histogram must carry exactly
// the same key spelling or the two series cannot be joined in a query.
constexpr char kJobTypeLabel[] = "job_type";
constexpr char kHandlerLabel[] = "handler";

/**
 * Bucket boundaries for microsecond-valued duration instruments.
 *
 * 100 µs, 500 µs, 1 ms, 5 ms, 10 ms, 25 ms, 50 ms, 100 ms, 250 ms, 500 ms,
 * 1 s, 2.5 s, 5 s, 10 s, 30 s, 60 s. Covers sub-millisecond jobs through
 * multi-second stalls without saturating.
 */
constexpr std::array kMicrosecondBoundaries{
    100.0,
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
    60'000'000.0};

/**
 * Bucket boundaries for latencies that are normally sub-millisecond.
 *
 * 1 µs, 2 µs, 5 µs, 10 µs, 25 µs, 50 µs, 100 µs, 250 µs, 500 µs, 1 ms, 5 ms,
 * 25 ms.
 *
 * kMicrosecondBoundaries starts at 100 µs, which is above the entire range a
 * healthy nodestore read occupies, so every warm read falls in its first
 * bucket and the distribution reads as flat. These edges resolve the warm
 * range instead, while still reaching far enough to show a cold tail against
 * it.
 *
 * Currently unused: no sub-millisecond histogram instrument exists yet. The
 * edges live here so the instrument that records nodestore read latency gets
 * a ladder that fits it, rather than silently inheriting the wrong one.
 */
[[maybe_unused]] constexpr std::array kSubMillisecondBoundaries{
    1.0,
    2.0,
    5.0,
    10.0,
    25.0,
    50.0,
    100.0,
    250.0,
    500.0,
    1'000.0,
    5'000.0,
    25'000.0};

/**
 * Register an explicit-bucket histogram view.
 *
 * The SDK's default boundaries top out at 10,000, so any instrument whose
 * values exceed that saturates and every quantile reads as the ceiling.
 *
 * @param views      The registry to add the view to.
 * @param name       Instrument name to match (e.g. "job_running_us").
 * @param boundaries Bucket upper bounds, ascending.
 */
void
addHistogramView(
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
 * Register the microsecond-ladder view for a duration instrument.
 *
 * Job wait/run times and RPC latencies routinely exceed the SDK default
 * ceiling, so they all share `kMicrosecondBoundaries`.
 *
 * @param views   The registry to add the view to.
 * @param name    Instrument name to match.
 */
void
addMicrosecondHistogramView(metric_sdk::ViewRegistry& views, std::string const& name)
{
    addHistogramView(views, name, {kMicrosecondBoundaries.begin(), kMicrosecondBoundaries.end()});
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

    // Rule for anything added below: this phase may create only instruments
    // whose recording is PUSHED from app code -- counters and histograms. An
    // instrument registered here is live immediately, and the reader thread
    // may invoke a registered callback before the rest of the Application is
    // built, so any observable whose callback reads an Application service
    // belongs in startAsyncGauges(), not here. That includes observable
    // COUNTERS, not just gauges: jq_trans_overflow_total was created here and
    // its callback read getOverlay(), which asserts overlay_ is non-null.
    initExporterAndProvider(endpoint, instanceId);
    initSyncInstruments();

    JLOG(journal_.info()) << "MetricsRegistry: provider and instruments ready";
#else
    (void)endpoint;
    (void)instanceId;
    (void)enabled_;
#endif  // XRPL_ENABLE_TELEMETRY
}

void
MetricsRegistry::startAsyncGauges()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_)
        return;

    // A mis-ordered call must not crash: without a meter there is nothing to
    // create instruments on, so registration is skipped entirely.
    if (!meter_)
    {
        JLOG(journal_.warn()) << "MetricsRegistry: startAsyncGauges() called "
                                 "before start(); no gauges registered";
        return;
    }

    registerAsyncGauges();

    JLOG(journal_.info()) << "MetricsRegistry: started successfully";
#else
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
    otel_resource::ResourceAttributes attrs;
    // Use std::string, not a string literal: ResourceAttributes stores an
    // OTel AttributeValue variant whose char-const* overload binds to bool,
    // so "xrpld" would be recorded as the boolean true. std::string selects
    // the string alternative and the value round-trips as service.name=xrpld.
    attrs[opentelemetry::semconv::service::kServiceName] = std::string("xrpld");
    if (!instanceId.empty())
        attrs[opentelemetry::semconv::service::kServiceInstanceId] = instanceId;
    auto resourceAttrs = otel_resource::Resource::Create(attrs);

    // Build a view registry with explicit microsecond buckets for the
    // duration histograms. Without this they use the SDK default buckets
    // (max 10,000 = 10 ms), saturating every quantile at 10 ms.
    auto views = std::make_unique<metric_sdk::ViewRegistry>();
    addMicrosecondHistogramView(*views, kJobQueuedDurationUs);
    addMicrosecondHistogramView(*views, kJobRunningDurationUs);
    addMicrosecondHistogramView(*views, kRpcMethodDurationUs);
    // Recorded at its PeerImp.cpp call site, not created here, so the name
    // comes from the shared constant both sites use.
    addMicrosecondHistogramView(*views, kGetObjectLookupUs);

    // The remaining two GetObject histograms are not durations, so the
    // microsecond ladder above does not fit them. Both still need explicit
    // boundaries: the SDK default stops at 10,000 and both ranges exceed it.
    //
    // Object counts run 1..kHardMaxReplyNodes (12288). The honest sync path
    // asks for at most 8, so the low buckets are fine-grained and the upper
    // ones follow the charge size bands (64, 1024) up to the hard cap.
    addHistogramView(
        *views,
        kGetObjectRequestObjects,
        {1.0, 2.0, 4.0, 8.0, 16.0, 64.0, 256.0, 1'024.0, 4'096.0, 12'288.0});

    // Charge values span 0 (free tier) to ~99k for a full-size all-miss
    // request. Boundaries bracket the resource thresholds that decide a
    // peer's fate -- kWarningThreshold (5000) and kDropThreshold (25000) --
    // so a dashboard can show how close charges run to each.
    addHistogramView(
        *views,
        kGetObjectCharge,
        {0.0, 100.0, 500.0, 1'000.0, 5'000.0, 10'000.0, 25'000.0, 50'000.0, 100'000.0});

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
    stateChangesCounter_ =
        meter_->CreateUInt64Counter("state_changes_total", "Total operating mode changes");
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
MetricsRegistry::recordJobQueued(std::string_view jobType, std::string_view jobName)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_ || !jobQueuedCounter_)
        return;
    jobQueuedCounter_->Add(
        1,
        {{kJobTypeLabel, std::string(jobType)},
         {kHandlerLabel, std::string(sanitiseHandler(jobName))}});
#else
    (void)jobType;
    (void)jobName;
    (void)enabled_;
#endif
}

void
MetricsRegistry::recordJobStarted(
    std::string_view jobType,
    std::string_view jobName,
    std::int64_t queuedDurUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_ || !jobStartedCounter_)
        return;
    // Build the attribute pair once: both the counter and the histogram
    // must carry the identical label set or they cannot be joined.
    std::string const handler(sanitiseHandler(jobName));
    jobStartedCounter_->Add(1, {{kJobTypeLabel, std::string(jobType)}, {kHandlerLabel, handler}});
    if (jobQueuedDurationHistogram_ && queuedDurUs >= 0)
    {
        // Guard against negative queued durations: the caller derives this
        // from a steady-clock delta that can go slightly negative under clock
        // skew or reordering. The OTel SDK rejects negative histogram values
        // (logging a warning per call), so skip them rather than spam.
        jobQueuedDurationHistogram_->Record(
            static_cast<double>(queuedDurUs),
            {{kJobTypeLabel, std::string(jobType)}, {kHandlerLabel, handler}},
            opentelemetry::context::Context{});
    }
#else
    (void)jobType;
    (void)jobName;
    (void)queuedDurUs;
    (void)enabled_;
#endif
}

void
MetricsRegistry::recordJobFinished(
    std::string_view jobType,
    std::string_view jobName,
    std::int64_t runningDurUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_ || !jobFinishedCounter_)
        return;
    std::string const handler(sanitiseHandler(jobName));
    jobFinishedCounter_->Add(1, {{kJobTypeLabel, std::string(jobType)}, {kHandlerLabel, handler}});
    if (jobRunningDurationHistogram_)
    {
        jobRunningDurationHistogram_->Record(
            static_cast<double>(runningDurUs),
            {{kJobTypeLabel, std::string(jobType)}, {kHandlerLabel, handler}},
            opentelemetry::context::Context{});
    }
#else
    (void)jobType;
    (void)jobName;
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
    registerJqTransOverflowCounter();
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
}

void
MetricsRegistry::registerJqTransOverflowCounter()
{
    // jq_trans_overflow_total is observed from Overlay's existing cumulative
    // atomic (Overlay::getJqTransOverflow()) rather than pushed. The overlay
    // owns the only increment site (PeerImp), so an ObservableCounter reads the
    // live total each collection cycle without threading a push path through
    // develop-owned overlay code.
    //
    // Registered with the gauges, not with the synchronous instruments: the
    // callback reads getOverlay(), which asserts overlay_ is non-null. Arming
    // it any earlier would let a reader tick fire before the overlay exists,
    // and an assert is not caught by the try block below.
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
MetricsRegistry::observeNodeStoreTotals(node_store::Database& db, ObserveFn const& observe)
{
    // Cumulative counters (monotonically increasing).
    observe("node_reads_total", static_cast<std::int64_t>(db.getFetchTotalCount()));
    observe("node_reads_hit", static_cast<std::int64_t>(db.getFetchHitCount()));
    observe("node_writes", static_cast<std::int64_t>(db.getStoreCount()));
    observe("node_written_bytes", static_cast<std::int64_t>(db.getStoreSize()));
    observe("node_read_bytes", static_cast<std::int64_t>(db.getFetchSize()));

    // Cumulative I/O durations, read straight off the atomics.
    observe("node_reads_duration_us", static_cast<std::int64_t>(db.getFetchDurationUs()));
    observe("node_writes_duration_us", static_cast<std::int64_t>(db.getStoreDurationUs()));

    // Mean latencies. A cumulative total cannot separate "every read took
    // 9 us" from "most took 2 and a few took 900", and the second is the
    // cold-store signature: the hit rate reads the same either way, only the
    // latency differs. Each mean is omitted rather than reported as zero
    // when nothing has been read or written, so a dashboard shows a gap
    // instead of a plausible wrong number.
    if (auto const mean = scaledMean(db.getFetchDurationUs(), db.getFetchTotalCount()))
        observe("read_mean_us", *mean);
    if (auto const mean = scaledMean(db.getStoreDurationUs(), db.getStoreCount()))
        observe("write_mean_us", *mean);

    // Write load score (instantaneous).
    observe("write_load", static_cast<std::int64_t>(db.getWriteLoad()));
}

void
MetricsRegistry::observeWritePathDetail(node_store::Database const& db, ObserveFn const& observe)
{
    auto const ws = db.getWriteStats();
    if (!ws)
        return;

    observe("nudb_writers_in_flight", static_cast<std::int64_t>(ws->concurrentWriters));
    observe("nudb_insert_max_us", static_cast<std::int64_t>(ws->insertMaxUs));

    if (auto const mean = scaledMean(ws->insertTotalUs, ws->insertCount))
        observe("nudb_insert_mean_us", *mean);

    // Mean writer depth times 100. NuDB serializes inserts behind one
    // mutex, so this depth is the queue length at that mutex and sits just
    // above 1.0 even under load. An integral gauge would truncate that to 1
    // and lose the whole signal, hence the fixed-point scale -- which the
    // name states, so nobody reads 140 as 140 writers.
    if (auto const mean = scaledMean(ws->depthSum, ws->depthSamples, 100))
        observe("nudb_writer_depth_x100", *mean);
}

void
MetricsRegistry::observeAcquireStats(AcquireStats const& stats, ObserveFn const& observe)
{
    // Published unconditionally: for a counter, zero is the meaningful
    // "no such event yet" reading, unlike for a mean. The diagnostic value
    // is in the pairs -- deferrals rising while timeouts stay flat means the
    // give-up path cannot fire, so an acquisition never ends.
    observe("acquire_deferrals", static_cast<std::int64_t>(stats.getDeferrals()));
    observe("acquire_timeouts", static_cast<std::int64_t>(stats.getTimeouts()));

    // The same two events, narrowed to ledger acquisition. The pair above
    // sums every TimeoutCounter subclass, so a busy replay lane can imitate
    // a stalled ledger acquisition; compare these two instead when asking
    // whether ledger acquisition's give-up path is advancing.
    observe("acquire_ledger_deferrals", static_cast<std::int64_t>(stats.getLedgerDeferrals()));
    observe("acquire_ledger_timeouts", static_cast<std::int64_t>(stats.getLedgerTimeouts()));
    observe("acquire_give_ups", static_cast<std::int64_t>(stats.getGiveUps()));
    observe("acquire_aborts", static_cast<std::int64_t>(stats.getAborts()));
    observe("acquire_aborts_partial", static_cast<std::int64_t>(stats.getAbortsWithPartialWork()));
    observe("acquire_completions", static_cast<std::int64_t>(stats.getCompletions()));
    observe("acquire_sweep_evictions", static_cast<std::int64_t>(stats.getSweepEvictions()));
}

void
MetricsRegistry::observeReadQueue(node_store::Database& db, ObserveFn const& observe)
{
    json::Value obj(json::ValueType::Object);
    db.getCountsJson(obj);

    if (obj.isMember("read_queue"))
        observe("read_queue", static_cast<std::int64_t>(obj["read_queue"].asUInt()));

    // Read thread pool stats (native JSON ints, no jss:: constants).
    for (auto const* key : {"read_request_bundle", "read_threads_running", "read_threads_total"})
    {
        if (obj.isMember(key))
            observe(key, static_cast<std::int64_t>(obj[key].asInt()));
    }
}

void
MetricsRegistry::registerNodeStoreGauge()
{
    // --- Task 9.1: NodeStore I/O gauges ---
    // The cumulative counters (reads, writes, bytes) are also exposed here
    // as observable gauges.  This avoids adding an xrpld dependency into the
    // libxrpl nodestore code — the MetricsRegistry reads the existing atomic
    // counters from Database via its public accessors.
    //
    // Every value multiplexes onto this one gauge through its `metric`
    // label, so a new value needs no new instrument. The body is split
    // across four helpers, one per domain, to stay inside the per-function
    // line budget and to keep each domain testable on its own.
    nodeStoreGauge_ = meter_->CreateInt64ObservableGauge(
        "nodestore_state",
        "NodeStore I/O counters, latencies, write-queue depth and acquisition stalls");
    nodeStoreGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            if (self->callbacksDetached_.load(std::memory_order_acquire))
                return;
            auto& app = self->app_;

            try
            {
                auto& db = app.getNodeStore();

                ObserveFn const observe = [&](char const* name, std::int64_t value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<int64_t>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // Qualified because the enclosing lambda captures nothing:
                // these are static members, and the explicit scope says so.
                MetricsRegistry::observeNodeStoreTotals(db, observe);
                MetricsRegistry::observeWritePathDetail(db, observe);
                MetricsRegistry::observeAcquireStats(app.getAcquireStats(), observe);
                MetricsRegistry::observeReadQueue(db, observe);
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
                    ->Observe(1, {{"version", std::string(build_info::getVersionString())}});
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
                    return build_info::encodeSoftwareVersion(v.substr(firstDigit));
                };
                auto const ownEncoded = encodeVersion(build_info::getVersionString());

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

                // Fee and reserve values from the validated ledger.
                auto const ledger = app.getLedgerMaster().getValidatedLedger();
                if (ledger)
                {
                    auto const& fees = ledger->fees();
                    // Cost of a reference transaction (drops).
                    observe("base_fee_xrp", static_cast<double>(fees.base.drops()));
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
    // Reports the cumulative payload bytes handed to the NodeStore. See the
    // note at the observe() call below: this is logical bytes stored, not
    // on-disk file size, because no accessor for the latter exists. The label
    // value names it that way so it is not read as a filesystem measurement.
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

                // Cumulative payload bytes handed to the NodeStore. This is
                // not an on-disk file size: getStoreSize() sums the object
                // payloads this process has written, so it excludes NuDB's
                // keys, bucket padding and log, and it resets with the
                // process while the files do not. The value comes from
                // Database, not from any backend, so it carries no nudb_
                // prefix -- it reads the same on RocksDB.
                //
                // This is the same call node_written_bytes makes on the
                // nodestore_state gauge, so the two series are equal by
                // construction and their ratio is a constant 1.0. It is not
                // a write-amplification measure. Backend exposes no file
                // size accessor, so there is nothing better to read here;
                // computing one would mean stat()ing the backend's files
                // from the reader thread, which needs a new Backend method
                // rather than a change at this call site.
                observe(
                    "stored_object_bytes", static_cast<int64_t>(app.getNodeStore().getStoreSize()));
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
MetricsRegistry::incrementStateChanges()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (enabled_ && stateChangesCounter_)
        stateChangesCounter_->Add(1);
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
