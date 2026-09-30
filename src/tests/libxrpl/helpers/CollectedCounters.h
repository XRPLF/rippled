#pragma once

/**
 * @file CollectedCounters.h
 * Read what a metric reader collects as plain values, so a test can compare a
 * whole collection with one exact assertion.
 *
 * Guarded as a whole, like ManualMetricReader.h: every type it reads comes
 * from the OpenTelemetry metrics SDK, which is on the link line only when
 * XRPL_ENABLE_TELEMETRY is defined.
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <gtest/gtest.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/metrics/data/metric_data.h>
#include <opentelemetry/sdk/metrics/data/point_data.h>
#include <opentelemetry/sdk/metrics/export/metric_producer.h>
#include <opentelemetry/sdk/metrics/metric_reader.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <utility>

namespace xrpl::test {

/**
 * The labels of one exported point, as key to string value.
 */
using CounterLabels = std::map<std::string, std::string>;

/**
 * One counter as one collection saw it.
 *
 *   MetricReader --Collect()--> collectCounters() --> name -> CollectedCounter
 *
 * The two fields are what a counter test pins: how many streams carried the
 * name, and the value of every point by its labels. Equality is defaulted, so
 * one EXPECT_EQ on the map collectCounters() returns checks all of it.
 *
 * @code
 * // Primary use: pin everything one collection exported.
 * std::map<std::string, CollectedCounter> const expected{
 *     {"txq_expired_total", {.streams = 1, .points = {{CounterLabels{}, 0}}}}};
 * EXPECT_EQ(collectCounters(*reader), expected);
 *
 * // Edge case: one name on two streams, as when two instruments of that
 * // name were created with different descriptions.
 * EXPECT_EQ(collectCounters(*reader).at("ledgers_closed_total").streams, 1u);
 * @endcode
 *
 * @note A plain value with no shared state, so reading one from several
 * threads is safe.
 * @note Integer sums only. collectCounters() fails the running test for any
 * other point, such as a histogram, rather than dropping it.
 */
struct CollectedCounter
{
    /**
     * Streams that carried this name in one collection. More than one means
     * two instruments of this name did not share storage: they differed in
     * identity, such as the description, or came from different meters.
     */
    std::size_t streams{0};

    /**
     * Value of each point, by its labels. An unlabelled point has empty
     * labels.
     */
    std::map<CounterLabels, std::int64_t> points;

    /**
     * Lets a test compare a whole collection in one assertion.
     */
    bool
    operator==(CollectedCounter const&) const = default;
};

/**
 * Print a counter in a gtest failure message.
 *
 * @param os      The stream gtest prints to.
 * @param counter The counter to print.
 * @return @p os.
 */
inline std::ostream&
operator<<(std::ostream& os, CollectedCounter const& counter)
{
    os << "{streams=" << counter.streams << ", points={";
    for (auto const& [labels, value] : counter.points)
    {
        os << " {";
        for (auto const& [key, text] : labels)
            os << ' ' << key << '=' << text;
        os << " }=" << value;
    }
    return os << " }}";
}

/**
 * Add one exported point to @p counter.
 *
 * A point that is not an integer sum with string labels fails the running
 * test instead of being dropped, so an assertion on the result cannot pass
 * by missing it.
 *
 * @param counter Where the point goes.
 * @param name    The instrument name, for the failure message.
 * @param point   The point as the SDK exported it.
 */
inline void
addCounterPoint(
    CollectedCounter& counter,
    std::string const& name,
    opentelemetry::sdk::metrics::PointDataAttributes const& point)
{
    auto const* const sum =
        opentelemetry::nostd::get_if<opentelemetry::sdk::metrics::SumPointData>(&point.point_data);
    auto const* const value =
        sum != nullptr ? opentelemetry::nostd::get_if<std::int64_t>(&sum->value_) : nullptr;
    if (value == nullptr)
    {
        ADD_FAILURE() << name << " exported a point that is not an integer sum";
        return;
    }

    CounterLabels labels;
    for (auto const& [key, attribute] : point.attributes)
    {
        auto const* const text = opentelemetry::nostd::get_if<std::string>(&attribute);
        if (text == nullptr)
        {
            ADD_FAILURE() << name << " has a label that is not a string: " << key;
            continue;
        }
        labels.emplace(key, *text);
    }
    counter.points.emplace(std::move(labels), *value);
}

/**
 * Run one collection through @p reader and gather every counter it saw.
 *
 * @param reader A reader attached to a provider that still exists. Once the
 * provider is destroyed, collecting reads freed memory: the reader keeps a
 * raw pointer into it.
 * @return Each counter seen, by instrument name.
 *
 * @note The SDK serializes each collection against recording and against
 * other collections, so any thread may call this.
 */
[[nodiscard]] inline std::map<std::string, CollectedCounter>
collectCounters(opentelemetry::sdk::metrics::MetricReader& reader)
{
    std::map<std::string, CollectedCounter> counters;
    bool const collected =
        reader.Collect([&counters](opentelemetry::sdk::metrics::ResourceMetrics& data) {
            for (auto const& scope : data.scope_metric_data_)
            {
                for (auto const& metric : scope.metric_data_)
                {
                    auto const& name = metric.instrument_descriptor.name_;
                    auto& counter = counters[name];
                    ++counter.streams;
                    for (auto const& point : metric.point_data_attr_)
                        addCounterPoint(counter, name, point);
                }
            }
            return true;
        });
    EXPECT_TRUE(collected) << "the reader is not attached to a live pipeline";
    return counters;
}

}  // namespace xrpl::test

#endif  // XRPL_ENABLE_TELEMETRY
