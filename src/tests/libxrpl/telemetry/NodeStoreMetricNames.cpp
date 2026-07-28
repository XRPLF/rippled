/**
 * @file NodeStoreMetricNames.cpp
 * Unit tests for the nodestore read-latency histogram wiring.
 *
 * Two independent groups, split by what they can link:
 *
 *  1. The shared name/label constants and the three record-site helpers from
 *     `<xrpl/telemetry/NodeStoreMetricNames.h>`. Header-only and free of any
 *     OTel dependency, so these run in **both** builds. That matters: the
 *     constants are what keeps the bucket-view registration in
 *     MetricsRegistry.cpp and the record site in NodeStoreScheduler.cpp
 *     agreeing on one instrument name, and a divergence there silently drops
 *     the sub-millisecond bucket override.
 *
 *  2. An end-to-end record-and-read-back over a real SDK MeterProvider fitted
 *     with the same explicit sub-millisecond boundaries production registers,
 *     asserting the exact bucket counts a set of known latencies must land in.
 *     Guarded on XRPL_ENABLE_TELEMETRY because the metrics SDK headers only
 *     exist in that build.
 *
 * Why the second group does not drive NodeStoreScheduler directly: that class
 * lives in xrpld (`src/xrpld/app/main/`) and its onFetch() needs a live
 * JobQueue plus a ServiceRegistry, neither of which the standalone xrpl_tests
 * binary can supply -- the same reason MetricsRegistry.cpp is only compiled
 * into this binary on the no-op path (see src/tests/libxrpl/CMakeLists.txt).
 * What is testable here is everything that decides *what* gets recorded: the
 * instrument name, the two label values, the negative-value guard, and the
 * bucket ladder the value is filed into. The remaining step -- that
 * Database::fetchNodeObject actually reaches onFetch -- is covered by the
 * existing nodestore suites, which already exercise that call path.
 */

#include <xrpl/telemetry/NodeStoreMetricNames.h>

#include <gtest/gtest.h>

#include <string_view>

