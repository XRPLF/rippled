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
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/core/TimeKeeper.h>
#include <xrpld/overlay/Overlay.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/UptimeClock.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>

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

#include <algorithm>
#include <sstream>

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

    // --- External dashboard parity counters (Task 7.14) ---
    ledgersClosedCounter_ = meter_->CreateUInt64Counter(
        "rippled_ledgers_closed_total", "Total ledgers closed by consensus");
    validationsSentCounter_ = meter_->CreateUInt64Counter(
        "rippled_validations_sent_total", "Total validations sent by this node");
    validationsCheckedCounter_ = meter_->CreateUInt64Counter(
        "rippled_validations_checked_total", "Total network validations received and checked");
    stateChangesCounter_ =
        meter_->CreateUInt64Counter("rippled_state_changes_total", "Total operating mode changes");
    jqTransOverflowCounter_ = meter_->CreateUInt64Counter(
        "rippled_jq_trans_overflow_total", "Total job queue transaction overflows");

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
                    observe(
                        "read_request_bundle",
                        static_cast<int64_t>(obj["read_request_bundle"].asInt()));
                if (obj.isMember("read_threads_running"))
                    observe(
                        "read_threads_running",
                        static_cast<int64_t>(obj["read_threads_running"].asInt()));
                if (obj.isMember("read_threads_total"))
                    observe(
                        "read_threads_total",
                        static_cast<int64_t>(obj["read_threads_total"].asInt()));
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip on error.
            }
        },
        this);

    // --- Task 9.7a: Server info gauges ---
    serverInfoGauge_ =
        meter_->CreateInt64ObservableGauge("rippled_server_info", "Server-level health metrics");
    serverInfoGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
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
                observe("peers", static_cast<int64_t>(app.overlay().size()));

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
                    static_cast<int64_t>(app.overlay().getPeerDisconnectCharges()));

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
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);

    // --- Task 9.7b: Build info gauge ---
    buildInfoGauge_ =
        meter_->CreateInt64ObservableGauge("rippled_build_info", "Build version information");
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

    // --- Task 9.7c: Complete ledgers range gauge ---
    completeLedgersGauge_ = meter_->CreateInt64ObservableGauge(
        "rippled_complete_ledgers", "Complete ledger range start/end pairs");
    completeLedgersGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
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

    // --- Task 9.7d: Database size and fetch rate gauges ---
    dbMetricsGauge_ = meter_->CreateInt64ObservableGauge(
        "rippled_db_metrics", "Database storage sizes and fetch rates");
    dbMetricsGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
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

    // --- Task 7.9: Validator health gauges ---
    validatorHealthGauge_ = meter_->CreateDoubleObservableGauge(
        "rippled_validator_health", "Validator health indicators");
    validatorHealthGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
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

    // --- Task 7.10: Peer quality gauges ---
    // Uses Peer::json() to read latency and version since those accessors
    // are not on the abstract Peer interface (they live on PeerImp).
    peerQualityGauge_ =
        meter_->CreateDoubleObservableGauge("rippled_peer_quality", "Peer network quality metrics");
    peerQualityGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            auto& app = self->app_;

            try
            {
                auto observe = [&](char const* name, double value) {
                    opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                        opentelemetry::metrics::ObserverResultT<double>>>(result)
                        ->Observe(value, {{"metric", name}});
                };

                // Collect latencies and version info from each peer's JSON.
                std::vector<int> latencies;
                int higherVersionCount = 0;
                int totalPeers = 0;
                auto const ownVersion = std::string(BuildInfo::getVersionString());

                app.overlay().foreach([&](std::shared_ptr<Peer> const& peer) {
                    ++totalPeers;
                    auto const pj = peer->json();
                    if (pj.isMember(jss::latency))
                    {
                        latencies.push_back(pj[jss::latency].asInt());
                    }
                    if (pj.isMember(jss::version))
                    {
                        auto const pv = pj[jss::version].asString();
                        if (!pv.empty() && pv > ownVersion)
                            ++higherVersionCount;
                    }
                });

                // P90 latency across connected peers.
                if (!latencies.empty())
                {
                    std::sort(latencies.begin(), latencies.end());
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

                // Count peers that are insane/diverged (tracking ==
                // Tracking::diverged). Not directly available from the Peer
                // interface, so we count peers with negative or zero latency
                // as a proxy for unreachable/diverged state.
                // TODO: expose PeerImp::tracking_ via the Peer interface for
                //       a precise count.
                observe("peers_insane_count", 0.0);

                // Binary flag: recommend upgrade if >60% run a newer version.
                observe("upgrade_recommended", higherPct > 60.0 ? 1.0 : 0.0);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);

    // --- Task 7.11: Ledger economy gauges ---
    ledgerEconomyGauge_ = meter_->CreateDoubleObservableGauge(
        "rippled_ledger_economy", "Ledger fee and economy metrics");
    ledgerEconomyGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
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
                    observe(
                        "reserve_base_xrp", static_cast<double>(fees.accountReserve(0).drops()));
                    observe("reserve_inc_xrp", static_cast<double>(fees.increment.drops()));
                }

                // Seconds since the last validated ledger closed.
                auto const age = app.getLedgerMaster().getValidatedLedgerAge();
                observe("ledger_age_seconds", static_cast<double>(age.count()));

                // Transaction rate: tx count from current open ledger divided
                // by ledger interval. A rough approximation — a proper
                // smoothed rate would require storing previous counts.
                if (ledger)
                {
                    auto txCount = ledger->txs.size();
                    // Approximate rate: txs per ~4s ledger interval.
                    observe("transaction_rate", static_cast<double>(txCount) / 4.0);
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

    // --- Task 7.12: State tracking gauges ---
    stateTrackingGauge_ = meter_->CreateDoubleObservableGauge(
        "rippled_state_tracking", "Node state and mode tracking");
    stateTrackingGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
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
                double stateValue = static_cast<double>(mode);

                // If FULL, refine using consensus info for validating/proposing.
                if (mode == OperatingMode::FULL)
                {
                    auto const info = app.getOPs().getConsensusInfo();
                    if (info.isMember("proposing") && info["proposing"].asBool())
                        stateValue = 6.0;
                    else if (info.isMember("validating") && info["validating"].asBool())
                        stateValue = 5.0;
                }
                observe("state_value", stateValue);

                // TODO: Wire time_in_current_state_seconds to StateAccounting
                // once a public accessor is available on NetworkOPs.
                observe("time_in_current_state_seconds", 0.0);
            }
            catch (...)  // NOLINT(bugprone-empty-catch)
            {
                // Silently skip if services are not yet ready.
            }
        },
        this);

    // --- Task 7.13: Storage detail gauges ---
    // Reports NuDB on-disk size via the NodeStore JSON counters interface.
    storageDetailGauge_ =
        meter_->CreateInt64ObservableGauge("rippled_storage_detail", "Storage detail metrics");
    storageDetailGauge_->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto* self = static_cast<MetricsRegistry*>(state);
            auto& app = self->app_;

            try
            {
                // Use getCountsJson which includes backend-reported sizes.
                Json::Value obj(Json::objectValue);
                app.getNodeStore().getCountsJson(obj);

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

    // TODO(Task 7.15): Wire validation agreement gauge to ValidationTracker
    // after rebase brings ValidationTracker from Phase 7 into this branch.
    // Will observe: agreement_pct_1h, agreement_pct_24h, agreements_1h,
    // missed_1h, agreements_24h, missed_24h from validationTracker_.
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
MetricsRegistry::incrementJqTransOverflow()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (enabled_ && jqTransOverflowCounter_)
        jqTransOverflowCounter_->Add(1);
#endif
}

}  // namespace telemetry
}  // namespace xrpl
