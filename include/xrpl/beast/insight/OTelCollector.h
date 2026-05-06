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

namespace beast {
namespace insight {

/**
 * @brief A Collector that exports metrics via OpenTelemetry OTLP/HTTP.
 *
 * Replaces StatsD-based metric collection with native OTel Metrics SDK
 * instruments. Each beast::insight instrument maps to an OTel equivalent:
 *
 *   - Counter  -> OTel Counter<uint64_t>
 *   - Gauge    -> OTel ObservableGauge<int64_t> (async callback)
 *   - Event    -> OTel Histogram<double> (duration in milliseconds)
 *   - Meter    -> OTel Counter<uint64_t> (monotonic, unsigned)
 *   - Hook     -> Called by PeriodicMetricReader at collection time
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
     * @param prefix      Prefix prepended to all metric names
     *                    (e.g. "xrpld").
     * @param instanceId  Unique identifier for this node instance,
     *                    emitted as the `service.instance.id` OTel
     *                    resource attribute. Defaults to empty string
     *                    (attribute omitted when empty).
     * @param journal     Journal for logging.
     * @return Shared pointer to the created Collector.
     */
    static std::shared_ptr<Collector>
    New(std::string const& endpoint,
        std::string const& prefix,
        std::string const& instanceId,
        Journal journal);
};

}  // namespace insight
}  // namespace beast