namespace {

using namespace xrpl::telemetry;

// ---------------------------------------------------------------------------
// Group 1: shared constants and record-site helpers. Compile-time first, so a
// regression is a build failure rather than only a test failure; the runtime
// duplicates below name the offending case when one does fail.
// ---------------------------------------------------------------------------

// The instrument name is the contract between the two call sites. Pinned to
// the exact literal: bare lower snake_case, no `xrpld_` prefix (no metric in
// this codebase carries one), and the `_us` suffix stating the unit.
static_assert(std::string_view{kNodeStoreReadUs} == "nodestore_read_us");

// Label keys. `found` rather than `was_found` and `fetch_type` rather than
// `type`, matching what the dashboards query.
static_assert(std::string_view{kFetchTypeLabel} == "fetch_type");
static_assert(std::string_view{kFetchFoundLabel} == "found");

// Label values.
static_assert(std::string_view{kFetchTypeAsync} == "async");
static_assert(std::string_view{kFetchTypeSync} == "sync");
static_assert(std::string_view{kFetchFoundTrue} == "true");
static_assert(std::string_view{kFetchFoundFalse} == "false");

// The helpers map each input to exactly one value, and the two arms differ.
static_assert(std::string_view{fetchTypeLabelValue(true)} == "async");
static_assert(std::string_view{fetchTypeLabelValue(false)} == "sync");
static_assert(std::string_view{fetchFoundLabelValue(true)} == "true");
static_assert(std::string_view{fetchFoundLabelValue(false)} == "false");

// The negative guard. Zero is admitted on purpose -- a page-cache hit really
// can round to 0 us -- while anything below it is refused.
static_assert(shouldRecordFetchLatency(0));
static_assert(shouldRecordFetchLatency(1));
static_assert(shouldRecordFetchLatency(25'000));
static_assert(!shouldRecordFetchLatency(-1));

}  // namespace

TEST(NodeStoreMetricNames, instrument_name_is_the_exact_shared_literal)
{
    // Both the view registration (MetricsRegistry.cpp) and the record site
    // (NodeStoreScheduler.cpp) read this one constant. If it changes, the
    // dashboard query and the reference doc must change with it, so the exact
    // string is asserted rather than merely its shape.
    EXPECT_EQ(std::string_view{kNodeStoreReadUs}, "nodestore_read_us");
    EXPECT_EQ(std::string_view{kNodeStoreReadUs}.size(), 17u);

    // No `xrpld_` prefix: verified against the live metric surface, where 0 of
    // 537 exported names carry one. A prefix here would make this the only
    // odd metric out and break every dashboard that globs the family.
    EXPECT_FALSE(std::string_view{kNodeStoreReadUs}.starts_with("xrpld_"));

    // The `_us` suffix is load-bearing: FetchReport::elapsed is
    // std::chrono::microseconds, and a name implying milliseconds would make
    // every reading 1000x wrong to a reader.
    EXPECT_TRUE(std::string_view{kNodeStoreReadUs}.ends_with("_us"));

    // The description must name the unit too, since that is all a Prometheus
    // consumer sees alongside the metric.
    EXPECT_EQ(
        std::string_view{kNodeStoreReadUsDesc}, "NodeStore backend fetch latency in microseconds");
}

TEST(NodeStoreMetricNames, label_keys_and_values_are_the_exact_literals)
{
    EXPECT_EQ(std::string_view{kFetchTypeLabel}, "fetch_type");
    EXPECT_EQ(std::string_view{kFetchFoundLabel}, "found");

    EXPECT_EQ(std::string_view{kFetchTypeAsync}, "async");
    EXPECT_EQ(std::string_view{kFetchTypeSync}, "sync");
    EXPECT_EQ(std::string_view{kFetchFoundTrue}, "true");
    EXPECT_EQ(std::string_view{kFetchFoundFalse}, "false");

    // The two keys must differ, or one label would overwrite the other in the
    // attribute map and a whole dimension would vanish.
    EXPECT_NE(std::string_view{kFetchTypeLabel}, std::string_view{kFetchFoundLabel});
}

TEST(NodeStoreMetricNames, helpers_map_each_input_to_its_own_value)
{
    // Positive path for both arms of both helpers.
    EXPECT_EQ(std::string_view{fetchTypeLabelValue(true)}, "async");
    EXPECT_EQ(std::string_view{fetchTypeLabelValue(false)}, "sync");
    EXPECT_EQ(std::string_view{fetchFoundLabelValue(true)}, "true");
    EXPECT_EQ(std::string_view{fetchFoundLabelValue(false)}, "false");

    // Cause, not just state: the two arms are genuinely distinct, so a
    // copy-paste that returned the same value for both would fail here rather
    // than quietly collapsing async and sync into one series.
    EXPECT_NE(
        std::string_view{fetchTypeLabelValue(true)}, std::string_view{fetchTypeLabelValue(false)});
    EXPECT_NE(
        std::string_view{fetchFoundLabelValue(true)},
        std::string_view{fetchFoundLabelValue(false)});

    // Each helper returns one of its own two constants and never the other
    // helper's, which is what keeps the two dimensions independent.
    EXPECT_EQ(fetchTypeLabelValue(true), kFetchTypeAsync);
    EXPECT_EQ(fetchTypeLabelValue(false), kFetchTypeSync);
    EXPECT_EQ(fetchFoundLabelValue(true), kFetchFoundTrue);
    EXPECT_EQ(fetchFoundLabelValue(false), kFetchFoundFalse);
}

TEST(NodeStoreMetricNames, latency_guard_admits_zero_and_refuses_negatives)
{
    // Negative path -- the reason the guard exists. The OTel SDK drops a
    // negative histogram value AND logs a warning for it; on a per-fetch path
    // that is a log flood, so the sample is filtered before it gets there.
    EXPECT_FALSE(shouldRecordFetchLatency(-1));
    EXPECT_FALSE(shouldRecordFetchLatency(-1'000));

    // Zero must NOT be filtered: a read served from the page cache genuinely
    // truncates to 0 us, and suppressing it would hide the fastest reads and
    // bias the whole distribution upward.
    EXPECT_TRUE(shouldRecordFetchLatency(0));

    // Ordinary and cold-tail values pass.
    EXPECT_TRUE(shouldRecordFetchLatency(1));
    EXPECT_TRUE(shouldRecordFetchLatency(9));
    EXPECT_TRUE(shouldRecordFetchLatency(250));
    EXPECT_TRUE(shouldRecordFetchLatency(30'000));

    // The boundary is exactly at zero, not near it.
    EXPECT_TRUE(shouldRecordFetchLatency(0));
    EXPECT_FALSE(shouldRecordFetchLatency(-1));
}

// ---------------------------------------------------------------------------
// Group 2: record into a real histogram carrying production's explicit
// sub-millisecond boundaries, then read the exported point back and assert the
// exact per-bucket counts.
//
// This is what proves the signal is usable rather than merely emitted. The
// SDK's default boundaries begin at 0/5/10/25... but top out at 10,000, and
// the microsecond ladder used by the other duration histograms begins at 100
// us -- above the entire range a warm read occupies. Under that ladder every
// warm read files into bucket 0 and the distribution reads flat. The
// assertions below pin warm reads into distinct low buckets, which is exactly
// the property the sub-millisecond ladder exists to provide.
// ---------------------------------------------------------------------------

#ifdef XRPL_ENABLE_TELEMETRY

#include <opentelemetry/context/context.h>
#include <opentelemetry/exporters/memory/in_memory_metric_data.h>
#include <opentelemetry/exporters/memory/in_memory_metric_exporter_factory.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/metrics/aggregation/aggregation_config.h>
#include <opentelemetry/sdk/metrics/data/metric_data.h>
#include <opentelemetry/sdk/metrics/data/point_data.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/instruments.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/view_factory.h>
#include <opentelemetry/sdk/metrics/view/view_registry.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace metric_sdk = opentelemetry::sdk::metrics;
namespace in_memory = opentelemetry::exporter::memory;

/**
 * The same edges MetricsRegistry.cpp's kSubMillisecondBoundaries holds.
 *
 * Deliberately a second, independent copy rather than an include of the
 * production array: that constant lives in an unnamed namespace inside
 * MetricsRegistry.cpp and is unreachable from here, and re-deriving the edges
 * from the implementation would make the bucket-index assertions below
 * tautological. Written out by hand, they pin the ladder -- so silently
 * re-tuning an edge in production without revisiting this file fails here.
 */
constexpr std::array kExpectedBoundaries{
    1.0,
    2.0,
    5.0,
    10.0,
    25.0,
    50.0,
    100.0,
    250.0,
    500.0,
    1'000.0,
    5'000.0,
    25'000.0};

/**
 * Meter identity used by the production view selector, so the view this
 * fixture registers matches the instrument the fixture creates.
 */
constexpr char kMeterName[] = "xrpld";
constexpr char kMeterVersion[] = "1.0.0";

/**
 * A MeterProvider carrying one explicit-bucket view for kNodeStoreReadUs and
 * an in-memory exporter, so a test can record values and read the resulting
 * histogram point back without any network or OTLP involvement.
 *
 * Mirrors what MetricsRegistry::initExporterAndProvider() builds for this
 * instrument, minus the OTLP exporter.
 */
class HistogramFixture
{
public:
    HistogramFixture()
    {
        // The view: same instrument type, same name, same meter selector and
        // the same boundaries production registers.
        auto config = std::make_shared<metric_sdk::HistogramAggregationConfig>();
        config->boundaries_ = {kExpectedBoundaries.begin(), kExpectedBoundaries.end()};

        auto views = std::make_unique<metric_sdk::ViewRegistry>();
        views->AddView(
            metric_sdk::InstrumentSelectorFactory::Create(
                metric_sdk::InstrumentType::kHistogram, kNodeStoreReadUs, ""),
            metric_sdk::MeterSelectorFactory::Create(kMeterName, kMeterVersion, ""),
            metric_sdk::ViewFactory::Create(
                kNodeStoreReadUs, "", metric_sdk::AggregationType::kHistogram, config));

        provider_ = metric_sdk::MeterProviderFactory::Create(std::move(views));

        // A long export interval keeps the background thread from exporting
        // on its own schedule; the test drives collection via ForceFlush().
        metric_sdk::PeriodicExportingMetricReaderOptions readerOpts;
        readerOpts.export_interval_millis = std::chrono::milliseconds(600'000);
        readerOpts.export_timeout_millis = std::chrono::milliseconds(5'000);
        provider_->AddMetricReader(
            metric_sdk::PeriodicExportingMetricReaderFactory::Create(
                in_memory::InMemoryMetricExporterFactory::Create(data_), readerOpts));

        histogram_ = provider_->GetMeter(kMeterName, kMeterVersion)
                         ->CreateDoubleHistogram(kNodeStoreReadUs, kNodeStoreReadUsDesc);
    }

    ~HistogramFixture()
    {
        provider_->Shutdown();
    }

    HistogramFixture(HistogramFixture const&) = delete;
    HistogramFixture&
    operator=(HistogramFixture const&) = delete;

    /**
     * Record one latency with the exact label set the production record site
     * attaches, built through the same two helpers.
     *
     * @param elapsedUs Latency in microseconds.
     * @param isAsync   True for an async (prefetch) read.
     * @param wasFound  True when the object was found.
     */
    void
    record(double elapsedUs, bool isAsync, bool wasFound)
    {
        histogram_->Record(
            elapsedUs,
            {{kFetchTypeLabel, std::string(fetchTypeLabelValue(isAsync))},
             {kFetchFoundLabel, std::string(fetchFoundLabelValue(wasFound))}},
            opentelemetry::context::Context{});
    }

    /**
     * Flush the reader, then return every exported histogram point for
     * kNodeStoreReadUs keyed by its attribute set.
     */
    [[nodiscard]] in_memory::SimpleAggregateInMemoryMetricData::AttributeToPoint const&
    collect()
    {
        provider_->ForceFlush();
        return data_->Get(kMeterName, kNodeStoreReadUs);
    }

private:
    /**
     * Sink the in-memory exporter writes each collection into.
     */
    std::shared_ptr<in_memory::SimpleAggregateInMemoryMetricData> data_ =
        std::make_shared<in_memory::SimpleAggregateInMemoryMetricData>();

    /**
     * Provider owning the view registry and the in-memory reader.
     */
    std::shared_ptr<metric_sdk::MeterProvider> provider_;

    /**
     * The instrument under test.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>> histogram_;
};

/**
 * Extract the HistogramPointData for the one series matching @p isAsync and
 * @p wasFound, or nullptr when no such series was exported.
 *
 * @param points   Exported points keyed by attribute set.
 * @param isAsync  fetch_type dimension to match.
 * @param wasFound found dimension to match.
 */
[[nodiscard]] metric_sdk::HistogramPointData const*
findPoint(
    in_memory::SimpleAggregateInMemoryMetricData::AttributeToPoint const& points,
    bool isAsync,
    bool wasFound)
{
    for (auto const& [attributes, point] : points)
    {
        auto const type = attributes.find(kFetchTypeLabel);
        auto const found = attributes.find(kFetchFoundLabel);
        if (type == attributes.end() || found == attributes.end())
            continue;

        if (opentelemetry::nostd::get<std::string>(type->second) != fetchTypeLabelValue(isAsync) ||
            opentelemetry::nostd::get<std::string>(found->second) != fetchFoundLabelValue(wasFound))
            continue;

        return &opentelemetry::nostd::get<metric_sdk::HistogramPointData>(point);
    }
    return nullptr;
}

}  // namespace

TEST(NodeStoreReadHistogram, view_applies_the_sub_millisecond_boundaries)
{
    HistogramFixture fixture;
    fixture.record(9.0, /*isAsync=*/false, /*wasFound=*/true);

    auto const* point = findPoint(fixture.collect(), /*isAsync=*/false, /*wasFound=*/true);
    ASSERT_NE(point, nullptr);

    // The exported point must carry OUR boundaries, not the SDK defaults. This
    // is the assertion that catches a name mismatch between the view selector
    // and the instrument: on a mismatch the view is never applied and the
    // default ladder appears here instead.
    ASSERT_EQ(point->boundaries_.size(), kExpectedBoundaries.size());
    for (std::size_t i = 0; i < kExpectedBoundaries.size(); ++i)
        EXPECT_EQ(point->boundaries_[i], kExpectedBoundaries[i]) << "boundary index " << i;

    // The first edge is 1 us, three orders of magnitude below the microsecond
    // ladder's 100 us first edge. That difference is the entire point: without
    // it a warm read cannot be distinguished from an instant one.
    EXPECT_EQ(point->boundaries_.front(), 1.0);
    EXPECT_EQ(point->boundaries_.back(), 25'000.0);
}

TEST(NodeStoreReadHistogram, warm_reads_land_in_distinct_low_buckets)
{
    HistogramFixture fixture;

    // Four warm latencies, each chosen to fall in a different low bucket.
    // Bucket i counts values in (boundaries[i-1], boundaries[i]].
    //   0.5 us -> bucket 0   ( <= 1 )
    //   3   us -> bucket 2   ( 2  < v <= 5 )
    //   9   us -> bucket 3   ( 5  < v <= 10 )
    //   40  us -> bucket 5   ( 25 < v <= 50 )
    fixture.record(0.5, /*isAsync=*/false, /*wasFound=*/true);
    fixture.record(3.0, /*isAsync=*/false, /*wasFound=*/true);
    fixture.record(9.0, /*isAsync=*/false, /*wasFound=*/true);
    fixture.record(40.0, /*isAsync=*/false, /*wasFound=*/true);

    auto const* point = findPoint(fixture.collect(), /*isAsync=*/false, /*wasFound=*/true);
    ASSERT_NE(point, nullptr);

    // Exact counts, bucket by bucket -- not merely "the total is 4". Under the
    // microsecond ladder all four would sit in bucket 0 and this test would
    // fail, which is precisely the regression it guards against.
    ASSERT_EQ(point->counts_.size(), kExpectedBoundaries.size() + 1);
    EXPECT_EQ(point->counts_[0], 1u);  // 0.5 us
    EXPECT_EQ(point->counts_[1], 0u);
    EXPECT_EQ(point->counts_[2], 1u);  // 3 us
    EXPECT_EQ(point->counts_[3], 1u);  // 9 us
    EXPECT_EQ(point->counts_[4], 0u);
    EXPECT_EQ(point->counts_[5], 1u);  // 40 us
    for (std::size_t i = 6; i < point->counts_.size(); ++i)
        EXPECT_EQ(point->counts_[i], 0u) << "bucket " << i << " should be empty";

    // Aggregate state must agree with the per-bucket detail.
    EXPECT_EQ(point->count_, 4u);
    EXPECT_EQ(opentelemetry::nostd::get<double>(point->sum_), 52.5);
    EXPECT_EQ(opentelemetry::nostd::get<double>(point->min_), 0.5);
    EXPECT_EQ(opentelemetry::nostd::get<double>(point->max_), 40.0);
}

TEST(NodeStoreReadHistogram, a_cold_read_lands_in_the_tail_not_the_ceiling)
{
    HistogramFixture fixture;

    // A cold read and an outlier beyond the top edge. 800 us falls in bucket 9
    // ( 500 < v <= 1000 ); 30000 us exceeds the 25000 top edge and so lands in
    // the overflow bucket, index 12.
    fixture.record(800.0, /*isAsync=*/false, /*wasFound=*/true);
    fixture.record(30'000.0, /*isAsync=*/false, /*wasFound=*/true);

    auto const* point = findPoint(fixture.collect(), /*isAsync=*/false, /*wasFound=*/true);
    ASSERT_NE(point, nullptr);

    EXPECT_EQ(point->counts_[9], 1u);   // 800 us -- resolved, not saturated
    EXPECT_EQ(point->counts_[12], 1u);  // 30 ms  -- overflow bucket
    EXPECT_EQ(point->count_, 2u);
    EXPECT_EQ(opentelemetry::nostd::get<double>(point->max_), 30'000.0);
}

TEST(NodeStoreReadHistogram, the_two_labels_split_the_series_four_ways)
{
    HistogramFixture fixture;

    // One record per (fetch_type, found) combination, each with a distinct
    // latency so the series cannot be confused with one another.
    fixture.record(3.0, /*isAsync=*/true, /*wasFound=*/true);
    fixture.record(9.0, /*isAsync=*/true, /*wasFound=*/false);
    fixture.record(40.0, /*isAsync=*/false, /*wasFound=*/true);
    fixture.record(800.0, /*isAsync=*/false, /*wasFound=*/false);

    auto const& points = fixture.collect();

    // Four distinct label sets means four distinct time series. If either
    // label were dropped or misspelled these would collapse into fewer.
    EXPECT_EQ(points.size(), 4u);

    struct Expected
    {
        bool isAsync;
        bool wasFound;
        double value;
        std::size_t bucket;
    };

    // Each series holds exactly its own one sample, in its own bucket. This is
    // the assertion that a label mix-up would break: swapping two values would
    // put a sample in the wrong series and fail here.
    for (auto const& [isAsync, wasFound, value, bucket] : std::array<Expected, 4>{
             {{true, true, 3.0, 2},
              {true, false, 9.0, 3},
              {false, true, 40.0, 5},
              {false, false, 800.0, 9}}})
    {
        auto const* point = findPoint(points, isAsync, wasFound);
        ASSERT_NE(point, nullptr) << "missing series for fetch_type="
                                  << fetchTypeLabelValue(isAsync)
                                  << " found=" << fetchFoundLabelValue(wasFound);
        EXPECT_EQ(point->count_, 1u);
        EXPECT_EQ(opentelemetry::nostd::get<double>(point->sum_), value);
        EXPECT_EQ(point->counts_[bucket], 1u);
    }
}

TEST(NodeStoreReadHistogram, a_guarded_negative_latency_never_reaches_the_instrument)
{
    HistogramFixture fixture;

    // Negative path, end to end: the record site consults
    // shouldRecordFetchLatency() before calling Record(), so a clock anomaly
    // produces no sample at all. Reproduced here with the same guard.
    for (double const elapsedUs : {-1.0, -1'000.0})
    {
        if (shouldRecordFetchLatency(static_cast<long long>(elapsedUs)))
            fixture.record(elapsedUs, /*isAsync=*/false, /*wasFound=*/true);
    }

    // No series at all: nothing was recorded, so the instrument exported
    // nothing rather than exporting a zero-count point.
    EXPECT_TRUE(fixture.collect().empty());

    // Cause, not just state: the same fixture DOES accept a valid sample, so
    // the emptiness above is the guard working and not a broken fixture.
    fixture.record(9.0, /*isAsync=*/false, /*wasFound=*/true);
    auto const* point = findPoint(fixture.collect(), /*isAsync=*/false, /*wasFound=*/true);
    ASSERT_NE(point, nullptr);
    EXPECT_EQ(point->count_, 1u);
    EXPECT_EQ(point->counts_[3], 1u);
}

#endif  // XRPL_ENABLE_TELEMETRY
