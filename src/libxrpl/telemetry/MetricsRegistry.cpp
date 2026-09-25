/**
 * MetricsRegistry implementation — the OpenTelemetry metrics pipeline.
 *
 * This file contains:
 * - Construction / destruction logic for the OTel MeterProvider pipeline.
 * - Synchronous instrument creation (counters, histograms) for RPC, job
 *   queue and the external dashboard parity counters.
 * - The record / increment methods app code pushes values through.
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

#include <xrpl/telemetry/MetricsRegistry.h>

// Unguarded because the constructor's `beast::Journal journal` parameter is
// declared in both configurations; only the member it initialises is guarded.
#include <xrpl/beast/utility/Journal.h>

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/basics/Log.h>
#include <xrpl/telemetry/GetObjectMetricNames.h>
#include <xrpl/telemetry/HistogramBuckets.h>
#include <xrpl/telemetry/MetricNames.h>
#include <xrpl/telemetry/RpcMetricNames.h>
#include <xrpl/telemetry/SpanNames.h>
// For networkTypeFromId(), the one xrpl.network.type mapping both export
// paths use, plus noopMeter() and the shared meter name and version.
#include <xrpl/telemetry/Telemetry.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#include <opentelemetry/nostd/shared_ptr.h>
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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
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
 * Register an explicit-bucket histogram view.
 *
 * The SDK's default boundaries top out at 10,000, so any instrument whose
 * values exceed that saturates and every quantile reads as the ceiling. The
 * floor matters just as much and is easier to miss: a ladder whose first edge
 * sits above the mass of the distribution makes every low quantile an
 * interpolation inside bucket 0 -- a number derived from the bucket edge
 * rather than from any sample. Both ends are chosen from measured
 * distributions in HistogramBuckets.h.
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
    auto meterSelector = metric_sdk::MeterSelectorFactory::Create(
        std::string(xrpl::telemetry::kMeterName), std::string(xrpl::telemetry::kMeterVersion), "");
    auto view =
        metric_sdk::ViewFactory::Create(name, "", metric_sdk::AggregationType::kHistogram, config);

    views.AddView(std::move(selector), std::move(meterSelector), std::move(view));
}

/**
 * Register the microsecond-ladder view for a duration instrument.
 *
 * Job wait/run times and RPC latencies routinely exceed the SDK default
 * ceiling, so they all share `buckets::kMicrosecondBuckets`.
 *
 * @param views The registry to add the view to.
 * @param name  Instrument name to match (e.g. "job_running_us").
 */
void
addMicrosecondHistogramView(metric_sdk::ViewRegistry& views, std::string const& name)
{
    addHistogramView(
        views,
        name,
        xrpl::telemetry::buckets::toVector(xrpl::telemetry::buckets::kMicrosecondBuckets));
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
    addHistogramView(
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

/**
 * Register the seconds-ladder view for an online-delete rotation phase.
 *
 * Rotation phases run from seconds to many minutes, far past the SDK default
 * ceiling, so they share `buckets::kRotationPhaseSecondsBuckets`.
 *
 * @param views The registry to add the view to.
 * @param name  Instrument name to match ("rotation_phase_duration_seconds").
 */
void
addRotationPhaseHistogramView(metric_sdk::ViewRegistry& views, std::string const& name)
{
    addHistogramView(
        views,
        name,
        xrpl::telemetry::buckets::toVector(xrpl::telemetry::buckets::kRotationPhaseSecondsBuckets));
}

}  // namespace

#endif  // XRPL_ENABLE_TELEMETRY

namespace xrpl::telemetry {

MetricsRegistry::MetricsRegistry(
    [[maybe_unused]] bool enabled,
    [[maybe_unused]] beast::Journal journal,
    [[maybe_unused]] Options const& options)
    : enabled_(enabled)
#ifdef XRPL_ENABLE_TELEMETRY
    , journal_(journal)
#endif
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!enabled_)
        return;

    // useTls is logged because a collector that requires TLS rejects a
    // plaintext exporter with no local error. The paths are left out.
    JLOG(journal_.info()) << "MetricsRegistry: starting, endpoint=" << options.endpoint
                          << ", serviceName=" << options.serviceName
                          << ", serviceVersion=" << options.serviceVersion
                          << ", instanceId=" << options.serviceInstanceId
                          << ", nodeId=" << options.nodeId << ", networkId=" << options.networkId
                          << ", useTls=" << options.useTls;

    // A broken pipeline must not stop the node. The SDK is third-party code,
    // so the catch-all is deliberate, as in ~ApplicationImp.
    try
    {
        initExporterAndProvider(options);

        // Rule for anything added below: the constructor may create only
        // instruments whose recording is PUSHED from app code -- counters and
        // histograms. An instrument registered here is live immediately, and
        // the reader thread may invoke a registered callback before the rest
        // of the server is built, so any observable whose callback reads live
        // server state belongs in the layer that owns those callbacks, not
        // here. That includes observable COUNTERS, not just gauges:
        // jq_trans_overflow_total was created here and its callback read
        // getOverlay(), which asserts overlay_ is non-null.
        initSyncInstruments();
    }
    catch (std::exception const& e)
    {
        disablePipeline(e.what());
        return;
    }
    catch (...)
    {
        disablePipeline("unknown exception");
        return;
    }

    JLOG(journal_.info()) << "MetricsRegistry: provider and instruments ready";
#endif  // XRPL_ENABLE_TELEMETRY
}

