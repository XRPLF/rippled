#pragma once

/**
 * Abstract interface for OpenTelemetry distributed tracing.
 *
 * Provides the Telemetry base class that all components use to create trace
 * spans. Three concrete implementations exist, selected at construction time
 * by makeTelemetry():
 *
 * - TelemetryImpl (Telemetry.cpp): real OTel SDK integration, compiled
 * only when XRPL_ENABLE_TELEMETRY is defined and enabled at runtime.
 * - NullTelemetry (NullTelemetry.cpp): no-op stub used when telemetry is
 * disabled at compile time or runtime.
 * - NullTelemetryOtel (Telemetry.cpp): no-op stub that still depends on
 * the OTel API (used during transition or for testing).
 *
 * Inheritance / dependency diagram:
 *
 * +--------------------+
 * |    Telemetry       |  (abstract, this file)
 * |  <<interface>>     |
 * +---------+----------+
 * |
 * +---------+-----------+-------------------+
 * |                     |                   |
 * +---+------------+  +-----+---------+  +------+----------+
 * | TelemetryImpl  |  | NullTelemetry |  | NullTelemetryOtel|
 * | (Telemetry.cpp)|  |(NullTelemetry |  | (Telemetry.cpp)  |
 * | OTel SDK       |  | .cpp)         |  | noop w/ OTel API |
 * +----------------+  +---------------+  +------------------+
 *
 * The Setup struct holds all configuration parsed from the [telemetry]
 * section of xrpld.cfg. See TelemetryConfig.cpp for the parser and
 * cfg/xrpld-example.cfg for the available options.
 *
 * OTel SDK headers are conditionally included behind XRPL_ENABLE_TELEMETRY
 * so that builds without telemetry have zero dependency on opentelemetry-cpp.
 *
 * Usage examples:
 *
 * 1. Root span at a subsystem entry point (typical usage):
 * @code
 * #include <xrpld/rpc/detail/RpcSpanNames.h>
 * using namespace xrpl::telemetry;
 *
 * // In an RPC handler dispatch:
 * auto guard = SpanGuard::span(
 * TraceCategory::Rpc, rpc_span::prefix::command, commandName);
 * guard.setAttribute(rpc_span::attr::command, commandName);
 * // ... process request
 * // guard destructor automatically ends the span on scope exit
 * @endcode
 *
 * 2. Child span for a sub-operation (scoped child):
 * @code
 * auto parent = ScopedSpanGuard(
 * TraceCategory::Rpc, rpc_span::prefix::rpc, rpc_span::op::process);
 * {
 * auto child = parent.childSpan(rpc_span::prefix::command);
 * child.setAttribute(rpc_span::attr::version, static_cast<int64_t>(apiVersion));
 * // child ends here
 * }
 * @endcode
 * childSpan() parents to the ambient scope, so the parent must be a
 * ScopedSpanGuard. A plain SpanGuard is not ambient: pass its spanContext()
 * to childSpan(name, ctx) instead. childSpan() takes the name verbatim, so
 * pass a full dotted constant, never a bare op:: suffix.
 *
 * 3. Unrelated span (cross-scope, same thread):
 * @code
 * // gRPC and RPC handlers can be active simultaneously
 * auto grpcSpan = SpanGuard::span(
 * TraceCategory::Rpc, grpc_span::prefix::grpc, grpc_span::attr::method);
 * auto rpcSpan = SpanGuard::span(
 * TraceCategory::Rpc, rpc_span::prefix::command, commandName);
 * // both spans end on scope exit
 * @endcode
 *
 * 4. Cross-thread context propagation:
 * @code
 * // Thread A: take a handle to the parent span's own context
 * auto ctx = parentGuard.spanContext();
 *
 * // Thread B: create child span with explicit parent
 * auto child = SpanGuard::childSpan(rpc_span::prefix::command, ctx);
 * @endcode
 *
 * @note Thread safety: The Telemetry interface is safe for concurrent reads
 * (isEnabled, shouldTrace*, getTracer, startSpan) after start() completes.
 * setServiceInstanceId() must be called before start() and is not thread-safe.
 * The OTel SDK's TracerProvider and Tracer are internally thread-safe.
 */

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#ifdef XRPL_ENABLE_TELEMETRY
#include <opentelemetry/context/context.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_metadata.h>
#include <opentelemetry/trace/tracer.h>

