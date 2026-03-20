/** MetricsRegistry implementation — OpenTelemetry metric instruments for rippled.

    This file contains:
    - Construction / destruction logic for the OTel MeterProvider pipeline.
    - Synchronous instrument creation (counters, histograms) for RPC, job
      queue, and NodeStore I/O metrics.
    - Observable gauge callback registration for cache hit rates, TxQ state,
      CountedObject instances, load factors, and NodeStore queue depth.
    - No-op stubs when XRPL_ENABLE_TELEMETRY is not defined.
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

#include <xrpld/app/ledger/AcceptedLedger.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/misc/TxQ.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/server/LoadFeeTrack.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/resource/semantic_conventions.h>

namespace metric_sdk = opentelemetry::sdk::metrics;
namespace otlp_http = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;

#endif  // XRPL_ENABLE_TELEMETRY

namespace xrpl {
namespace telemetry {

MetricsRegistry::MetricsRegistry(bool enabled, ServiceRegistry& app, beast::Journal journal)
    : enabled_(enabled), app_(app), journal_(journal)
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

    // Configure resource attributes so Prometheus exported_instance labels
    // distinguish metrics from different nodes (matches OTelCollector setup).
    resource::ResourceAttributes attrs;
    attrs[resource::SemanticConventions::kServiceName] = "rippled";
    if (!instanceId.empty())
        attrs[resource::SemanticConventions::kServiceInstanceId] = instanceId;
    auto resourceAttrs = resource::Resource::Create(attrs);

    // Create MeterProvider with resource, then attach the metric reader.
    provider_ = metric_sdk::MeterProviderFactory::Create(
        std::make_unique<metric_sdk::ViewRegistry>(), resourceAttrs);
    provider_->AddMetricReader(std::move(reader));

    // Get a meter for all rippled instruments.
    meter_ = provider_->GetMeter("rippled", "1.0.0");

    // --- Create synchronous instruments ---

    // RPC per-method counters and histogram.
    rpcStartedCounter_ = meter_->CreateUInt64Counter(
        "rippled_rpc_method_started_total", "Total RPC method calls started");
    rpcFinishedCounter_ = meter_->CreateUInt64Counter(
        "rippled_rpc_method_finished_total", "Total RPC method calls completed successfully");
    rpcErroredCounter_ = meter_->CreateUInt64Counter(
        "rippled_rpc_method_errored_total", "Total RPC method calls that errored");
    rpcDurationHistogram_ = meter_->CreateDoubleHistogram(
        "rippled_rpc_method_duration_us", "RPC method execution time in microseconds");

    // Job queue per-type counters and histograms.
    jobQueuedCounter_ =
        meter_->CreateUInt64Counter("rippled_job_queued_total", "Total jobs enqueued");
    jobStartedCounter_ =
        meter_->CreateUInt64Counter("rippled_job_started_total", "Total jobs started");
    jobFinishedCounter_ =
        meter_->CreateUInt64Counter("rippled_job_finished_total", "Total jobs completed");
    jobQueuedDurationHistogram_ = meter_->CreateDoubleHistogram(
        "rippled_job_queued_duration_us", "Time jobs spent waiting in the queue (microseconds)");
    jobRunningDurationHistogram_ = meter_->CreateDoubleHistogram(
        "rippled_job_running_duration_us", "Job execution time in microseconds");

    // Register all observable (async) gauges.
    registerAsyncGauges();

    JLOG(journal_.info()) << "MetricsRegistry: started successfully";
#else
    (void)endpoint;
#endif  // XRPL_ENABLE_TELEMETRY
}

void
MetricsRegistry::stop()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!provider_)
        return;

    JLOG(journal_.info()) << "MetricsRegistry: stopping";

    // Force-flush any pending metrics, then destroy the provider.
    // This stops the PeriodicExportingMetricReader, which in turn
    // stops invoking observable gauge callbacks.  No explicit
    // RemoveCallback is needed — the provider destruction handles it.
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
        rpcDurationHistogram_->Record(
            static_cast<double>(durationUs),
            {{"method", std::string(method)}},
            opentelemetry::context::Context{});
#else
    (void)method;
    (void)durationUs;
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
        rpcDurationHistogram_->Record(
            static_cast<double>(durationUs),
            {{"method", std::string(method)}},
            opentelemetry::context::Context{});
#else
    (void)method;
    (void)durationUs;
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
#endif
}

void
MetricsRegistry::recordJobStarted(std::string_view jobType, std::int64_t queuedDurUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_ || !jobStartedCounter_)
        return;
    jobStartedCounter_->Add(1, {{"job_type", std::string(jobType)}});
    if (jobQueuedDurationHistogram_)
        jobQueuedDurationHistogram_->Record(
            static_cast<double>(queuedDurUs),
            {{"job_type", std::string(jobType)}},
            opentelemetry::context::Context{});
#else
    (void)jobType;
    (void)queuedDurUs;
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
        jobRunningDurationHistogram_->Record(
            static_cast<double>(runningDurUs),
            {{"job_type", std::string(jobType)}},
            opentelemetry::context::Context{});
#else
    (void)jobType;
    (void)runningDurUs;
#endif
}

// -----------------------------------------------------------------
// Observable gauge callbacks (Tasks 9.1, 9.2, 9.3, 9.6, 9.7)
// -----------------------------------------------------------------

#ifdef XRPL_ENABLE_TELEMETRY

void
MetricsRegistry::registerAsyncGauges()
{
    // --- Task 9.2: Cache hit rate and size gauges ---
    cacheHitRateGauge_ =
        meter_->CreateDoubleObservableGauge("rippled_cache_metrics", "Cache hit rates and sizes");
    cacheHitRateGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            auto& app = self->app_;

            try
            {
                // SLE cache hit rate (0.0 - 1.0).
                auto sleRate = app.cachedSLEs().rate();
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
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);

    // --- Task 9.3: TxQ metrics gauges ---
    txqGauge_ =
        meter_->CreateDoubleObservableGauge("rippled_txq_metrics", "Transaction queue metrics");
    txqGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            auto& app = self->app_;

            try
            {
                auto const metrics = app.getTxQ().getMetrics(*app.openLedger().current());

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

    // --- Task 9.6: Counted object instance gauges ---
    objectCountGauge_ = meter_->CreateInt64ObservableGauge(
        "rippled_object_count", "Live instance counts for key internal object types");
    objectCountGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* /* state */) {
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

    // --- Task 9.7: Load factor breakdown gauges ---
    loadFactorGauge_ = meter_->CreateDoubleObservableGauge(
        "rippled_load_factor_metrics", "Fee load factor breakdown");
    loadFactorGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
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
                auto const metrics = app.getTxQ().getMetrics(*app.openLedger().current());
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
                    double feeEscalation = static_cast<double>(metrics.openLedgerFeeLevel.fee()) *
                        loadBaseServer / refLevel;
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

    // --- Task 9.1: NodeStore I/O gauges ---
    // The cumulative counters (reads, writes, bytes) are also exposed here
    // as observable gauges.  This avoids adding an xrpld dependency into the
    // libxrpl nodestore code — the MetricsRegistry reads the existing atomic
    // counters from Database via its public accessors.
    nodeStoreGauge_ = meter_->CreateInt64ObservableGauge(
        "rippled_nodestore_state", "NodeStore I/O counters, queue depth, and write load");
    nodeStoreGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
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
                Json::Value obj(Json::objectValue);
                db.getCountsJson(obj);
                if (obj.isMember("read_queue"))
                {
                    observe("read_queue", static_cast<int64_t>(obj["read_queue"].asUInt()));
                }
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);
}

#endif  // XRPL_ENABLE_TELEMETRY

}  // namespace telemetry
}  // namespace xrpl