#ifdef XRPL_ENABLE_TELEMETRY
void
MetricsRegistry::disablePipeline(std::string_view reason)
{
    provider_.reset();
    // A no-op meter keeps the invariant the XRPL_METRIC_* macros rely on: an
    // enabled registry always has a meter, so every call site gets an inert
    // instrument here with no check of its own.
    meter_ = noopMeter(kMeterName);
    JLOG(journal_.error()) << "MetricsRegistry: metrics pipeline failed to initialise, "
                              "continuing without native metrics: "
                           << reason;
}
#endif  // XRPL_ENABLE_TELEMETRY

MetricsRegistry::~MetricsRegistry()
{
    stop();
}

#ifdef XRPL_ENABLE_TELEMETRY
void
MetricsRegistry::initExporterAndProvider(Options const& options)
{
    // Configure OTLP/HTTP metric exporter. The TLS settings come from the one
    // [telemetry] block that also drives the trace exporter in Telemetry.cpp,
    // so both exporters reach the collector on the same terms.
    otlp_http::OtlpHttpMetricExporterOptions exporterOpts;
    exporterOpts.url = options.endpoint;
    if (options.useTls)
    {
        exporterOpts.ssl_ca_cert_path = options.tlsCaCertPath;
        exporterOpts.ssl_client_cert_path = options.tlsClientCertPath;
        exporterOpts.ssl_client_key_path = options.tlsClientKeyPath;
    }

    auto exporter = otlp_http::OtlpHttpMetricExporterFactory::Create(exporterOpts);

    // Configure periodic reader with 10-second export interval.
    metric_sdk::PeriodicExportingMetricReaderOptions readerOpts;
    readerOpts.export_interval_millis = std::chrono::milliseconds(10000);
    readerOpts.export_timeout_millis = std::chrono::milliseconds(5000);
    auto reader =
        metric_sdk::PeriodicExportingMetricReaderFactory::Create(std::move(exporter), readerOpts);

    // Stamp the same resource Telemetry::makeMetricsResource() builds for the
    // trace pipeline. Both must agree: a node whose service.name or
    // xrpl.network.type differs between the two pipelines splits its own
    // series, and a dashboard filtering on either label shows only half.
    //
    // Use std::string, never a string literal: ResourceAttributes stores an
    // OTel AttributeValue variant whose char-const* overload binds to bool,
    // so a literal would be recorded as the boolean true.
    otel_resource::ResourceAttributes attrs;
    attrs[opentelemetry::semconv::service::kServiceName] = options.serviceName;
    // int64_t, matching the trace resource. The same key with two types would
    // give the two pipelines incompatible attribute values.
    attrs[std::string(attr::networkId)] = static_cast<int64_t>(options.networkId);
    // Derived here rather than passed in, so the id and the type label cannot
    // disagree. Same helper the trace path uses.
    attrs[std::string(attr::networkType)] = networkTypeFromId(options.networkId);

    // The three below are left off when empty rather than stamped blank. An
    // absent label reads as "not reported"; an empty one looks like a value.
    if (!options.serviceVersion.empty())
        attrs[opentelemetry::semconv::service::kServiceVersion] = options.serviceVersion;
    if (!options.serviceInstanceId.empty())
        attrs[opentelemetry::semconv::service::kServiceInstanceId] = options.serviceInstanceId;
    // xrpl.node.id: the same per-node key the trace resource carries, so
    // metrics and traces resolve to one node.
    if (!options.nodeId.empty())
        attrs[std::string(attr::nodeId)] = options.nodeId;
    auto resourceAttrs = otel_resource::Resource::Create(attrs);

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

    // Recorded at its SHAMapStoreImp RotationPhase destructor, only the view
    // lives here. Seconds ladder from HistogramBuckets.h.
    addRotationPhaseHistogramView(*views, metric::rotationPhaseDurationSeconds);

    // Recorded at its PeerImp.cpp call site, not created here, so the name
    // comes from the shared constant both sites use.
    addMicrosecondHistogramView(*views, kGetObjectLookupUs);

    // Sweep malloc_trim duration. Shares the microsecond ladder rather than
    // getting a bespoke one, and the ladder is what makes it readable: a trim on
    // a small heap lands in the tens-of-microseconds buckets, while a trim on a
    // multi-gigabyte resident heap runs well past 10 ms -- which is exactly the
    // large-existing-database case this signal exists to catch. With the SDK
    // default ceiling of 10,000 every one of those would collapse into the
    // overflow bucket and p95 would read exactly 10 ms however bad it got. The
    // shared ladder's upper reaches (25 ms, 50 ms, 100 ms, 250 ms, 500 ms, 1 s
    // and beyond) resolve those, and its lower reaches (100 us, 500 us) resolve
    // the healthy fresh-node case, so a per-instrument ladder would add a second
    // thing to maintain for no extra resolution.
    addMicrosecondHistogramView(*views, metric::sweepMallocTrimUs);

    // Millisecond dial/resolve latencies. Both exceed the SDK default ceiling
    // of 10,000: the dial timer is 15 s, so without an explicit ladder every
    // timed-out dial lands in the overflow bucket and p95 reads exactly 10 s
    // however bad it gets. The 15 s boundary sits on its own so a timeout is
    // distinguishable from merely slow.
    addHistogramView(
        *views,
        metric::dnsResolveLatencyMs,
        {1.0,
         5.0,
         10.0,
         25.0,
         50.0,
         100.0,
         250.0,
         500.0,
         1'000.0,
         2'500.0,
         5'000.0,
         10'000.0,
         15'000.0,
         20'000.0,
         30'000.0});
    addHistogramView(
        *views,
        metric::overlayDialLatencyMs,
        {1.0,
         5.0,
         10.0,
         25.0,
         50.0,
         100.0,
         250.0,
         500.0,
         1'000.0,
         2'500.0,
         5'000.0,
         10'000.0,
         15'000.0,
         20'000.0,
         30'000.0});

    // The remaining two GetObject histograms are not durations, so the
    // microsecond ladder above does not fit them. Both still need explicit
    // boundaries: the SDK default stops at 10,000 and both ranges exceed it.
    //
    // Object counts run 1..kHardMaxReplyNodes (12288). The honest sync path
    // asks for at most 8, so the low buckets are fine-grained and the upper
    // ones follow the charge size bands (64, 1024) up to the hard cap.
    addHistogramView(
        *views, kGetObjectRequestObjects, buckets::toVector(buckets::kObjectCountBuckets));

    // Charge values span 0 (free tier) to ~99k for a full-size all-miss
    // request. Boundaries bracket the resource thresholds that decide a
    // peer's fate -- kWarningThreshold (5000) and kDropThreshold (25000) --
    // so a dashboard can show how close charges run to each.
    addHistogramView(*views, kGetObjectCharge, buckets::toVector(buckets::kChargeBuckets));

    // The two RPC request-count histograms are recorded at their ServerHandler
    // and PathRequest call sites, so the names come from the shared constants
    // all three sites use. Both are small counts, and the reason they need a
    // view is the FLOOR rather than the ceiling: the SDK default edges start
    // 0, 5, 10, 25, so a batch of one to five sub-requests -- the normal case --
    // would land in a single bucket and every quantile over it would be an
    // interpolation inside that bucket rather than a measurement.
    //
    // The object-count ladder is the fit: its 1, 2, 4, 8, 16 edges sit exactly
    // where both distributions have their mass. Path counts are hard-bounded at
    // kMaxPaths * kMaxAutoSrcCur = 352, well under its 12288 top. Batch sizes
    // have no such cap; see the ceiling note in RpcMetricNames.h.
    addHistogramView(*views, kRpcBatchSize, buckets::toVector(buckets::kObjectCountBuckets));
    addHistogramView(
        *views, kPathfindDiscoveredPaths, buckets::toVector(buckets::kObjectCountBuckets));

    // Create MeterProvider with resource, then attach the metric reader.
    provider_ = metric_sdk::MeterProviderFactory::Create(std::move(views), resourceAttrs);
    provider_->AddMetricReader(std::move(reader));

    // Get a meter for all xrpld instruments.
    meter_ = provider_->GetMeter(std::string(kMeterName), std::string(kMeterVersion));
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
    jobStallCounter_ = meter_->CreateUInt64Counter(
        metric::jobqStallTotal, "Jobs whose run time reached the 1 s stall threshold");
    jobQueuedDurationHistogram_ = meter_->CreateDoubleHistogram(
        kJobQueuedDurationUs, "Time jobs spent waiting in the queue (microseconds)");
    jobRunningDurationHistogram_ =
        meter_->CreateDoubleHistogram(kJobRunningDurationUs, "Job execution time in microseconds");

    // --- External dashboard parity counters ---
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
    ledgerHistoryMismatchCounter_ = meter_->CreateUInt64Counter(
        metric::ledgerHistoryMismatchTotal, "Total built-vs-validated ledger mismatches by reason");
    txqExpiredCounter_ = meter_->CreateUInt64Counter(
        "txq_expired_total", "Total transactions expired out of the transaction queue");
    txqDroppedCounter_ = meter_->CreateUInt64Counter(
        "txq_dropped_total", "Total transactions refused admission to the queue by reason");
    // Note: validation_agreements_total / validation_missed_total are monotonic
    // ObservableCounters owned by the observable-gauge layer.
}
#endif  // XRPL_ENABLE_TELEMETRY

void
MetricsRegistry::stop()
{
#ifdef XRPL_ENABLE_TELEMETRY
    // Store Stopped with release ordering BEFORE the pipeline goes away.
    // Every recording thread reads phase_ through recording() with acquire
    // ordering, so any record that has not yet passed the gate will see
    // Stopped and skip. Idempotent: destructor calls this after run() or
    // ~ApplicationImp already did.
    phase_.store(Phase::Stopped, std::memory_order_release);
    if (!provider_)
        return;

    JLOG(journal_.info()) << "MetricsRegistry: stopping";

    // meter_ is left alone on purpose. Job threads are still running here and
    // may be inside a macro, so writing meter_ would race with their read.
    // The recording() gate is what keeps them off the dying pipeline: only the
    // macros read meter_, and none of them does so once phase_ is Stopped.
    //
    // SDK teardown order: Shutdown() stops the PeriodicExportingMetricReader
    // thread (so no further gauge callbacks fire) and performs the final
    // collect-and-export drain itself. The trailing ForceFlush() is a
    // redundant safety net (a no-op once the reader is shut down), then
    // reset() destroys the provider.
    //
    // provider_.reset() destroys MeterProvider -> MeterContext -> ViewRegistry
    // -> each View -> its shared_ptr<AggregationConfig>. Live SDK
    // SyncMetricStorage instances cached in call-site statics still hold a
    // raw AggregationConfig pointer; a Record with a NEW attribute set after
    // this point would fire the factory lambda and deref that dangling
    // pointer, and a late meter()->CreateXxx would return null.
    provider_->Shutdown();
    provider_->ForceFlush();
    provider_.reset();

    JLOG(journal_.info()) << "MetricsRegistry: stopped";
#endif  // XRPL_ENABLE_TELEMETRY
}

// This reads provider_ when telemetry is compiled in and touches no member
// when it is not, so clang-tidy asks for it to be static. Making it static
// would give the two builds different signatures.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
bool
MetricsRegistry::hasPipeline() const noexcept
{
#ifdef XRPL_ENABLE_TELEMETRY
    return provider_ != nullptr;
#else
    return false;
#endif
}
// NOLINTEND(readability-convert-member-functions-to-static)

// -----------------------------------------------------------------
// Synchronous instrument recording — RPC metrics
// -----------------------------------------------------------------

void
MetricsRegistry::recordRpcStarted([[maybe_unused]] std::string_view method)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!recording() || !rpcStartedCounter_)
        return;
    rpcStartedCounter_->Add(1, {{"method", std::string(method)}});
#endif
}

void
MetricsRegistry::recordRpcFinished(
    [[maybe_unused]] std::string_view method,
    [[maybe_unused]] std::int64_t durationUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!recording() || !rpcFinishedCounter_)
        return;
    rpcFinishedCounter_->Add(1, {{"method", std::string(method)}});
    if (rpcDurationHistogram_)
    {
        rpcDurationHistogram_->Record(
            static_cast<double>(durationUs),
            {{"method", std::string(method)}},
            opentelemetry::context::Context{});
    }
#endif
}

void
MetricsRegistry::recordRpcErrored(
    [[maybe_unused]] std::string_view method,
    [[maybe_unused]] std::int64_t durationUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!recording() || !rpcErroredCounter_)
        return;
    rpcErroredCounter_->Add(1, {{"method", std::string(method)}});
    if (rpcDurationHistogram_)
    {
        rpcDurationHistogram_->Record(
            static_cast<double>(durationUs),
            {{"method", std::string(method)}},
            opentelemetry::context::Context{});
    }
#endif
}

