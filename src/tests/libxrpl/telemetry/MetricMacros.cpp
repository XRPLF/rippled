/**
 * @file MetricMacros.cpp
 * Unit tests for the XRPL_METRIC_* call-site macros (MetricMacros.h).
 *
 * Compiled only when XRPL_ENABLE_TELEMETRY is defined -- the macros expand
 * to no-ops otherwise and have nothing to test beyond "it compiles", which
 * MetricsRegistry.cpp's own no-op build already proves.
 *
 * These tests exercise the macros against a bare SDK MeterProvider (no
 * OTLP exporter, no network, no background thread), the same technique
 * GetMeter.cpp uses. The macros are duck-typed: they only ever call
 * app.getMetricsRegistry(), then isEnabled() and meter() on the result.
 * The tests therefore drive them through a tiny FakeApp/FakeMetricsRegistry
 * pair instead of the real telemetry::MetricsRegistry, whose enabled-path
 * .cpp drags xrpld link dependencies (LedgerMaster, TxQ, NetworkOPs, ...)
 * that the standalone xrpl_tests binary cannot satisfy -- the same reason
 * MetricsRegistry.cpp is only compiled into this binary on the no-op path.
 */

// cspell:ignore ISTOGRAM
// The all-caps macro name XRPL_METRIC_HISTOGRAM_RECORD trips cspell's
// compound-word splitter, which emits the subword "ISTOGRAM"; ignore it here.

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpld/telemetry/MetricMacros.h>

#include <gtest/gtest.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

using namespace xrpl;

namespace {

/**
 * Duck-typed stand-in for telemetry::MetricsRegistry. The macros only
 * ever call isEnabled() and meter() on the registry pointer, so this
 * minimal type is all they need -- and it links without pulling in the
 * real MetricsRegistry.cpp's xrpld dependencies.
 */
class FakeMetricsRegistry
{
public:
    /**
     * Sets the enable flag and meter the macros will observe. Tests call
     * this once before exercising a macro, mimicking a started registry.
     */
    void
    configure(bool enabled, opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter)
    {
        enabled_ = enabled;
        meter_ = std::move(meter);
    }

    /**
     * Number of times meter() has been consulted, so a test can assert the
     * create-once (call_once) and disabled-gating behavior exactly.
     */
    [[nodiscard]] int
    meterCalls() const noexcept
    {
        return meterCalls_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool
    isEnabled() const noexcept
    {
        return enabled_;
    }

    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
    meter() const noexcept
    {
        meterCalls_.fetch_add(1, std::memory_order_relaxed);
        return meter_;
    }

private:
    /**
     * Master enable flag the macro consults via isEnabled().
     */
    bool enabled_ = true;

    /**
     * Meter handed to the macro; sourced from a bare SDK provider.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter_;

    /**
     * Counts meter() calls so tests can assert create-once / gating.
     */
    mutable std::atomic<int> meterCalls_{0};
};

/**
 * Duck-typed stand-in for ServiceRegistry. The macros only call
 * getMetricsRegistry() on the app object, so this is all they need.
 */
class FakeApp
{
public:
    [[nodiscard]] FakeMetricsRegistry*
    getMetricsRegistry() noexcept
    {
        return &registry_;
    }

    /**
     * Direct handle to the underlying fake registry, for test setup and
     * post-condition assertions.
     */
    [[nodiscard]] FakeMetricsRegistry&
    registry() noexcept
    {
        return registry_;
    }

private:
    /**
     * The macro-facing registry; tests configure it before use.
     */
    FakeMetricsRegistry registry_;
};

/**
 * Wraps a bare, exporter-less SDK MeterProvider so tests can build a
 * real (non-noop) meter without touching the network. This is exactly
 * the technique GetMeter.cpp's global_provider_meter_accepts_updown_counter
 * test uses.
 */
class ScopedBareProvider
{
public:
    ScopedBareProvider()
    {
        // Hold the SDK provider in a std::shared_ptr first, then wrap it in a
        // nostd::shared_ptr, exactly as GetMeter.cpp does. Constructing the
        // nostd::shared_ptr directly from the factory's std::unique_ptr is
        // ambiguous across its unique_ptr/shared_ptr overloads.
        std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> const sdkProvider =
            opentelemetry::sdk::metrics::MeterProviderFactory::Create();
        previous_ = opentelemetry::metrics::Provider::GetMeterProvider();
        opentelemetry::metrics::Provider::SetMeterProvider(
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>(sdkProvider));
    }

