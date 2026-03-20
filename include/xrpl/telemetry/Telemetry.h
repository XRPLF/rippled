#pragma once

/** Abstract interface for OpenTelemetry distributed tracing.

    Provides the Telemetry base class that all components use to create trace
    spans. Two implementations exist:

      - TelemetryImpl (Telemetry.cpp): real OTel SDK integration, compiled
        only when XRPL_ENABLE_TELEMETRY is defined and enabled at runtime.
      - NullTelemetry (NullTelemetry.cpp): no-op stub used when telemetry is
        disabled at compile time or runtime.

    The Setup struct holds all configuration parsed from the [telemetry]
    section of xrpld.cfg. See TelemetryConfig.cpp for the parser and
    cfg/xrpld-example.cfg for the available options.

    OTel SDK headers are conditionally included behind XRPL_ENABLE_TELEMETRY
    so that builds without telemetry have zero dependency on opentelemetry-cpp.
*/

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/utility/Journal.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#ifdef XRPL_ENABLE_TELEMETRY
#include <opentelemetry/context/context.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/tracer.h>
#endif

namespace xrpl {
namespace telemetry {

class Telemetry
{
public:
    /** Configuration parsed from the [telemetry] section of xrpld.cfg.

        All fields have sensible defaults so the section can be minimal
        or omitted entirely. See TelemetryConfig.cpp for the parser.
    */
    struct Setup
    {
        /** Master switch: true to enable tracing at runtime. */
        bool enabled = false;

        /** OTel resource attribute `service.name`. */
        std::string serviceName = "rippled";

        /** OTel resource attribute `service.version` (set from BuildInfo). */
        std::string serviceVersion;

        /** OTel resource attribute `service.instance.id` (defaults to node
            public key). */
        std::string serviceInstanceId;

        /** Exporter type: currently only "otlp_http" is supported. */
        std::string exporterType = "otlp_http";

        /** OTLP/HTTP endpoint URL where spans are sent. */
        std::string exporterEndpoint = "http://localhost:4318/v1/traces";

        /** Whether to use TLS for the exporter connection. */
        bool useTls = false;

        /** Path to a CA certificate bundle for TLS verification. */
        std::string tlsCertPath;

        /** Head-based sampling ratio in [0.0, 1.0]. 1.0 = trace everything. */
        double samplingRatio = 1.0;

        /** Maximum number of spans per batch export. */
        std::uint32_t batchSize = 512;

        /** Delay between batch exports. */
        std::chrono::milliseconds batchDelay{5000};

        /** Maximum number of spans queued before dropping. */
        std::uint32_t maxQueueSize = 2048;

        /** Network identifier, added as an OTel resource attribute. */
        std::uint32_t networkId = 0;

        /** Network type label (e.g. "mainnet", "testnet", "devnet"). */
        std::string networkType = "mainnet";

        /** Enable tracing for transaction processing. */
        bool traceTransactions = true;

        /** Enable tracing for consensus rounds. */
        bool traceConsensus = true;

        /** Enable tracing for RPC request handling. */
        bool traceRpc = true;

        /** Enable tracing for peer-to-peer messages (disabled by default
            due to high volume). */
        bool tracePeer = false;

        /** Enable tracing for ledger close/accept. */
        bool traceLedger = true;
    };

    virtual ~Telemetry() = default;

    /** Update the service instance ID (OTel resource attribute
        `service.instance.id`).

        Must be called before start(). The node public key is not available
        when Telemetry is constructed (during the ApplicationImp member
        initializer list), so this setter allows Application::setup() to
        inject the identity once nodeIdentity_ is known.

        @param id  The node's base58-encoded public key or custom identifier.
    */
    virtual void
    setServiceInstanceId(std::string const& id)
    {
        // Default no-op for NullTelemetry implementations.
        (void)id;
    }

    /** Initialize the tracing pipeline (exporter, processor, provider).
        Call after construction.
    */
    virtual void
    start() = 0;

    /** Flush pending spans and shut down the tracing pipeline.
        Call before destruction.
    */
    virtual void
    stop() = 0;

    /** @return true if this instance is actively exporting spans. */
    virtual bool
    isEnabled() const = 0;

    /** @return true if transaction processing should be traced. */
    virtual bool
    shouldTraceTransactions() const = 0;

    /** @return true if consensus rounds should be traced. */
    virtual bool
    shouldTraceConsensus() const = 0;

    /** @return true if RPC request handling should be traced. */
    virtual bool
    shouldTraceRpc() const = 0;

    /** @return true if peer-to-peer messages should be traced. */
    virtual bool
    shouldTracePeer() const = 0;

#ifdef XRPL_ENABLE_TELEMETRY
    /** Get or create a named tracer instance.

        @param name  Tracer name used to identify the instrumentation library.
        @return A shared pointer to the Tracer.
    */
    virtual opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>
    getTracer(std::string_view name = "rippled") = 0;

    /** Start a new span on the current thread's context.

        The span becomes a child of the current active span (if any) via
        OpenTelemetry's context propagation.

        @param name  Span name (typically "rpc.command.<cmd>").
        @param kind  The span kind (defaults to kInternal).
        @return A shared pointer to the new Span.
    */
    virtual opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    startSpan(
        std::string_view name,
        opentelemetry::trace::SpanKind kind = opentelemetry::trace::SpanKind::kInternal) = 0;

    /** Start a new span with an explicit parent context.

        Use this overload when the parent span is not on the current
        thread's context stack (e.g. cross-thread trace propagation).

        @param name           Span name.
        @param parentContext  The parent span's context.
        @param kind           The span kind (defaults to kInternal).
        @return A shared pointer to the new Span.
    */
    virtual opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    startSpan(
        std::string_view name,
        opentelemetry::context::Context const& parentContext,
        opentelemetry::trace::SpanKind kind = opentelemetry::trace::SpanKind::kInternal) = 0;
#endif
};

/** Create a Telemetry instance.

    Returns a TelemetryImpl when setup.enabled is true, or a
    NullTelemetry no-op stub otherwise.

    @param setup    Configuration from the [telemetry] config section.
    @param journal  Journal for log output during initialization.
*/
std::unique_ptr<Telemetry>
make_Telemetry(Telemetry::Setup const& setup, beast::Journal journal);

/** Parse the [telemetry] config section into a Setup struct.

    @param section        The [telemetry] config section.
    @param nodePublicKey  Node public key, used as default instance ID.
    @param version        Build version string.
    @return A populated Setup struct with defaults for missing values.
*/
Telemetry::Setup
setup_Telemetry(
    Section const& section,
    std::string const& nodePublicKey,
    std::string const& version);

}  // namespace telemetry
}  // namespace xrpl