// -----------------------------------------------------------------
// Synchronous instrument recording — Job Queue metrics
// -----------------------------------------------------------------

void
MetricsRegistry::recordJobQueued(
    [[maybe_unused]] std::string_view jobType,
    [[maybe_unused]] std::string_view jobName)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!recording() || !jobQueuedCounter_)
        return;
    jobQueuedCounter_->Add(
        1,
        {{label::jobType, std::string(jobType)},
         {label::handler, std::string(sanitiseHandler(jobName))}});
#endif
}

void
MetricsRegistry::recordJobStarted(
    [[maybe_unused]] std::string_view jobType,
    [[maybe_unused]] std::string_view jobName,
    [[maybe_unused]] std::int64_t queuedDurUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!recording() || !jobStartedCounter_)
        return;
    // Build the attribute pair once: both the counter and the histogram
    // must carry the identical label set or they cannot be joined.
    std::string const handler(sanitiseHandler(jobName));
    jobStartedCounter_->Add(1, {{label::jobType, std::string(jobType)}, {label::handler, handler}});
    if (jobQueuedDurationHistogram_ && queuedDurUs >= 0)
    {
        // Guard against negative queued durations: the caller derives this
        // from a steady-clock delta that can go slightly negative under clock
        // skew or reordering. The OTel SDK rejects negative histogram values
        // (logging a warning per call), so skip them rather than spam.
        jobQueuedDurationHistogram_->Record(
            static_cast<double>(queuedDurUs),
            {{label::jobType, std::string(jobType)}, {label::handler, handler}},
            opentelemetry::context::Context{});
    }