// std::string_view appears only in the telemetry-enabled declarations below.
#include <string_view>
#endif

namespace xrpl::telemetry {

#ifdef XRPL_ENABLE_TELEMETRY
/**
 * OTel instrumentation scope (tracer) name. Identifies this library as the
 * source of spans; distinct from the `service.name` resource attribute
 * (Setup::serviceName), which is config-overridable.
 */
inline constexpr std::string_view kTracerName{"xrpld"};
#endif

/**
 * How a consensus round span picks its trace id.
 *
 *   consensus_trace_strategy (xrpld.cfg)
 *          |
 *          v
 *   makeTelemetrySetup()  ──>  Setup::consensusTraceStrategy
 *          |
 *          v
 *   RCLConsensus::Adaptor::startRoundTracing()
 *          |
 *          +-- Deterministic ──> SpanGuard::hashSpan(prev ledger hash)
 *          +-- Random        ──> SpanGuard::span() / linkedSpan()
 *
 * Deterministic is the strategy in use. Every validator of a round hashes the
 * same previous ledger id, so all of them land in one trace.
 *
 * Random is experimental and not used. Each node would invent its own trace
 * id, so one round would arrive as one trace per node, joinable only by the
 * `consensus_ledger_id` attribute.
 *
 * @code
 * // Branch on the strategy rather than on a string.
 * if (telemetry.getConsensusTraceStrategy() == ConsensusTraceStrategy::Random)
 *     span = SpanGuard::span(TraceCategory::Consensus, seg::consensus, op::round);
 * else
 *     span = SpanGuard::hashSpan(TraceCategory::Consensus, name, id.data(), id.kBytes);
 *
 * // Edge case: the value also goes on a span attribute, so it needs its
 * // config spelling back.
 * span.setAttribute(attr::traceStrategy, strategyName(ConsensusTraceStrategy::Random));
 * @endcode
 *
 * @note Adding an enumerator means adding a spelling to strategyName() below
 * and to the parser in TelemetryConfig.cpp. Both switch without a default, so
 * the compiler catches a missed one.
 */
enum class ConsensusTraceStrategy : std::uint8_t { Deterministic, Random };

/**
 * Config spelling of a consensus trace strategy.
 *
 * This is the same text `consensus_trace_strategy` accepts, and it is what
 * goes on the `trace_strategy` span attribute, so the two cannot drift.
 *
 * @param strategy  Strategy to name.
 * @return "deterministic" or "random", pointing at a string literal.
 */
[[nodiscard]] constexpr char const*
strategyName(ConsensusTraceStrategy strategy)
{
    switch (strategy)
    {
        case ConsensusTraceStrategy::Deterministic:
            return "deterministic";
        case ConsensusTraceStrategy::Random:
            return "random";
    }
    // The switch covers every enumerator. This return only satisfies the
    // compiler, which cannot rule out a value outside the enumeration.
    return "deterministic";
}

class Telemetry
{
    /**
     * Global singleton pointer, set by start()/stop() in the active
     * implementation. Allows SpanGuard factory methods to access the
     * Telemetry instance without callers passing it explicitly.
     *
     * Atomic with acquire/release ordering: start()/stop() store on
     * the initialization thread, factory methods load on worker threads.
     * @see setInstance(), getInstance()
     */
    inline static std::atomic<Telemetry*> instance{nullptr};

public:
    /**
     * Get the global Telemetry instance.
     * @return Pointer to the active instance, or nullptr if not started.
     */
    [[nodiscard]] static Telemetry*
    getInstance()
    {
        return instance.load(std::memory_order_acquire);
    }

