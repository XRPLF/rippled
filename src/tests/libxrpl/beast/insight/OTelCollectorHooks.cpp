#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Counter.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Meter.h>
#include <xrpl/beast/insight/OTelCollector.h>
#include <xrpl/beast/utility/Journal.h>

#include <gtest/gtest.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/nostd/function_ref.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/metrics/data/metric_data.h>
#include <opentelemetry/sdk/metrics/data/point_data.h>
#include <opentelemetry/sdk/metrics/export/metric_producer.h>
#include <opentelemetry/sdk/metrics/instruments.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/metric_reader.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beast::insight {

namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;

/**
 * A MetricReader that collects only when the test asks it to.
 *
 * The SDK ships only PeriodicExportingMetricReader, whose background thread
 * would make these tests depend on timing. MetricReader::Collect() is public
 * and synchronous, so a minimal subclass lets a test drive one collection pass
 * on the calling thread. That pass is what invokes an observable gauge's
 * callback, which is the only path that reaches the collector's hooks.
 *
 * @code
 * auto reader = std::make_shared<ManualMetricReader>();
 * provider->AddMetricReader(reader);
 * reader->collectOnce();  // runs every registered observable callback
 * @endcode
 */
class ManualMetricReader : public metrics_sdk::MetricReader
{
public:
    /**
     * @brief Run exactly one collection pass, discarding the metric data.
     *
     * The hook tests assert on side effects, not on exported points, so the
     * callback returns true without inspecting what it was handed.
     */
    void
    collectOnce()
    {
        Collect([](metrics_sdk::ResourceMetrics&) { return true; });
    }

    [[nodiscard]] metrics_sdk::AggregationTemporality
    GetAggregationTemporality(metrics_sdk::InstrumentType) const noexcept override
    {
        return metrics_sdk::AggregationTemporality::kCumulative;
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

/**
 * Installs a real SDK MeterProvider so instruments record and gauges fire.
 *
 * OTelCollector takes its Meter from the global provider. Under the default
 * noop provider nothing is recorded and no observable callback runs, so a
 * test could not see what the collector did. The fixture swaps in an SDK
 * provider with a ManualMetricReader and restores the previous global provider
 * afterwards, so it leaks no state into other telemetry tests in this binary.
 *
 * @code
 *   OTelCollectorTest --installs--> SDK MeterProvider --> ManualMetricReader
 *          ^
 *          |  derived by
 *          +-- OTelCollectorHooks     (hook lifetime)
 *          +-- OTelCollectorCounters  (counter and meter points)
 * @endcode
 *
 * @code
 * // Primary use, in a TEST_F body on a derived fixture: build the collector
 * // there, then run a pass.
 * auto const collector = makeCollector();
 * auto const counter = collector->makeCounter("probe");
 * counter.increment(1);
 * reader_->collectOnce();
 *
 * // Edge case: a gauge is observed only after onCollectionReady().
 * auto const gauge = collector->makeGauge("probe_gauge");
 * reader_->collectOnce();  // not armed yet: its callback does not run
 * collector->onCollectionReady();
 * reader_->collectOnce();  // armed: its callback runs
 * @endcode
 *
 * @note Build each collector in the test body. A collector takes its Meter
 * from the global provider when it is built, so one built before SetUp()
 * reports to the previous provider, and reader_ never sees it.
 * @note Not thread-safe. SetUp() and TearDown() swap the process-wide
 * provider, and reader_ collects on the calling thread, so use the fixture
 * from the test thread only.
 */
class OTelCollectorTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        previous_ = metrics_api::Provider::GetMeterProvider();
        reader_ = std::make_shared<ManualMetricReader>();
        auto provider = metrics_sdk::MeterProviderFactory::Create();
        provider->AddMetricReader(reader_);
        provider_ = std::shared_ptr<metrics_sdk::MeterProvider>(std::move(provider));
        metrics_api::Provider::SetMeterProvider(
            opentelemetry::nostd::shared_ptr<metrics_api::MeterProvider>(provider_));
    }

    void
    TearDown() override
    {
        metrics_api::Provider::SetMeterProvider(previous_);
        provider_.reset();
        reader_.reset();
    }

    /**
     * @brief Build a collector on the provider SetUp() installed.
     * @return A collector whose instruments reader_ collects.
     */
    static Collector::Ptr
    makeCollector()
    {
        return OTelCollector::New(
            "http://127.0.0.1:4318/v1/metrics",
            "",
            "test-instance",
            "xrpld",
            "test",
            Journal(Journal::getNullSink()));
    }

