/**
 * OpenTelemetry SDK implementation of the Telemetry interface.
 *
 * Compiled only when XRPL_ENABLE_TELEMETRY is defined (via CMake
 * telemetry=ON). Contains:
 *
 * - TelemetryImpl: configures the OTel SDK with an OTLP/HTTP exporter,
 * FilteringSpanProcessor wrapping a batch span processor, the head
 * sampler from makeHeadSampler(), and resource attributes.
 * - makeHeadSampler(): builds the head sampler (see HeadSampler.h).
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
#include <xrpl/telemetry/FilteringSpanProcessor.h>
#include <xrpl/telemetry/HeadSampler.h>
#include <xrpl/telemetry/SpanNames.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/string_view.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_options.h>
#include <opentelemetry/sdk/trace/sampler.h>
#include <opentelemetry/sdk/trace/samplers/always_off.h>
#include <opentelemetry/sdk/trace/samplers/always_on.h>
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
 * View a std::string_view as the SDK's string view, without a copy.
 *
 * Without the SDK's STL option, nostd::string_view is a separate type with
 * no constructor from std::string_view.
 *
 * @param text Characters that must outlive the returned view.
 * @return A view of the same characters.
 */
[[nodiscard]] opentelemetry::nostd::string_view
toOtelView(std::string_view text) noexcept
{
    return {text.data(), text.size()};
}

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
        // Clear the global instance only if this object is the one that
        // published it. A process with two of these, as the test binary has,
        // would otherwise let one unregister the other.
        if (Telemetry::getInstance() == this)
        {
            Telemetry::setInstance(nullptr);
        }
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

    [[nodiscard]] opentelemetry::nostd::shared_ptr<trace_api::Tracer>
    getTracer(std::string_view) override
    {
        static auto noopTracer =
            opentelemetry::nostd::shared_ptr<trace_api::Tracer>(new trace_api::NoopTracer());
        return noopTracer;
    }

    [[nodiscard]] opentelemetry::nostd::shared_ptr<trace_api::Span>
    startSpan(std::string_view, trace_api::SpanKind) override
    {
        return opentelemetry::nostd::shared_ptr<trace_api::Span>(new trace_api::NoopSpan(nullptr));
    }

    [[nodiscard]] opentelemetry::nostd::shared_ptr<trace_api::Span>
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

    /**
     * Tracer for kTracerName, cached so a span does not take the provider's
     * lock.
     *
     * Starts as the global provider's tracer. start() sets it to the SDK
     * tracer before setInstance() publishes this object, and stop() never
     * writes it, so readers need no lock.
     */
    opentelemetry::nostd::shared_ptr<trace_api::Tracer> tracer_ =
        trace_api::Provider::GetTracerProvider()->GetTracer(toOtelView(kTracerName));

    /**
     * Coroutine-aware runtime-context storage, installed globally so the OTel
     * ambient context follows JobQueue coroutines. Held for the process
     * lifetime because it must outlive every span (SDK requirement).
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::context::RuntimeContextStorage> contextStorage_;

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
        JLOG(journal_.info()) << "Telemetry starting: traces_endpoint=" << setup_.tracesEndpoint
                              << " sampling=" << setup_.samplingRatio;

        // Configure OTLP HTTP exporter
        otlp_http::OtlpHttpExporterOptions exporterOpts;
        exporterOpts.url = setup_.tracesEndpoint;
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

        // Wrap the batch processor. FilteringSpanProcessor drops discarded
        // spans and exports each attribute key once, with its last value.
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

        // Head sampling is fixed at 1.0. Spans with no parent or a remote parent use the
        // trace-id ratio sampler, so a peer's sampled flag cannot turn our spans off or on.
        // Spans with a local parent follow it. The ratio sampler reads only the trace id,
        // so nodes agree on every trace. Collector tail sampling reduces volume.
        auto sampler = makeHeadSampler(setup_.samplingRatio);

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

        // GetTracer() locks the provider, so fetch the tracer once here rather
        // than per span. It must be set before setInstance() publishes it.
        tracer_ = sdkProvider_->GetTracer(toOtelView(kTracerName));

        // Register as the global Telemetry instance so SpanGuard factory
        // methods can access it without callers passing a reference.
        Telemetry::setInstance(this);

        JLOG(journal_.info()) << "Telemetry started successfully";
    }

    void
    stop() override
    {
        JLOG(journal_.info()) << "Telemetry stopping";

        // Unregister global instance before tearing down the pipeline, but only
        // if this object is the one that published it.
        if (Telemetry::getInstance() == this)
        {
            Telemetry::setInstance(nullptr);
        }

        if (sdkProvider_)
        {
            // Force flush with timeout to avoid blocking indefinitely
            // when the OTLP endpoint is unreachable.
            sdkProvider_->ForceFlush(std::chrono::milliseconds(5000));
            // startSpan() and getTracer() never read sdkProvider_, so this
            // reset() cannot race with them. A span started after stop()
            // still goes through tracer_, into the shut-down pipeline, and is
            // dropped rather than exported.
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

    [[nodiscard]] opentelemetry::nostd::shared_ptr<trace_api::Tracer>
    getTracer(std::string_view name = kTracerName) override
    {
        if (name == kTracerName)
            return tracer_;
        return trace_api::Provider::GetTracerProvider()->GetTracer(toOtelView(name));
    }

    [[nodiscard]] opentelemetry::nostd::shared_ptr<trace_api::Span>
    startSpan(std::string_view name, trace_api::SpanKind kind) override
    {
        trace_api::StartSpanOptions opts;
        opts.kind = kind;
        return tracer_->StartSpan(toOtelView(name), opts);
    }

    [[nodiscard]] opentelemetry::nostd::shared_ptr<trace_api::Span>
    startSpan(
        std::string_view name,
        opentelemetry::context::Context const& parentContext,
        trace_api::SpanKind kind) override
    {
        trace_api::StartSpanOptions opts;
        opts.kind = kind;
        opts.parent = parentContext;
        return tracer_->StartSpan(toOtelView(name), opts);
    }
};

}  // namespace

std::unique_ptr<trace_sdk::Sampler>
makeHeadSampler(double ratio)
{
    // One ratio sampler serves the root case and both remote-parent cases.
    std::shared_ptr<trace_sdk::Sampler> const ratioSampler =
        std::make_shared<trace_sdk::TraceIdRatioBasedSampler>(ratio);
    return trace_sdk::ParentBasedSamplerFactory::Create(
        ratioSampler,
        ratioSampler,
        ratioSampler,
        std::make_shared<trace_sdk::AlwaysOnSampler>(),
        std::make_shared<trace_sdk::AlwaysOffSampler>());
}

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