    /**
     * Set the global Telemetry instance.
     * Called by start()/stop() in concrete implementations.
     * Tests can call this with a mock to override the global instance.
     * @param t  Pointer to the Telemetry instance, or nullptr to clear.
     */
    static void
    setInstance(Telemetry* t)
    {
        instance.store(t, std::memory_order_release);
    }

    /**
     * Configuration parsed from the [telemetry] section of xrpld.cfg.
     *
     * All fields have sensible defaults so the section can be minimal
     * or omitted entirely. See TelemetryConfig.cpp for the parser.
     */
    struct Setup
    {
        /**
         * Master switch: true to enable tracing at runtime.
         */
        bool enabled = false;

        /**
         * OTel resource attribute `service.name`.
         */
        std::string serviceName = "xrpld";

        /**
         * OTel resource attribute `service.version` (set from BuildInfo).
         */
        std::string serviceVersion;

        /**
         * OTel resource attribute `service.instance.id` (defaults to node
         * public key).
         */
        std::string serviceInstanceId;

        /**
         * Full OTLP/HTTP URL where spans are sent, including the signal path.
         * Used verbatim: no other endpoint is derived from it.
         */
        std::string tracesEndpoint = "http://localhost:4318/v1/traces";

        /**
         * Whether to use TLS for the exporter connection.
         */
        bool useTls = false;

        /**
         * Path to a CA certificate bundle for TLS verification.
         */
        std::string tlsCertPath;

        /**
         * Path to this node's client certificate (PEM), presented to the
         * collector for mutual TLS. Empty disables client-side auth, in
         * which case only server (one-way) TLS is used.
         */
        std::string tlsClientCertPath;

        /**
         * Path to the private key (PEM) for tlsClientCertPath. Required
         * whenever tlsClientCertPath is set.
         */
        std::string tlsClientKeyPath;

        /**
         * Head-based sampling ratio. Intentionally fixed at 1.0 (sample
         * everything) and NOT read from config. A per-node ratio would let
         * nodes make divergent keep/drop decisions for the same distributed
         * trace, producing broken/partial traces. The ratio sampler is wrapped
         * in a ParentBasedSampler (see Telemetry.cpp) so spans inheriting a
         * remote parent honor the upstream sampled flag. Volume reduction is
         * delegated to the collector's tail sampling; for node-local post-hoc
         * dropping see SpanGuard::discard().
         */
        static constexpr double samplingRatio = 1.0;

        /**
         * Maximum number of spans per batch export.
         */
        std::uint32_t batchSize = 512;

        /**
         * Delay between batch exports.
         */
        std::chrono::milliseconds batchDelay = std::chrono::milliseconds{5000};

        /**
         * Maximum number of spans queued before dropping.
         */
        std::uint32_t maxQueueSize = 2048;

        /**
         * Network identifier, added as an OTel resource attribute.
         */
        std::uint32_t networkId = 0;

        /**
         * Network type label (e.g. "mainnet", "testnet", "devnet").
         */
        std::string networkType = "mainnet";

        /**
         * Enable tracing for transaction processing.
         */
        bool traceTransactions = true;

        /**
         * Enable tracing for consensus rounds.
         */
        bool traceConsensus = true;

        /**
         * Enable tracing for RPC request handling.
         */
        bool traceRpc = true;

        /**
         * Enable tracing for peer-to-peer messages (enabled by default;
         * high volume).
         */
        bool tracePeer = true;

        /**
         * Enable tracing for ledger close/accept.
         */
        bool traceLedger = true;

        /**
         * How a consensus round span picks its trace id.
         *
         * Read from `consensus_trace_strategy`. Deterministic is the strategy
         * in use; Random is experimental. See ConsensusTraceStrategy.
         */
        ConsensusTraceStrategy consensusTraceStrategy = ConsensusTraceStrategy::Deterministic;
    };