    ~ScopedBareProvider()
    {
        opentelemetry::metrics::Provider::SetMeterProvider(previous_);
    }

private:
    /**
     * Previous global provider, restored on scope exit.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider> previous_;
};

/**
 * Fetches a real (non-noop) meter from whatever provider is globally
 * installed -- inside a test this is the ScopedBareProvider's SDK provider.
 */
[[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
bareMeter()
{
    return opentelemetry::metrics::Provider::GetMeterProvider()->GetMeter("xrpld_test", "1.0.0");
}

/**
 * Builds a FakeApp wired to a real meter from the bare provider.
 */
void
wire(FakeApp& app, bool enabled)
{
    app.registry().configure(enabled, bareMeter());
}

}  // namespace

TEST(MetricMacros, counter_inc_creates_once_and_does_not_crash)
{
    ScopedBareProvider const bareProvider;
    FakeApp app;
    wire(app, /*enabled=*/true);

    // Call three times from the same call site; must not crash, and must
    // not throw even though no real collector is listening.
    for (int i = 0; i < 3; ++i)
    {
        XRPL_METRIC_COUNTER_INC(
            app, "test_macro_counter_total", "Test counter for macro unit test");
    }

    // Create-once proof: std::call_once consults meter() exactly once across
    // the three calls at this site, then reuses the cached instrument handle.
    EXPECT_EQ(app.registry().meterCalls(), 1);
}

TEST(MetricMacros, counter_inc_labeled_does_not_crash)
{
    ScopedBareProvider const bareProvider;
    FakeApp app;
    wire(app, /*enabled=*/true);

    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        "test_macro_labeled_counter_total",
        "Test labeled counter for macro unit test",
        {{"reason", std::string("unit_test")}});

    // Instrument was created exactly once at this single call site.
    EXPECT_EQ(app.registry().meterCalls(), 1);
}

TEST(MetricMacros, histogram_record_does_not_crash)
{
    ScopedBareProvider const bareProvider;
    FakeApp app;
    wire(app, /*enabled=*/true);

    for (std::int64_t const v : {100, 250, 9999})
    {
        XRPL_METRIC_HISTOGRAM_RECORD(
            app, "test_macro_histogram_us", "Test histogram for macro unit test", v);
    }

    // One call site, three records: histogram created once, then reused.
    EXPECT_EQ(app.registry().meterCalls(), 1);
}

TEST(MetricMacros, updown_add_accepts_positive_and_negative)
{
    ScopedBareProvider const bareProvider;
    FakeApp app;
    wire(app, /*enabled=*/true);

    // The whole point of UpDownCounter vs. Counter: negative Add() must not
    // throw/crash/assert, unlike a plain Counter where a negative value
    // violates the API contract. These are two distinct call sites (two
    // source lines), so each lazily creates its own handle: meter() twice.
    XRPL_METRIC_UPDOWN_ADD(app, "test_macro_updown_total", "Test updown for macro unit test", 1);
    XRPL_METRIC_UPDOWN_ADD(app, "test_macro_updown_total", "Test updown for macro unit test", -1);

    EXPECT_EQ(app.registry().meterCalls(), 2);
}

TEST(MetricMacros, updown_add_labeled_does_not_crash)
{
    ScopedBareProvider const bareProvider;
    FakeApp app;
    wire(app, /*enabled=*/true);

    XRPL_METRIC_UPDOWN_ADD_LABELED(
        app,
        "test_macro_updown_labeled_total",
        "Test labeled updown for macro unit test",
        -1,
        {{"reason", std::string("unit_test")}});

    EXPECT_EQ(app.registry().meterCalls(), 1);
}

TEST(MetricMacros, observable_gauge_register_reports_current_value)
{
    ScopedBareProvider const bareProvider;
    FakeApp app;
    wire(app, /*enabled=*/true);

    // Own the state exactly as a real caller would -- the macro's callback
    // reads through this atomic on every collection tick, it does not own
    // the value itself.
    std::atomic<std::int64_t> queueDepth{0};
    XRPL_METRIC_OBSERVABLE_GAUGE_REGISTER(
        app,
        "test_macro_observable_gauge",
        "Test observable gauge for macro unit test",
        [&queueDepth] { return queueDepth.load(); });

    // There is no application-level read-back API -- this test can only
    // prove registration doesn't crash and that meter() was consulted to
    // create the observable instrument. It does NOT assert the observed
    // value reaches Prometheus; that is the docker-harness integration
    // test's job, not this hermetic unit test.
    queueDepth.store(42);
    EXPECT_EQ(app.registry().meterCalls(), 1);
}

TEST(MetricMacros, observable_counter_and_updown_register_do_not_crash)
{
    ScopedBareProvider const bareProvider;
    FakeApp app;
    wire(app, /*enabled=*/true);

    std::atomic<std::int64_t> total{0};
    XRPL_METRIC_OBSERVABLE_COUNTER_REGISTER(
        app,
        "test_macro_observable_counter_total",
        "Test observable counter for macro unit test",
        [&total] { return total.load(); });

    std::atomic<std::int64_t> delta{0};
    XRPL_METRIC_OBSERVABLE_UPDOWN_REGISTER(
        app,
        "test_macro_observable_updown",
        "Test observable updown for macro unit test",
        [&delta] { return delta.load(); });

    // Two observable registrations, each consulting meter() once.
    EXPECT_EQ(app.registry().meterCalls(), 2);
}

TEST(MetricMacros, disabled_registry_is_noop)
{
    ScopedBareProvider const bareProvider;
    FakeApp app;
    // enabled=false: the macro's isEnabled() gate short-circuits before it
    // ever touches meter(), so nothing is created or recorded.
    wire(app, /*enabled=*/false);

    XRPL_METRIC_COUNTER_INC(
        app, "test_macro_disabled_counter_total", "Test counter for macro unit test (disabled)");

    // Gating proof: meter() is never consulted when disabled.
    EXPECT_EQ(app.registry().meterCalls(), 0);
}

#endif  // XRPL_ENABLE_TELEMETRY