#endif
}

void
MetricsRegistry::recordJobFinished(
    [[maybe_unused]] std::string_view jobType,
    [[maybe_unused]] std::string_view jobName,
    [[maybe_unused]] std::int64_t runningDurUs)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (!recording() || !jobFinishedCounter_)
        return;
    std::string const handler(sanitiseHandler(jobName));
    jobFinishedCounter_->Add(
        1, {{label::jobType, std::string(jobType)}, {label::handler, handler}});
    if (jobRunningDurationHistogram_)
    {
        jobRunningDurationHistogram_->Record(
            static_cast<double>(runningDurUs),
            {{label::jobType, std::string(jobType)}, {label::handler, handler}},
            opentelemetry::context::Context{});
    }
    // One compare per job finish. A process-wide freeze shows up here as
    // several job types crossing the bar in the same second.
    if (runningDurUs >= kJobStallThresholdUs && jobStallCounter_)
        jobStallCounter_->Add(1, {{label::jobType, std::string(jobType)}});
#endif
}

// -----------------------------------------------------------------
// External dashboard parity counter increments
// -----------------------------------------------------------------

void
MetricsRegistry::incrementLedgersClosed()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (recording() && ledgersClosedCounter_)
        ledgersClosedCounter_->Add(1);
#endif
}

void
MetricsRegistry::incrementValidationsSent()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (recording() && validationsSentCounter_)
        validationsSentCounter_->Add(1);
#endif
}

void
MetricsRegistry::incrementValidationsChecked()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (recording() && validationsCheckedCounter_)
        validationsCheckedCounter_->Add(1);
#endif
}

void
MetricsRegistry::incrementLedgerHistoryMismatch(std::string_view reason)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (recording() && ledgerHistoryMismatchCounter_)
        ledgerHistoryMismatchCounter_->Add(1, {{"reason", std::string(reason)}});
#endif
}

void
MetricsRegistry::incrementTxqExpired()
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (recording() && txqExpiredCounter_)
        txqExpiredCounter_->Add(1);
#endif
}

void
MetricsRegistry::incrementTxqDropped(std::string_view reason)
{
#ifdef XRPL_ENABLE_TELEMETRY
    if (recording() && txqDroppedCounter_)
        txqDroppedCounter_->Add(1, {{"reason", std::string(reason)}});
#endif
}

}  // namespace xrpl::telemetry
