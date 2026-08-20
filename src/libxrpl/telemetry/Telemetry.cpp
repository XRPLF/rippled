/**
 * OpenTelemetry SDK implementation of the Telemetry interface.
 *
 * Compiled only when XRPL_ENABLE_TELEMETRY is defined (via CMake
 * telemetry=ON). Contains:
 *
 * - FilteringSpanProcessor: decorator that drops spans marked with
 * kDiscardedAttr before they enter the batch export queue.
 * - TelemetryImpl: configures the OTel SDK with an OTLP/HTTP exporter,
 * FilteringSpanProcessor wrapping a batch span processor,
 * trace-ID-ratio sampler, and resource attributes.
 * - NullTelemetryOtel: no-op fallback used when telemetry is compiled in
 * but disabled at runtime (enabled=0 in config).
 * - makeTelemetry(): factory that selects the appropriate implementation.
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/Telemetry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/telemetry/CoroAwareContextStorage.h>
#include <xrpl/telemetry/DeterministicIdGenerator.h>
#include <xrpl/telemetry/DiscardFlag.h>
#include <xrpl/telemetry/SpanNames.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/noop.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/sdk/metrics/aggregation/aggregation_config.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/instruments.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/view_factory.h>
#include <opentelemetry/sdk/metrics/view/view_registry.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_options.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/sampler.h>
#include <opentelemetry/sdk/trace/samplers/parent_factory.h>
#include <opentelemetry/sdk/trace/samplers/trace_id_ratio.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/semconv/incubating/service_attributes.h>
#include <opentelemetry/trace/noop.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/span_startoptions.h>
#include <opentelemetry/trace/tracer.h>
#include <opentelemetry/trace/tracer_provider.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xrpl::telemetry {

namespace {

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace otlp_http = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;

/**
 * SpanProcessor decorator that drops discarded spans.
 *
 * Wraps a delegate processor (typically BatchSpanProcessor). In OnEnd(),
 * calls DiscardScope::isActive(). If the calling thread is inside a
 * DiscardScope (entered by SpanGuard::discard()), the span is silently
 * dropped — never entering the batch queue, never sent over the network,
 * never stored.
 *
 * Uses a thread-local flag rather than inspecting Recordable attributes
 * because the Recordable type varies by exporter (SpanData for simple
 * exporters, OtlpRecordable for OTLP) and none expose a uniform getter.
 * The flag is safe because Span::End() calls OnEnd() synchronously on
 * the same thread.
 *
 * All other methods delegate directly to the wrapped processor.
 *
 * Dependency diagram:
 *
 * +---------------------------+
 * | FilteringSpanProcessor    |
 * +---------------------------+
 * | - delegate_ : unique_ptr  |
 * |   <SpanProcessor>         |
 * +---------------------------+
 * |  wraps
 * +---------+-----------+
 * | BatchSpanProcessor  |
 * +---------------------+
 *
 * @note Thread safety: OnEnd() may be called concurrently from multiple
 * threads. The discard flag behind DiscardScope is thread-local, so each
 * thread's discard state is independent — no synchronization needed.
 */
class FilteringSpanProcessor : public trace_sdk::SpanProcessor
{
    std::unique_ptr<trace_sdk::SpanProcessor> delegate_;

public:
    explicit FilteringSpanProcessor(std::unique_ptr<trace_sdk::SpanProcessor> delegate)
        : delegate_(std::move(delegate))
    {
    }

    std::unique_ptr<trace_sdk::Recordable>
    MakeRecordable() noexcept override
    {
        return delegate_->MakeRecordable();
    }

    void
    OnStart(
        trace_sdk::Recordable& span,
        opentelemetry::trace::SpanContext const& parentContext) noexcept override
    {
        delegate_->OnStart(span, parentContext);
    }

    void
    OnEnd(std::unique_ptr<trace_sdk::Recordable>&& span) noexcept override
    {
        if (DiscardScope::isActive())
        {
            // SpanGuard::discard() is inside a DiscardScope on this thread,
            // which it entered just before calling Span::End() — and End()
            // invokes OnEnd() synchronously. Drop the span.
            return;
        }
        delegate_->OnEnd(std::move(span));
    }

