#pragma once

/**
 * @file OTelCollector.h
 * @brief OpenTelemetry-based implementation of the beast::insight::Collector
 *        interface for native OTLP metric export.
 *
 * When XRPL_ENABLE_TELEMETRY is defined, OTelCollector maps each
 * beast::insight instrument type (Counter, Gauge, Event, Meter, Hook) to
 * the corresponding OpenTelemetry Metrics SDK instrument and exports
 * them via OTLP/HTTP to an OpenTelemetry Collector.
 *
 * When XRPL_ENABLE_TELEMETRY is NOT defined, OTelCollector::New() returns
 * a NullCollector so the binary compiles without OTel dependencies.
 *
 * Dependency diagram:
 *
 *   +-----------------+     +-------------------+
 *   | Collector (ABC) |<----| OTelCollector     |
 *   +-----------------+     | (public header)   |
 *         ^                 +-------------------+
 *         |                          |
 *   +-----------------+     +-------------------+
 *   | NullCollector   |     | OTelCollectorImp  |
 *   | (fallback when  |     | (impl in .cpp,    |
 *   |  no telemetry)  |     |  uses OTel SDK)   |
 *   +-----------------+     +-------------------+
 *                                    |
 *                           +-------------------+
 *                           | OTel Metrics SDK  |
 *                           | MeterProvider     |
 *                           | OTLP HTTP Metric  |
 *                           | Exporter          |
 *                           +-------------------+
 */

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/utility/Journal.h>

#include <memory>
#include <string>
#include <string_view>

namespace beast::insight {

/**
 * Instrumentation scope this collector fetches its Meter under.
 *
 * Must equal xrpl::telemetry::kMeterName and kMeterVersion, or instruments land
 * on a different scope than the views. Duplicated because beast sits below the
 * telemetry module and cannot include its header; Telemetry.cpp static_asserts
 * the two agree.
 */
inline constexpr std::string_view kOTelMeterName{"xrpld"};
inline constexpr std::string_view kOTelMeterVersion{"1.0.0"};

/**
 * @brief A Collector that exports metrics via OpenTelemetry OTLP/HTTP.
 *
 * Selected by `[insight] server=otel`, as an alternative to StatsDCollector:
 * it exports through the native OTel Metrics SDK rather than the StatsD wire
 * format. Each beast::insight instrument maps to an OTel equivalent:
 *
 *   - Counter  -> OTel Counter<uint64_t>
 *   - Gauge    -> OTel ObservableGauge<int64_t> (async callback)
 *   - Event    -> OTel Histogram<double> (duration in milliseconds)
 *   - Meter    -> OTel Counter<uint64_t> (monotonic, unsigned)
 *   - Hook     -> Called by PeriodicMetricReader at collection time
 *
 * Example — primary use (create the collector and record a metric):
 * @code
 *   auto collector = beast::insight::OTelCollector::New(
 *       "http://localhost:4318/v1/metrics",  // OTLP/HTTP endpoint
 *       "xrpld",                              // legacy prefix (log only)
 *       "node-1",                             // service.instance.id
 *       "xrpld",                              // service.name
 *       "mainnet",                            // xrpl.network.type
 *       journal);
 *
 *   auto counter = collector->makeCounter("ledgers", "closed");
 *   ++counter;  // exported on the next PeriodicMetricReader tick
 * @endcode
 *
 * Example — edge case (telemetry disabled at compile time): New()
 * returns a NullCollector, so callers need no #ifdef guard. The same
 * instruments compile and run, but recording is a no-op.
 * @code
 *   auto collector = beast::insight::OTelCollector::New(
 *       endpoint, prefix, instanceId, serviceName, networkType, journal);
 *   auto gauge = collector->makeGauge("peers", "count");
 *   gauge = 42;  // silently discarded when XRPL_ENABLE_TELEMETRY is off
 * @endcode
 *
 * @note Thread safety: instrument recording (Counter::Add,
 *       Histogram::Record, and atomic gauge writes) is thread-safe and
 *       may be called concurrently. Instrument *creation* (make_counter,
 *       make_gauge, etc.) is serialized by an internal mutex. Hook and
 *       observable-gauge callbacks run on the SDK's collection thread, so
 *       any state they read must itself be thread-safe.
 * @note Limitations: metrics export over OTLP/HTTP only (no gRPC); the
 *       PeriodicMetricReader interval is fixed at 1s; and gauge values
 *       are stored as int64_t, so fractional gauges are truncated.
 *
 * @see StatsDCollector for the StatsD-based alternative.
 * @see NullCollector for the no-op fallback.
 */
class OTelCollector : public Collector
{
public:
    explicit OTelCollector() = default;

    /**
     * @brief Factory method to create an OTelCollector instance.
     *
     * When XRPL_ENABLE_TELEMETRY is defined, creates a real OTel-backed
     * collector that exports metrics via OTLP/HTTP. When telemetry is
     * disabled at compile time, returns a NullCollector.
     *
     * @param endpoint    OTLP/HTTP metrics endpoint URL
     *                    (e.g. "http://localhost:4318/v1/metrics").
     * @param prefix      Legacy metric-name prefix (e.g. "xrpld"). Not
     *                    prepended to metric names; logged at startup only.
     *                    The `service.name` resource attribute identifies
     *                    the service.
     * @param instanceId  Unique identifier for this node instance,
     *                    emitted as the `service.instance.id` OTel
     *                    resource attribute. Defaults to empty string
     *                    (attribute omitted when empty).
     * @param serviceName Value for the `service.name` OTel resource
     *                    attribute. When empty, defaults to "xrpld".
     *                    Matches the trace exporter's service.name so
     *                    metrics and traces share one service identity.
     * @param networkType Value for the `xrpl.network.type` OTel resource
     *                    attribute (e.g. "mainnet"). When empty, the
     *                    attribute is omitted. Supplied by the node so
     *                    metrics carry the same network label as traces.
     * @param journal     Journal for logging.
     * @return Shared pointer to the created Collector.
     */
    [[nodiscard]] static std::shared_ptr<Collector>
    // NOLINTNEXTLINE(readability-identifier-naming)
    New(std::string const& endpoint,
        std::string const& prefix,
        std::string const& instanceId,
        std::string const& serviceName,
        std::string const& networkType,
        Journal journal);
};

}  // namespace beast::insight
