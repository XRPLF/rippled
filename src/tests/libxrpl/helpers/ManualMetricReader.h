#pragma once

/**
 * @file ManualMetricReader.h
 * A metric reader that collects on demand, for tests that need a real SDK
 * MeterProvider without a background export thread.
 *
 * Guarded as a whole: every type it names comes from the OpenTelemetry metrics
 * SDK, which is on the link line only when XRPL_ENABLE_TELEMETRY is defined.
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <opentelemetry/nostd/function_ref.h>
#include <opentelemetry/sdk/metrics/export/metric_producer.h>
#include <opentelemetry/sdk/metrics/instruments.h>
#include <opentelemetry/sdk/metrics/metric_reader.h>

#include <chrono>

namespace xrpl::test {

/**
 * A MetricReader that collects only when the test asks it to.
 *
 * The SDK ships only PeriodicExportingMetricReader, whose background thread
 * would make a test depend on timing. MetricReader::Collect() is public and
 * synchronous, so a minimal subclass lets a test drive one collection pass on
 * the calling thread. That pass is what invokes an observable instrument's
 * callback.
 *
 * The SDK types are spelled in full rather than through a namespace alias. An
 * alias here would be a member of xrpl::test, and a translation unit that
 * pulled that namespace in wholesale could then find two spellings of the same
 * short name.
 *
 * @code
 * auto reader = std::make_shared<ManualMetricReader>();
 * provider->AddMetricReader(reader);
 * reader->collectOnce();  // runs every registered observable callback
 * @endcode
 */
class ManualMetricReader : public opentelemetry::sdk::metrics::MetricReader
{
public:
    /**
     * @brief Run exactly one collection pass, discarding the metric data.
     *
     * A caller asserting on callback side effects does not need the exported
     * points, so the callback returns true without inspecting what it was
     * handed.
     */
    void
    collectOnce()
    {
        Collect([](opentelemetry::sdk::metrics::ResourceMetrics&) { return true; });
    }

    [[nodiscard]] opentelemetry::sdk::metrics::AggregationTemporality
    GetAggregationTemporality(opentelemetry::sdk::metrics::InstrumentType) const noexcept override
    {
        return opentelemetry::sdk::metrics::AggregationTemporality::kCumulative;
    }

    bool
    OnForceFlush(std::chrono::microseconds) noexcept override
    {
        return true;
    }

    bool
    OnShutDown(std::chrono::microseconds) noexcept override
    {
        return true;
    }
};

}  // namespace xrpl::test

#endif  // XRPL_ENABLE_TELEMETRY