    bool
    ForceFlush(
        std::chrono::microseconds timeout = std::chrono::microseconds::max()) noexcept override
    {
        return delegate_->ForceFlush(timeout);
    }

    bool
    Shutdown(std::chrono::microseconds timeout = std::chrono::microseconds::max()) noexcept override
    {
        return delegate_->Shutdown(timeout);
    }
};

/**
 * No-op implementation used when XRPL_ENABLE_TELEMETRY is defined but
 * setup.enabled is false at runtime.
 *
 * Lives in the anonymous namespace so there is no ODR conflict with the
 * NullTelemetry in NullTelemetry.cpp.
 */
class NullTelemetryOtel : public Telemetry
{
    /**
     * Retained configuration (unused, kept for diagnostic access).
     */
    Setup const setup_;

public:
    explicit NullTelemetryOtel(Setup setup) : setup_(std::move(setup))
    {
    }

    void
    start() override
    {
        Telemetry::setInstance(this);
    }

    void
    stop() override
    {
        Telemetry::setInstance(nullptr);
    }

    [[nodiscard]] bool
    isEnabled() const override
    {
        return false;
    }

    [[nodiscard]] bool
    shouldTraceTransactions() const override
    {
        return false;
    }

    [[nodiscard]] bool
    shouldTraceConsensus() const override
    {
        return false;
    }

    [[nodiscard]] bool
    shouldTraceRpc() const override
    {
        return false;
    }

    [[nodiscard]] bool
    shouldTracePeer() const override
    {
        return false;
    }

    [[nodiscard]] bool
    shouldTraceLedger() const override
    {
        return false;
    }

    [[nodiscard]] std::string const&
    getConsensusTraceStrategy() const override
    {
        return setup_.consensusTraceStrategy;
    }

    opentelemetry::nostd::shared_ptr<trace_api::Tracer>
    getTracer(std::string_view) override
    {
        static auto noopTracer =
            opentelemetry::nostd::shared_ptr<trace_api::Tracer>(new trace_api::NoopTracer());
        return noopTracer;
    }

    opentelemetry::nostd::shared_ptr<metrics_api::Meter>
    getMeter(std::string_view name) override
    {
        // Serve a meter from a process-wide noop provider, mirroring the
        // noop tracer above. Instruments created from it are inert.
        static auto noopProvider = opentelemetry::nostd::shared_ptr<metrics_api::MeterProvider>(
            new metrics_api::NoopMeterProvider());
        return noopProvider->GetMeter(std::string(name), std::string(kMeterVersion));
    }

    opentelemetry::nostd::shared_ptr<trace_api::Span>
    startSpan(std::string_view, trace_api::SpanKind) override
    {
        return opentelemetry::nostd::shared_ptr<trace_api::Span>(new trace_api::NoopSpan(nullptr));
    }

    opentelemetry::nostd::shared_ptr<trace_api::Span>
    startSpan(std::string_view, opentelemetry::context::Context const&, trace_api::SpanKind)
        override
    {
        return opentelemetry::nostd::shared_ptr<trace_api::Span>(new trace_api::NoopSpan(nullptr));
    }
};

/**
 * Full OTel SDK implementation that exports trace spans via OTLP/HTTP.
 *
 * Configures an OTLP/HTTP exporter, batch span processor,
 * TraceIdRatioBasedSampler, and resource attributes on start().
 */
class TelemetryImpl : public Telemetry
{
    /**
     * Configuration from the [telemetry] config section.
     * Non-const so setServiceInstanceId() and setNodeId() can update the
     * identity attributes before start() creates the OTel resource.
     */
    Setup setup_;

    /**
     * Journal used for log output during start/stop.
     */
    beast::Journal const journal_;

    /**
     * The SDK TracerProvider that owns the export pipeline.
     *
     * Held as std::shared_ptr so we can call ForceFlush() on shutdown.
     * Wrapped in a nostd::shared_ptr when registered as the global provider.
     */
    std::shared_ptr<trace_sdk::TracerProvider> sdkProvider_;