    /**
     * Global provider from before SetUp(), put back by TearDown().
     */
    opentelemetry::nostd::shared_ptr<metrics_api::MeterProvider> previous_;

    /**
     * Reader attached to provider_. Tests run collection passes through it.
     */
    std::shared_ptr<ManualMetricReader> reader_;

    /**
     * SDK provider that is the global provider while the test runs.
     */
    std::shared_ptr<metrics_sdk::MeterProvider> provider_;
};

/**
 * Fixture for the hook tests, which reach the hooks through an armed gauge.
 *
 * @code
 *   reader_->collectOnce()
 *     --> armed gauge's callback
 *       --> OTelCollectorImp::callHooks()
 *         --> hook handlers
 * @endcode
 */
class OTelCollectorHooks : public OTelCollectorTest
{
protected:
    /**
     * @brief Build a collector, plus the armed gauge that drives its hooks.
     *
     * A collection pass only reaches the hooks through an observable gauge's
     * callback, and a gauge is armed by onCollectionReady(), so every hook
     * test needs both. The gauge is returned because dropping it would
     * unregister the callback.
     *
     * Each test builds its own collector: the hook debounce is keyed to the
     * time of the last invocation, which starts unset, so the first collection
     * on a fresh collector always runs the hooks.
     */
    static std::pair<Collector::Ptr, Gauge>
    makeArmedCollector()
    {
        auto collector = makeCollector();
        auto gauge = collector->makeGauge("hook_test_gauge");
        collector->onCollectionReady();
        return {std::move(collector), std::move(gauge)};
    }
};

// ---------------------------------------------------------------------------
// 1. A hook that is still alive runs on a collection pass.
//    This is the registration path: makeHook() puts the hook on the
//    collector's list, and an observable gauge callback invokes it. Without
//    this, a hook that is never registered is indistinguishable from one that
//    is registered and skipped.
// ---------------------------------------------------------------------------
TEST_F(OTelCollectorHooks, live_hook_runs_once_per_collection)
{
    auto [collector, gauge] = makeArmedCollector();

    std::size_t calls = 0;
    auto const hook = collector->makeHook([&calls] { ++calls; });

    reader_->collectOnce();

    EXPECT_EQ(calls, 1u);
}

// ---------------------------------------------------------------------------
// 2. A hook destroyed before the collection pass is skipped, not called.
//    The collector holds weak references, so the destroyed hook's entry locks
//    to null. Asserting zero (not "did not crash") is what makes this a real
//    check: a stale entry that was still followed would run the handler and
//    increment the counter through freed memory.
// ---------------------------------------------------------------------------
TEST_F(OTelCollectorHooks, destroyed_hook_is_skipped)
{
    auto [collector, gauge] = makeArmedCollector();

    std::size_t calls = 0;
    {
        auto const hook = collector->makeHook([&calls] { ++calls; });
    }

    reader_->collectOnce();

    EXPECT_EQ(calls, 0u);
}

// ---------------------------------------------------------------------------
// 3. Destroying one hook leaves its siblings registered.
//    Guards the pruning step: removeExpiredHooks() erases by expiry rather
//    than by address, so an over-broad predicate would drop live hooks too and
//    silently stop their metrics updating.
// ---------------------------------------------------------------------------
TEST_F(OTelCollectorHooks, destroying_one_hook_keeps_the_others)
{
    auto [collector, gauge] = makeArmedCollector();

    std::size_t kept = 0;
    std::size_t dropped = 0;
    auto const keptHook = collector->makeHook([&kept] { ++kept; });
    {
        auto const droppedHook = collector->makeHook([&dropped] { ++dropped; });
    }

    reader_->collectOnce();

    EXPECT_EQ(kept, 1u);
    EXPECT_EQ(dropped, 0u);
}

