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
#include <xrpl/telemetry/DiscardFlag.h>
#include <xrpl/telemetry/SpanNames.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/nostd/shared_ptr.h>
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
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace xrpl::telemetry {

namespace {

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
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
     * Non-const so setServiceInstanceId() can update the instance ID
     * before start() creates the OTel resource.
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

public:
    TelemetryImpl(Setup setup, beast::Journal journal) : setup_(std::move(setup)), journal_(journal)
    {
    }

    void
    setServiceInstanceId(std::string const& id) override
    {
        setup_.serviceInstanceId = id;
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
        auto resourceAttrs = resource::Resource::Create({
            {opentelemetry::semconv::service::kServiceName, setup_.serviceName},
            {opentelemetry::semconv::service::kServiceVersion, setup_.serviceVersion},
            {opentelemetry::semconv::service::kServiceInstanceId, setup_.serviceInstanceId},
            {std::string(attr::networkId),
             static_cast<int64_t>(setup_.networkId)},              // LCOV_EXCL_LINE
            {std::string(attr::networkType), setup_.networkType},  // LCOV_EXCL_LINE
        });

        // Configure sampler. Head sampling is fixed at 1.0 (sample everything);
        // setup_.samplingRatio is not config-driven. Wrap the ratio sampler in a
        // ParentBasedSampler so spans with a remote parent honor the upstream
        // sampled flag — this keeps keep/drop decisions coherent for a single
        // distributed trace spanning multiple nodes. Volume reduction is left to
        // the collector's tail sampling.
        auto rootSampler =
            std::make_shared<trace_sdk::TraceIdRatioBasedSampler>(setup_.samplingRatio);
        auto sampler = trace_sdk::ParentBasedSamplerFactory::Create(std::move(rootSampler));

        // Create TracerProvider
        sdkProvider_ = trace_sdk::TracerProviderFactory::Create(
            std::move(processor), resourceAttrs, std::move(sampler));

        // Set as global provider
        trace_api::Provider::SetTracerProvider(
            opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>(sdkProvider_));

        // Register as the global Telemetry instance so SpanGuard factory
        // methods can access it without callers passing a reference.
        Telemetry::setInstance(this);

        JLOG(journal_.info()) << "Telemetry started successfully";
    }

    void
    stop() override
    {
        JLOG(journal_.info()) << "Telemetry stopping";

        // Unregister global instance before tearing down the pipeline.
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