    /**
     * The SDK MeterProvider that owns the metric export pipeline.
     *
     * Symmetric with sdkProvider_ on the tracing side. Held as
     * std::shared_ptr so we can ForceFlush() on shutdown; wrapped in a
     * nostd::shared_ptr when registered as the global meter provider.
     */
    std::shared_ptr<metrics_sdk::MeterProvider> meterProvider_;

    /**
     * Coroutine-aware runtime-context storage, installed globally so the OTel
     * ambient context follows JobQueue coroutines. Held for the process
     * lifetime because it must outlive every span (SDK requirement).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::context::RuntimeContextStorage> contextStorage_;

    /**
     * Set by stop(), so a second call does nothing.
     */
    bool stopped_{false};

public:
    TelemetryImpl(Setup setup, beast::Journal journal) : setup_(std::move(setup)), journal_(journal)
    {
        // Publish the MeterProvider before any subsystem is constructed; see
        // initMetrics(). setup_.serviceInstanceId is already resolved by the
        // caller, so the resource is complete.
        //
        // A failure must never stop the node starting: the global provider
        // stays noop and every instrument call remains valid.
        try
        {
            initMetrics();
        }
        catch (std::exception const& e)
        {
            JLOG(journal_.error()) << "Telemetry metrics pipeline failed to initialise, "
                                      "continuing without metrics: "
                                   << e.what();
        }
    }

    /**
     * Override the service instance id, for callers that learn it late.
     *
     * Affects only the tracer resource, which start() builds. The metrics
     * resource is built by the constructor and is immutable, so supply the id
     * through Setup to have it on both.
     *
     * @param id The instance id to report on spans.
     */
    void
    setServiceInstanceId(std::string const& id) override
    {
        setup_.serviceInstanceId = id;
    }

    void
    setNodeId(std::string const& id) override
    {
        setup_.nodeId = id;
    }

    void
    start() override
    {
        JLOG(journal_.info()) << "Telemetry starting: endpoint=" << setup_.exporterEndpoint
                              << " sampling=" << setup_.samplingRatio;

        // Configure OTLP HTTP exporter
        otlp_http::OtlpHttpExporterOptions exporterOpts;
        exporterOpts.url = setup_.exporterEndpoint;
        if (setup_.useTls)
        {
            exporterOpts.ssl_ca_cert_path = setup_.tlsCertPath;
            // Present a client cert for mutual TLS. When both paths are
            // empty the connection falls back to one-way (server) TLS.
            exporterOpts.ssl_client_cert_path = setup_.tlsClientCertPath;
            exporterOpts.ssl_client_key_path = setup_.tlsClientKeyPath;
        }

        auto exporter = otlp_http::OtlpHttpExporterFactory::Create(exporterOpts);

        // Configure batch processor
        trace_sdk::BatchSpanProcessorOptions processorOpts;
        processorOpts.max_queue_size = setup_.maxQueueSize;
        processorOpts.schedule_delay_millis = std::chrono::milliseconds(setup_.batchDelay);
        processorOpts.max_export_batch_size = setup_.batchSize;

        auto batchProcessor =
            trace_sdk::BatchSpanProcessorFactory::Create(std::move(exporter), processorOpts);

        // Wrap batch processor with filtering processor that drops spans
        // marked with kDiscardedAttr (via SpanGuard::discard()).
        auto processor = std::make_unique<FilteringSpanProcessor>(std::move(batchProcessor));

        // Configure resource attributes
        auto resourceAttrs = makeTracerResource();

        // Configure sampler. Head sampling is fixed at 1.0 (sample everything);
        // setup_.samplingRatio is not config-driven. Wrap the ratio sampler in a
        // ParentBasedSampler so spans with a remote parent honor the upstream
        // sampled flag — this keeps keep/drop decisions coherent for a single
        // distributed trace spanning multiple nodes. Volume reduction is left to
        // the collector's tail sampling.
        auto rootSampler =
            std::make_shared<trace_sdk::TraceIdRatioBasedSampler>(setup_.samplingRatio);
        auto sampler = trace_sdk::ParentBasedSamplerFactory::Create(std::move(rootSampler));

        // Create TracerProvider with a DeterministicIdGenerator. It returns a
        // deterministic trace_id when a PendingTraceId is active on the thread,
        // else a random one — letting hash-derived roots (introduced on a later
        // branch) become true trace roots. Dormant until such a caller exists.
        sdkProvider_ = trace_sdk::TracerProviderFactory::Create(
            std::move(processor),
            resourceAttrs,
            std::move(sampler),
            std::make_unique<DeterministicIdGenerator>());

        // Install coroutine-aware context storage BEFORE any span is created
        // so the OTel ambient context follows JobQueue coroutines across
        // yield/resume (fixes wrong-thread scope pop; keeps log-trace
        // correlation). Must precede SetTracerProvider and the first span.
        // Not reset in stop(): resetting the storage while spans may still
        // exist is undefined behaviour (SDK), and by stop() all spans are
        // gone, so the storage is simply left installed for process lifetime.
        contextStorage_ =
            opentelemetry::nostd::shared_ptr<opentelemetry::context::RuntimeContextStorage>(
                new CoroAwareContextStorage());
        opentelemetry::context::RuntimeContext::SetRuntimeContextStorage(contextStorage_);

        // Set as global provider
        trace_api::Provider::SetTracerProvider(
            opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>(sdkProvider_));

        // The metrics pipeline (meterProvider_) was already built and published
        // in the constructor via initMetrics(), before any subsystem could
        // create a beast::insight instrument. See initMetrics().

        // Register as the global Telemetry instance so SpanGuard factory
        // methods can access it without callers passing a reference.
        Telemetry::setInstance(this);

        JLOG(journal_.info()) << "Telemetry started successfully";
    }