    virtual ~Telemetry() = default;

    /**
     * Update the service instance ID (OTel resource attribute
     * `service.instance.id`).
     *
     * Must be called before start(). The node public key is not available
     * when Telemetry is constructed (during the ApplicationImp member
     * initializer list), so this setter allows Application::setup() to
     * inject the identity once nodeIdentity_ is known.
     *
     * @param id  The node's base58-encoded public key or custom identifier.
     */
    virtual void
    setServiceInstanceId([[maybe_unused]] std::string const& id)
    {
        // Default no-op for NullTelemetry implementations.
    }

    /**
     * Initialize the tracing pipeline (exporter, processor, provider).
     * Call after construction.
     */
    virtual void
    start() = 0;

    /**
     * Flush pending spans and shut down the tracing pipeline.
     * Call before destruction.
     */
    virtual void
    stop() = 0;

    /**
     * @return true if this instance is actively exporting spans.
     */
    [[nodiscard]] virtual bool
    isEnabled() const = 0;

    /**
     * @return true if transaction processing should be traced.
     */
    [[nodiscard]] virtual bool
    shouldTraceTransactions() const = 0;

    /**
     * @return true if consensus rounds should be traced.
     */
    [[nodiscard]] virtual bool
    shouldTraceConsensus() const = 0;

    /**
     * @return true if RPC request handling should be traced.
     */
    [[nodiscard]] virtual bool
    shouldTraceRpc() const = 0;

    /**
     * @return true if peer-to-peer messages should be traced.
     */
    [[nodiscard]] virtual bool
    shouldTracePeer() const = 0;

    /**
     * @return true if ledger close/accept should be traced.
     */
    [[nodiscard]] virtual bool
    shouldTraceLedger() const = 0;

    /**
     * @return How a consensus round span picks its trace id.
     */
    [[nodiscard]] virtual ConsensusTraceStrategy
    getConsensusTraceStrategy() const = 0;

#ifdef XRPL_ENABLE_TELEMETRY
    /**
     * Get or create a named tracer instance.
     *
     * @param name  Tracer name used to identify the instrumentation library.
     * @return A shared pointer to the Tracer.
     */
    [[nodiscard]] virtual opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer>
    getTracer(std::string_view name = kTracerName) = 0;

    /**
     * Start a new span on the current thread's context.
     *
     * The span becomes a child of the current active span (if any) via
     * OpenTelemetry's context propagation.
     *
     * @param name  Span name (typically "rpc.command.<cmd>").
     * @param kind  The span kind (defaults to kInternal). Possible values:
     * - kInternal: default, in-process operation
     * - kServer:   incoming synchronous request (e.g. RPC)
     * - kClient:   outgoing synchronous request
     * - kProducer: async message send (e.g. peer broadcast)
     * - kConsumer: async message receive
     * @return A shared pointer to the new Span.
     */
    [[nodiscard]] virtual opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    startSpan(
        std::string_view name,
        opentelemetry::trace::SpanKind kind = opentelemetry::trace::SpanKind::kInternal) = 0;

