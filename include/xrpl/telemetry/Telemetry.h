#pragma once

/** Abstract interface for OpenTelemetry distributed tracing.

    Provides the Telemetry base class that all components use to create trace
    spans. Two concrete implementations exist, selected at construction time
    by make_Telemetry():

      - TelemetryImpl (Telemetry.cpp): real OTel SDK integration, compiled
        only when XRPL_ENABLE_TELEMETRY is defined and enabled at runtime.
      - NullTelemetry (NullTelemetry.cpp): no-op stub used when telemetry is
        disabled at compile time or runtime.

    Inheritance / dependency diagram:

        +--------------------+
        |    Telemetry       |  (abstract, this file)
        |  <<interface>>     |
        +---------+----------+
                  |
        +---------+-----------+-------------------+
        |                     |                   |
    +---+------------+  +-----+---------+  +------+----------+
    | TelemetryImpl  |  | NullTelemetry |  | NullTelemetryOtel|
    | (Telemetry.cpp)|  |(NullTelemetry |  | (Telemetry.cpp)  |
    | OTel SDK       |  | .cpp)         |  | noop w/ OTel API |
    +----------------+  +---------------+  +------------------+

    The Setup struct holds all configuration parsed from the [telemetry]
    section of xrpld.cfg. See TelemetryConfig.cpp for the parser and
    cfg/xrpld-example.cfg for the available options.

    OTel SDK headers are conditionally included behind XRPL_ENABLE_TELEMETRY
    so that builds without telemetry have zero dependency on opentelemetry-cpp.

    Usage examples:

    1. Check before tracing (typical guard pattern):
    @code
        auto& telemetry = registry.getTelemetry();
        if (telemetry.isEnabled() && telemetry.shouldTraceRpc())
        {
            auto span = telemetry.startSpan("rpc.command.server_info");
            // ... do work, span ends when shared_ptr refcount drops to 0
        }
    @endcode

    2. RAII tracing with SpanGuard (preferred):
    @code
        if (telemetry.isEnabled() && telemetry.shouldTraceRpc())
        {
            SpanGuard guard(telemetry.startSpan("rpc.command.submit"));
            guard.setAttribute("xrpl.rpc.command", "submit");
            // ... guard ends span automatically on scope exit
        }
    @endcode

    3. Cross-thread context propagation:
    @code
        // On thread A: capture context
        auto ctx = guard.context();
        // On thread B: create child span with explicit parent
        auto child = telemetry.startSpan("async.work", ctx);
    @endcode

    @note Thread safety: The Telemetry interface is safe for concurrent reads
    (isEnabled, shouldTrace*, getTracer, startSpan) after start() completes.
    setServiceInstanceId() must be called before start() and is not thread-safe.
    The OTel SDK's TracerProvider and Tracer are internally thread-safe.
*/

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/utility/Journal.h>

#include <atomic>
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

namespace xrpl::telemetry {

class Telemetry
{
    /** Global singleton pointer, set by start()/stop() in the active
        implementation. Allows SpanGuard factory methods to access the
        Telemetry instance without callers passing it explicitly.

        Atomic with acquire/release ordering: start()/stop() store on
        the initialization thread, factory methods load on worker threads.
        @see setInstance(), getInstance()
    */
    inline static std::atomic<Telemetry*> instance_{nullptr};

public:
    /** Get the global Telemetry instance.
        @return Pointer to the active instance, or nullptr if not started.
    */
    static Telemetry*
    getInstance()
    {
        return instance_.load(std::memory_order_acquire);
    }

    /** Set the global Telemetry instance.
        Called by start()/stop() in concrete implementations.
        Tests can call this with a mock to override the global instance.
        @param t  Pointer to the Telemetry instance, or nullptr to clear.
    */
    static void
    setInstance(Telemetry* t)
    {
        instance_.store(t, std::memory_order_release);
    }

    /** Configuration parsed from the [telemetry] section of xrpld.cfg.

        All fields have sensible defaults so the section can be minimal
        or omitted entirely. See TelemetryConfig.cpp for the parser.
    */
    struct Setup
    {
        /** Master switch: true to enable tracing at runtime. */
        bool enabled = false;

        /** OTel resource attribute `service.name`. */
        std::string serviceName = "xrpld";

        /** OTel resource attribute `service.version` (set from BuildInfo). */
        std::string serviceVersion;

        /** OTel resource attribute `service.instance.id` (defaults to node
            public key). */
        std::string serviceInstanceId;

        /** OTLP/HTTP endpoint URL where spans are sent. */
        std::string exporterEndpoint = "http://localhost:4318/v1/traces";

        /** Whether to use TLS for the exporter connection. */
        bool useTls = false;

        /** Path to a CA certificate bundle for TLS verification. */
        std::string tlsCertPath;

        /** Head-based sampling ratio in [0.0, 1.0]. 1.0 = trace everything.
            This is a head-based (pre-decision) sampler using
            TraceIdRatioBasedSampler — the decision to record or drop a
            trace is made before the root span starts. For post-hoc
            (tail-based) filtering, see SpanGuard::discard().
        */
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

        /** Strategy for cross-node consensus trace correlation.
            "deterministic" — derive trace_id from ledger hash so all
            validators in the same round share the same trace_id.
            "attribute" — random trace_id, correlate via ledger_id attribute.
        */
        std::string consensusTraceStrategy = "deterministic";
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
    [[nodiscard]] virtual bool
    isEnabled() const = 0;

    /** @return true if transaction processing should be traced. */
    [[nodiscard]] virtual bool
    shouldTraceTransactions() const = 0;

    /** @return true if consensus rounds should be traced. */
    [[nodiscard]] virtual bool
    shouldTraceConsensus() const = 0;

    /** @return true if RPC request handling should be traced. */
    [[nodiscard]] virtual bool
    shouldTraceRpc() const = 0;

    /** @return true if peer-to-peer messages should be traced. */
    [[nodiscard]] virtual bool
    shouldTracePeer() const = 0;

    /** @return true if ledger close/accept should be traced. */
    [[nodiscard]] virtual bool
    shouldTraceLedger() const = 0;

    /** @return The configured consensus trace correlation strategy. */
    [[nodiscard]] virtual std::string const&
    getConsensusTraceStrategy() const = 0;

#ifdef XRPL_ENABLE_TELEMETRY
    /** Get or create a named tracer instance.

        @param name  Tracer name used to identify the instrumentation library.
        @return A shared pointer to the Tracer.
    */
    virtual opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>
    getTracer(std::string_view name = "xrpld") = 0;

    /** Start a new span on the current thread's context.

        The span becomes a child of the current active span (if any) via
        OpenTelemetry's context propagation.

        @param name  Span name (typically "rpc.command.<cmd>").
        @param kind  The span kind (defaults to kInternal). Possible values:
                     - kInternal: default, in-process operation
                     - kServer:   incoming synchronous request (e.g. RPC)
                     - kClient:   outgoing synchronous request
                     - kProducer: async message send (e.g. peer broadcast)
                     - kConsumer: async message receive
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
    @param networkId      Network identifier from [network_id] config
                          (0 = mainnet, 1 = testnet, 2 = devnet).
    @return A populated Setup struct with defaults for missing values.
*/
Telemetry::Setup
setup_Telemetry(
    Section const& section,
    std::string const& nodePublicKey,
    std::string const& version,
    std::uint32_t networkId);

}  // namespace xrpl::telemetry