    /**
     * Build the tracer resource: the process-identity attributes stamped on
     * every exported span.
     *
     * Called from start(), which runs after Application::setup() has injected
     * the node identity, so setup_.nodeId is populated by then.
     *
     * @return The resource attached to the TracerProvider.
     */
    [[nodiscard]] resource::Resource
    makeTracerResource() const
    {
        return resource::Resource::Create({
            {opentelemetry::semconv::service::kServiceName, setup_.serviceName},
            {opentelemetry::semconv::service::kServiceVersion, setup_.serviceVersion},
            {opentelemetry::semconv::service::kServiceInstanceId, setup_.serviceInstanceId},
            {std::string(attr::networkId),
             static_cast<int64_t>(setup_.networkId)},              // LCOV_EXCL_LINE
            {std::string(attr::networkType), setup_.networkType},  // LCOV_EXCL_LINE
            {std::string(attr::nodeId), setup_.nodeId},            // LCOV_EXCL_LINE
        });
    }

    /**
     * Build the metrics resource: the same attributes as the tracer resource
     * in start(), so metrics and traces share one identity.
     *
     * xrpl.node.id is added only when setup_.nodeId already holds a value.
     * setNodeId() runs after the constructor that calls this, so on the normal
     * startup path the attribute is left off rather than stamped blank.
     *
     * @return The resource attached to the MeterProvider.
     */
    [[nodiscard]] resource::Resource
    makeMetricsResource() const
    {
        resource::ResourceAttributes attrs{
            {opentelemetry::semconv::service::kServiceName, setup_.serviceName},
            {opentelemetry::semconv::service::kServiceVersion, setup_.serviceVersion},
            {opentelemetry::semconv::service::kServiceInstanceId, setup_.serviceInstanceId},
            {std::string(attr::networkId), static_cast<int64_t>(setup_.networkId)},
            {std::string(attr::networkType), setup_.networkType},
        };

        if (!setup_.nodeId.empty())
            attrs[std::string(attr::nodeId)] = setup_.nodeId;

        return resource::Resource::Create(attrs);
    }