    /**
     * Start a new span with an explicit parent context.
     *
     * Use this overload when the parent span is not on the current
     * thread's context stack (e.g. cross-thread trace propagation).
     *
     * @param name           Span name.
     * @param parentContext  The parent span's context.
     * @param kind           The span kind (defaults to kInternal).
     * @return A shared pointer to the new Span.
     */
    [[nodiscard]] virtual opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>
    startSpan(
        std::string_view name,
        opentelemetry::context::Context const& parentContext,
        opentelemetry::trace::SpanKind kind = opentelemetry::trace::SpanKind::kInternal) = 0;
#endif
};

/**
 * Create a Telemetry instance.
 *
 * With XRPL_ENABLE_TELEMETRY defined, returns a TelemetryImpl when
 * setup.enabled is true, or a no-op stub otherwise. Without it, the only
 * definition of this factory always returns the no-op stub and never reads
 * setup.enabled.
 *
 * @param setup    Configuration from the [telemetry] config section.
 * @param journal  Journal for log output during initialization.
 */
std::unique_ptr<Telemetry>
makeTelemetry(Telemetry::Setup const& setup, beast::Journal journal);

/**
 * Parse the [telemetry] config section into a Setup struct.
 *
 * @param section        The [telemetry] config section.
 * @param nodePublicKey  Node public key, used as default instance ID.
 * @param version        Build version string.
 * @param networkId      Network identifier from [network_id] config
 * (0 = mainnet, 1 = testnet, 2 = devnet).
 * @return A populated Setup struct with defaults for missing values.
 * @throws std::runtime_error  If `enabled` is set and the mutual TLS (mTLS)
 * settings contradict each other: only one of `tls_client_cert`/`tls_client_key`
 * is given, a client certificate is given while `use_tls` is 0, or a client
 * certificate is given while `traces_endpoint` is not an `https://` URL — which
 * includes leaving `traces_endpoint` at its plain-HTTP default. Also if
 * `enabled` and `use_tls` are both set and a non-empty `tls_ca_cert`,
 * `tls_client_cert` or `tls_client_key` cannot be read; an empty path is skipped,
 * so an empty `tls_ca_cert` still means "use the system CA store". All four
 * checks are skipped when `enabled` is 0.
 * @throws boost::bad_lexical_cast  If any numeric key (`enabled`, `use_tls`,
 * `batch_size`, the trace switches, ...) holds a value Section::valueOr cannot
 * convert. None of the numeric reads sit inside the `enabled` branch, so this
 * escapes whether telemetry is on or off.
 */
Telemetry::Setup
makeTelemetrySetup(
    Section const& section,
    std::string const& nodePublicKey,
    std::string const& version,
    std::uint32_t networkId);

#ifdef XRPL_ENABLE_TELEMETRY
/**
 * Build the OTLP/HTTP trace exporter options that Setup asks for.
 *
 *   Telemetry::Setup ──> makeTraceExporterOptions() ──> OtlpHttpExporterOptions
 *                                                             |
 *                                                             v
 *                                              OtlpHttpExporterFactory::Create
 *
 * Named and declared here rather than left inline in start() so the mapping
 * from config to exporter options can be asserted directly. A swapped
 * certificate and key, or a CA path written to the wrong field, is invisible
 * from outside a running exporter.
 *
 * TLS fields are set only when `use_tls` is on. Whether the transport is
 * actually encrypted is decided by the scheme of the URL, not by this function;
 * makeTelemetrySetup() is what rejects a client certificate on a plain-HTTP
 * endpoint.
 *
 * @code
 * // Primary use: one-way TLS with a custom CA bundle.
 * Telemetry::Setup setup;
 * setup.useTls = true;
 * setup.tlsCertPath = "/etc/ssl/ca.pem";
 * auto const opts = makeTraceExporterOptions(setup);   // ssl_ca_cert_path set
 *
 * // Edge case: use_tls off leaves every ssl_ field empty, even when paths
 * // are configured.
 * setup.useTls = false;
 * auto const plain = makeTraceExporterOptions(setup);  // ssl_ca_cert_path empty
 * @endcode
 *
 * @param setup  Parsed [telemetry] configuration.
 * @return Options carrying `traces_endpoint` as the URL, plus the TLS paths
 * when `use_tls` is on. Every other field keeps its SDK default.
 * @note Pure: reads `setup` and touches no global state, so it is safe to call
 * from any thread.
 */
[[nodiscard]] opentelemetry::exporter::otlp::OtlpHttpExporterOptions
makeTraceExporterOptions(Telemetry::Setup const& setup);
#endif

}  // namespace xrpl::telemetry
