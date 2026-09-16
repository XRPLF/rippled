#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/OTelCollector.h>
#include <xrpl/beast/utility/Journal.h>

#include <gtest/gtest.h>
#include <helpers/ManualMetricReader.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace beast::insight {

namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;

// The reader is not specific to this suite -- any test that needs a real SDK
// provider without a background export thread wants it -- so its one
// definition lives in the test helpers.
using xrpl::test::ManualMetricReader;

/**
 * Installs a real SDK MeterProvider so observable gauges actually fire.
 *
 * OTelCollector takes its Meter from the global provider. Under the default
 * noop provider an observable gauge's callback is never invoked, so a hook
 * test would pass whatever the collector did. The fixture swaps in an SDK
 * provider with a ManualMetricReader and restores the previous global provider
 * afterwards, so it leaks no state into other telemetry tests in this binary.
 */
class OTelCollectorHooks : public ::testing::Test
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
     * @brief Build a collector, plus the armed gauge that drives its hooks.
     *
     * A collection pass only reaches the hooks through an observable gauge's
     * callback, and a gauge is armed by onCollectionReady(), so every test
     * needs both. The gauge is returned because dropping it would unregister
     * the callback.
     *
     * Each test builds its own collector: the hook debounce is keyed to the
     * time of the last invocation, which starts unset, so the first collection
     * on a fresh collector always runs the hooks.
     */
    static std::pair<Collector::ptr, Gauge>
    makeArmedCollector()
    {
        auto collector = OTelCollector::New(
            "http://127.0.0.1:4318/v1/metrics",
            "",
            "test-instance",
            "xrpld",
            "test",
            Journal(Journal::getNullSink()));
        auto gauge = collector->makeGauge("hook_test_gauge");
        collector->onCollectionReady();
        return {std::move(collector), std::move(gauge)};
    }

    opentelemetry::nostd::shared_ptr<metrics_api::MeterProvider> previous_;
    std::shared_ptr<ManualMetricReader> reader_;
    std::shared_ptr<metrics_sdk::MeterProvider> provider_;
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

}  // namespace beast::insight

#endif  // XRPL_ENABLE_TELEMETRY