    /**
     * Build and publish the metrics pipeline (MeterProvider + periodic
     * reader + OTLP exporter + histogram view).
     *
     * Called from the constructor, NOT start(), so the global MeterProvider
     * exists before subsystems construct their beast::insight instruments
     * during ApplicationImp's member-init list. The metrics resource uses
     * setup_.serviceInstanceId from config; it is immutable once the provider
     * is built, so a later node-key setServiceInstanceId() does not affect it.
     * The same applies to setNodeId(): xrpl.node.id reaches this resource only
     * if setup_.nodeId is already populated when the constructor runs.
     */
    void
    initMetrics()
    {
        // Derive the metrics endpoint from the trace endpoint by swapping
        // the trailing "/v1/traces" path for "/v1/metrics". Any other URL
        // shape is used as-is.
        std::string metricsEndpoint = setup_.exporterEndpoint;
        constexpr std::string_view tracesPath{"/v1/traces"};
        if (metricsEndpoint.ends_with(tracesPath))
        {
            metricsEndpoint.replace(
                metricsEndpoint.size() - tracesPath.size(), tracesPath.size(), "/v1/metrics");
        }

        // Configure OTLP HTTP metric exporter, honoring the same TLS
        // options as the trace exporter.
        otlp_http::OtlpHttpMetricExporterOptions metricExporterOpts;
        metricExporterOpts.url = metricsEndpoint;
        if (setup_.useTls)
        {
            metricExporterOpts.ssl_ca_cert_path = setup_.tlsCertPath;
            metricExporterOpts.ssl_client_cert_path = setup_.tlsClientCertPath;
            metricExporterOpts.ssl_client_key_path = setup_.tlsClientKeyPath;
        }

        auto metricExporter = otlp_http::OtlpHttpMetricExporterFactory::Create(metricExporterOpts);

        // Configure periodic metric reader (1-second export interval,
        // matching the beast OTelCollector path).
        metrics_sdk::PeriodicExportingMetricReaderOptions readerOpts;
        readerOpts.export_interval_millis = std::chrono::milliseconds(1000);
        readerOpts.export_timeout_millis = std::chrono::milliseconds(500);

        auto reader = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
            std::move(metricExporter), readerOpts);

        auto resourceAttrs = makeMetricsResource();

        // Create MeterProvider with the shared resource, then attach reader.
        meterProvider_ = metrics_sdk::MeterProviderFactory::Create(
            std::make_unique<metrics_sdk::ViewRegistry>(), resourceAttrs);
        meterProvider_->AddMetricReader(std::move(reader));

        // Histogram view: SpanMetrics-compatible bucket boundaries (ms) so
        // histogram instruments align with the collector's SpanMetrics. The
        // view is created with an EMPTY name so it applies the buckets WITHOUT
        // renaming instruments — a non-empty view name would collapse every
        // matching histogram (ios_latency, rpc_size, rpc_time, pathfind_*)
        // into a single series under that one name.
        auto histogramSelector = metrics_sdk::InstrumentSelectorFactory::Create(
            metrics_sdk::InstrumentType::kHistogram, "*", "ms");
        // Meter selector MUST match the meter name used by getMeter() and the
        // beast OTelCollector (kMeterName = "xrpld"); otherwise this histogram
        // view never applies and duration histograms fall back to the SDK
        // default boundaries instead of these SpanMetrics-aligned buckets.
        auto meterSelector =
            metrics_sdk::MeterSelectorFactory::Create(std::string(kMeterName), "", "");
        auto histogramConfig = std::make_shared<metrics_sdk::HistogramAggregationConfig>();
        histogramConfig->boundaries_ =
            std::vector<double>{1.0, 5.0, 10.0, 25.0, 50.0, 100.0, 250.0, 500.0, 1000.0, 5000.0};
        auto histogramView = metrics_sdk::ViewFactory::Create(
            "",  // empty name: keep each instrument's own name, only set buckets
            "SpanMetrics-compatible histogram buckets",
            metrics_sdk::AggregationType::kHistogram,
            histogramConfig);

        meterProvider_->AddView(
            std::move(histogramSelector), std::move(meterSelector), std::move(histogramView));