namespace {

/**
 * Check that a collection pass exported exactly one counter point.
 *
 * The SDK makes one stream per distinct instrument, and one point per
 * attribute set. One stream holding one point therefore means the zero
 * recorded at construction and every later increment land on one series.
 *
 * @param streams   Streams one pass exported under the tested name.
 * @param expected  Sum the point must hold.
 * @return Success, or a failure naming the first check that does not hold.
 */
::testing::AssertionResult
isOneCounterPoint(std::vector<metrics_sdk::MetricData> const& streams, std::int64_t expected)
{
    if (streams.size() != 1)
    {
        return ::testing::AssertionFailure() << "expected 1 stream, got " << streams.size();
    }
    auto const& points = streams.front().point_data_attr_;
    if (points.size() != 1)
    {
        return ::testing::AssertionFailure() << "expected 1 point, got " << points.size();
    }
    auto const& point = points.front();
    if (!point.attributes.empty())
    {
        return ::testing::AssertionFailure()
            << "expected no attributes, got " << point.attributes.size();
    }
    auto const* sum = opentelemetry::nostd::get_if<metrics_sdk::SumPointData>(&point.point_data);
    if (sum == nullptr)
    {
        return ::testing::AssertionFailure() << "expected a sum point";
    }
    if (!sum->is_monotonic_)
    {
        return ::testing::AssertionFailure() << "expected a monotonic sum";
    }
    auto const* value = opentelemetry::nostd::get_if<std::int64_t>(&sum->value_);
    if (value == nullptr)
    {
        return ::testing::AssertionFailure() << "expected an integer sum";
    }
    if (*value != expected)
    {
        return ::testing::AssertionFailure() << "expected " << expected << ", got " << *value;
    }
    return ::testing::AssertionSuccess();
}

}  // namespace

/**
 * Fixture for the counter and meter tests, which read back exported points.
 *
 * @code
 *   OTelCollectorCounters (derived from OTelCollectorTest)
 *          |
 *          |  collectStreams()
 *          v
 *   reader_->Collect() --> streams named kName --> isOneCounterPoint()
 * @endcode
 *
 * @code
 * // Primary use: the point exists at 0 before any increment.
 * auto const collector = makeCollector();
 * auto const counter = collector->makeCounter(std::string{kName});
 * EXPECT_TRUE(isOneCounterPoint(collectStreams(), 0));
 * counter.increment(1);
 * EXPECT_TRUE(isOneCounterPoint(collectStreams(), 1));
 *
 * // Edge case: an instrument under another name is left out.
 * auto const other = collector->makeCounter("other_probe");
 * other.increment(1);
 * EXPECT_TRUE(isOneCounterPoint(collectStreams(), 1));
 * @endcode
 *
 * @note collectStreams() matches on name alone, so each test makes one
 * instrument named kName. ManualMetricReader asks for cumulative temporality,
 * so each pass reports the running total, not the change since the last pass.
 * @note Not thread-safe, as for OTelCollectorTest. collectStreams() runs its
 * pass on the calling thread.
 */
class OTelCollectorCounters : public OTelCollectorTest
{
protected:
    /**
     * Name the tests create and look up. Lowercase with underscores, so
     * formatName() leaves it unchanged.
     */
    static constexpr std::string_view kName = "first_event_probe";

    /**
     * @brief Run one collection pass and keep the streams named kName.
     * @return Copies of those streams, in the order the pass returned them.
     */
    [[nodiscard]] std::vector<metrics_sdk::MetricData>
    collectStreams() const
    {
        std::vector<metrics_sdk::MetricData> streams;
        bool const collected =
            reader_->Collect([&streams](metrics_sdk::ResourceMetrics const& data) {
                for (auto const& scope : data.scope_metric_data_)
                {
                    std::ranges::copy_if(
                        scope.metric_data_,
                        std::back_inserter(streams),
                        [](metrics_sdk::MetricData const& metric) {
                            return metric.instrument_descriptor.name_ == kName;
                        });
                }
                return true;
            });
        EXPECT_TRUE(collected);
        return streams;
    }
};

// ---------------------------------------------------------------------------
// 4. A counter exports 0 before its first increment, then 1 on the same
//    series.
//    A series born by its first event hides that event from rate() and
//    increase(), so the constructor records 0. Without that zero, the first
//    pass finds no stream at all.
// ---------------------------------------------------------------------------
TEST_F(OTelCollectorCounters, counter_exports_zero_until_first_increment)
{
    auto const collector = makeCollector();
    auto const counter = collector->makeCounter(std::string{kName});

    EXPECT_TRUE(isOneCounterPoint(collectStreams(), 0));

    counter.increment(1);

    EXPECT_TRUE(isOneCounterPoint(collectStreams(), 1));
}

// ---------------------------------------------------------------------------
// 5. The same for a meter, which creates its own counter instrument.
// ---------------------------------------------------------------------------
TEST_F(OTelCollectorCounters, meter_exports_zero_until_first_increment)
{
    auto const collector = makeCollector();
    auto const meter = collector->makeMeter(std::string{kName});

    EXPECT_TRUE(isOneCounterPoint(collectStreams(), 0));

    meter.increment(1);

    EXPECT_TRUE(isOneCounterPoint(collectStreams(), 1));
}

}  // namespace beast::insight

#endif  // XRPL_ENABLE_TELEMETRY
