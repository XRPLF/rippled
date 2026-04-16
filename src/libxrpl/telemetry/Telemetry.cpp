/** OpenTelemetry SDK implementation of the Telemetry interface.

    Compiled only when XRPL_ENABLE_TELEMETRY is defined (via CMake
    telemetry=ON). Contains:

      - TelemetryImpl: configures the OTel SDK with an OTLP/HTTP exporter,
        batch span processor, trace-ID-ratio sampler, and resource attributes.
      - NullTelemetryOtel: no-op fallback used when telemetry is compiled in
        but disabled at runtime (enabled=0 in config).
      - make_Telemetry(): factory that selects the appropriate implementation.
*/

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/basics/Log.h>
#include <xrpl/telemetry/Telemetry.h>

#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/sdk/resource/semantic_conventions.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_options.h>
#include <opentelemetry/sdk/trace/sampler.h>
#include <opentelemetry/sdk/trace/samplers/trace_id_ratio.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/noop.h>
#include <opentelemetry/trace/provider.h>

namespace xrpl {
namespace telemetry {

namespace {

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace otlp_http = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;

/** No-op implementation used when XRPL_ENABLE_TELEMETRY is defined but
    setup.enabled is false at runtime.

    Lives in the anonymous namespace so there is no ODR conflict with the
    NullTelemetry in NullTelemetry.cpp.
*/
class NullTelemetryOtel : public Telemetry
{
    /** Retained configuration (unused, kept for diagnostic access). */
    Setup const setup_;

public:
    explicit NullTelemetryOtel(Setup const& setup) : setup_(setup)
    {
    }

    void
    start() override
    {
    }

    void
    stop() override
    {
    }

    bool
    isEnabled() const override
    {
        return false;
    }

    bool
    shouldTraceTransactions() const override
    {
        return false;
    }

    bool
    shouldTraceConsensus() const override
    {
        return false;
    }

    bool
    shouldTraceRpc() const override
    {
        return false;
    }

    bool
    shouldTracePeer() const override
    {
        return false;
    }

    bool
    shouldTraceLedger() const override
    {
        return false;
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

/** Full OTel SDK implementation that exports trace spans via OTLP/HTTP.

    Configures an OTLP/HTTP exporter, batch span processor,
    TraceIdRatioBasedSampler, and resource attributes on start().
*/
class TelemetryImpl : public Telemetry
{
    /** Configuration from the [telemetry] config section.
        Non-const so setServiceInstanceId() can update the instance ID
        before start() creates the OTel resource.
    */
    Setup setup_;

    /** Journal used for log output during start/stop. */
    beast::Journal const journal_;

    /** The SDK TracerProvider that owns the export pipeline.

        Held as std::shared_ptr so we can call ForceFlush() on shutdown.
        Wrapped in a nostd::shared_ptr when registered as the global provider.
    */
    std::shared_ptr<trace_sdk::TracerProvider> sdkProvider_;

public:
    TelemetryImpl(Setup const& setup, beast::Journal journal) : setup_(setup), journal_(journal)
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
            exporterOpts.ssl_ca_cert_path = setup_.tlsCertPath;

        auto exporter = otlp_http::OtlpHttpExporterFactory::Create(exporterOpts);

        // Configure batch processor
        trace_sdk::BatchSpanProcessorOptions processorOpts;
        processorOpts.max_queue_size = setup_.maxQueueSize;
        processorOpts.schedule_delay_millis = std::chrono::milliseconds(setup_.batchDelay);
        processorOpts.max_export_batch_size = setup_.batchSize;

        auto processor =
            trace_sdk::BatchSpanProcessorFactory::Create(std::move(exporter), processorOpts);

        // Configure resource attributes
        auto resourceAttrs = resource::Resource::Create({
            {resource::SemanticConventions::kServiceName, setup_.serviceName},
            {resource::SemanticConventions::kServiceVersion, setup_.serviceVersion},
            {resource::SemanticConventions::kServiceInstanceId, setup_.serviceInstanceId},
            {"xrpl.network.id", static_cast<int64_t>(setup_.networkId)},
            {"xrpl.network.type", setup_.networkType},
        });

        // Configure sampler
        auto sampler = std::make_unique<trace_sdk::TraceIdRatioBasedSampler>(setup_.samplingRatio);

        // Create TracerProvider
        sdkProvider_ = trace_sdk::TracerProviderFactory::Create(
            std::move(processor), resourceAttrs, std::move(sampler));

        // Set as global provider
        trace_api::Provider::SetTracerProvider(
            opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>(sdkProvider_));

        JLOG(journal_.info()) << "Telemetry started successfully";
    }

    void
    stop() override
    {
        JLOG(journal_.info()) << "Telemetry stopping";
        if (sdkProvider_)
        {
            // Force flush before shutdown
            sdkProvider_->ForceFlush();
            sdkProvider_.reset();
            trace_api::Provider::SetTracerProvider(
                opentelemetry::nostd::shared_ptr<trace_api::TracerProvider>(
                    new trace_api::NoopTracerProvider()));
        }
        JLOG(journal_.info()) << "Telemetry stopped";
    }

    bool
    isEnabled() const override
    {
        return true;
    }

    bool
    shouldTraceTransactions() const override
    {
        return setup_.traceTransactions;
    }

    bool
    shouldTraceConsensus() const override
    {
        return setup_.traceConsensus;
    }

    bool
    shouldTraceRpc() const override
    {
        return setup_.traceRpc;
    }

    bool
    shouldTracePeer() const override
    {
        return setup_.tracePeer;
    }

    bool
    shouldTraceLedger() const override
    {
        return setup_.traceLedger;
    }

    opentelemetry::nostd::shared_ptr<trace_api::Tracer>
    getTracer(std::string_view name) override
    {
        if (!sdkProvider_)
            return trace_api::Provider::GetTracerProvider()->GetTracer(std::string(name));
        return sdkProvider_->GetTracer(std::string(name));
    }

    opentelemetry::nostd::shared_ptr<trace_api::Span>
    startSpan(std::string_view name, trace_api::SpanKind kind) override
    {
        auto tracer = getTracer("xrpld");
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
        auto tracer = getTracer("xrpld");
        trace_api::StartSpanOptions opts;
        opts.kind = kind;
        opts.parent = parentContext;
        return tracer->StartSpan(std::string(name), opts);
    }
};

}  // namespace

std::unique_ptr<Telemetry>
make_Telemetry(Telemetry::Setup const& setup, beast::Journal journal)
{
    if (setup.enabled)
        return std::make_unique<TelemetryImpl>(setup, journal);
    return std::make_unique<NullTelemetryOtel>(setup);
}

}  // namespace telemetry
}  // namespace xrpl

#endif  // XRPL_ENABLE_TELEMETRY