        // Publish as the global meter provider so developers (and the beast
        // OTelCollector shim) reach the same pipeline.
        metrics_api::Provider::SetMeterProvider(
            opentelemetry::nostd::shared_ptr<metrics_api::MeterProvider>(meterProvider_));
    }

    void
    stop() override
    {
        if (stopped_)
            return;
        stopped_ = true;

        JLOG(journal_.info()) << "Telemetry stopping";

        // Unregister global instance before tearing down the pipeline, but only
        // if this object is the one that published it.
        if (Telemetry::getInstance() == this)
            Telemetry::setInstance(nullptr);

        if (sdkProvider_)
        {
            // Force flush with timeout to avoid blocking indefinitely
            // when the OTLP endpoint is unreachable.
            sdkProvider_->ForceFlush(std::chrono::milliseconds(5000));
            // TODO: sdkProvider_ is not thread-safe. This reset() races with
            // getTracer() if any thread is still calling startSpan().
            // Currently safe because Application::stop() shuts down
            // serverHandler_, overlay_, and jobQueue_ before calling
            // telemetry_->stop() — so no callers should remain. If the
            // shutdown order ever changes, add an std::atomic<bool> stopped_
            // flag checked in getTracer() to make this robust.
            sdkProvider_.reset();
            trace_api::Provider::SetTracerProvider(
                opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>(
                    new trace_api::NoopTracerProvider()));
        }

        if (meterProvider_)
        {
            // Mirror the tracer teardown: bounded flush, drop our provider,
            // then restore a noop global provider. Same shutdown-order
            // caveat as sdkProvider_ above applies to getMeter() callers.
            meterProvider_->ForceFlush(std::chrono::milliseconds(5000));
            meterProvider_.reset();
            metrics_api::Provider::SetMeterProvider(
                opentelemetry::nostd::shared_ptr<metrics_api::MeterProvider>(
                    new metrics_api::NoopMeterProvider()));
        }
        JLOG(journal_.info()) << "Telemetry stopped";
    }

    [[nodiscard]] bool
    isEnabled() const override
    {
        return true;
    }

    [[nodiscard]] bool
    shouldTraceTransactions() const override
    {
        return setup_.traceTransactions;
    }

    [[nodiscard]] bool
    shouldTraceConsensus() const override
    {
        return setup_.traceConsensus;
    }

    [[nodiscard]] bool
    shouldTraceRpc() const override
    {
        return setup_.traceRpc;
    }

    [[nodiscard]] bool
    shouldTracePeer() const override
    {
        return setup_.tracePeer;
    }

    [[nodiscard]] bool
    shouldTraceLedger() const override
    {
        return setup_.traceLedger;
    }

    [[nodiscard]] std::string const&
    getConsensusTraceStrategy() const override
    {
        return setup_.consensusTraceStrategy;
    }

    opentelemetry::nostd::shared_ptr<trace_api::Tracer>
    getTracer(std::string_view name = kTracerName) override
    {
        if (!sdkProvider_)
        {
            return trace_api::Provider::GetTracerProvider()->GetTracer(std::string(name));
        }
        return sdkProvider_->GetTracer(std::string(name));
    }

    opentelemetry::nostd::shared_ptr<metrics_api::Meter>
    getMeter(std::string_view name = kMeterName) override
    {
        if (!meterProvider_)
        {
            return metrics_api::Provider::GetMeterProvider()->GetMeter(
                std::string(name), std::string(kMeterVersion));
        }
        return meterProvider_->GetMeter(std::string(name), std::string(kMeterVersion));
    }

    opentelemetry::nostd::shared_ptr<trace_api::Span>
    startSpan(std::string_view name, trace_api::SpanKind kind) override
    {
        auto tracer = getTracer();
        trace_api::StartSpanOptions opts;
        opts.kind = kind;
        return tracer->StartSpan(std::string(name), opts);
    }

    opentelemetry::nostd::shared_ptr<trace_api::Span>
    startSpan(
        std::string_view name,
        opentelemetry::context::Context const& parentContext,
        trace_api::SpanKind kind) override
    {
        auto tracer = getTracer();
        trace_api::StartSpanOptions opts;
        opts.kind = kind;
        opts.parent = parentContext;
        return tracer->StartSpan(std::string(name), opts);
    }
};

}  // namespace

std::unique_ptr<Telemetry>
makeTelemetry(Telemetry::Setup const& setup, beast::Journal journal)
{
    if (setup.enabled)
    {
        return std::make_unique<TelemetryImpl>(setup, journal);
    }
    return std::make_unique<NullTelemetryOtel>(setup);
}

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY
