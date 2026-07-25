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
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/variant.h>
#include <opentelemetry/sdk/metrics/data/metric_data.h>
#include <opentelemetry/sdk/metrics/data/point_data.h>
#include <opentelemetry/sdk/metrics/export/metric_producer.h>
#include <opentelemetry/sdk/metrics/instruments.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/metric_reader.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
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

namespace otel_sdk = opentelemetry::sdk::metrics;

/**
 * One collected time series set for a single metric name: the exact points the
 * SDK produced, keyed by their full attribute (label) set.
 *
 * `PointAttributes` is an ordered map of label name -> owned value, so two
 * different label sets are two different keys -- which is precisely what the
 * "distinct outcomes must not collapse into one series" assertions check.
 */
using PointsByAttrs = std::map<otel_sdk::PointAttributes, otel_sdk::PointType>;

/**
 * Everything one Collect() pulled out of the SDK, keyed by metric name.
 * A metric name that was never recorded is simply absent from the map, which
 * is how the disabled/no-op tests prove nothing was emitted.
 */
using CollectedMetrics = std::map<std::string, PointsByAttrs>;

/**
 * A MetricReader that collects synchronously, on demand, in-process.
 *
 * The OTel SDK only hands aggregated points to a MetricReader, and the only
 * pull entry point is the base class's `Collect(callback)`. This subclass adds
 * nothing but a convenience wrapper that flattens one collection into a
 * name -> (labels -> point) map a test can assert exact values against.
 *
 * Why not the shipped InMemoryMetricExporter? Its symbols live in a separate
 * Conan archive (libopentelemetry_exporter_in_memory_metric.a) which is NOT on
 * the xrpl_tests link line -- only libopentelemetry_exporter_in_memory.a (the
 * SPAN exporter, used by SpanGuardScope.cpp) is. Subclassing MetricReader needs
 * only libopentelemetry_metrics.a, which is already linked via the umbrella
 * target, so this keeps the tests hermetic with no build-system change.
 *
 * Inheritance:
 *
 *     +--------------+
 *     | MetricReader |  (SDK abstract pull interface)
 *     +------+-------+
 *            |
 *     +------+-----------------+
 *     | CollectOnDemandReader  |  synchronous, in-process collection
 *     +------------------------+
 *
 * Example usage -- assert an exact counter value:
 * @code
 * CollectingProvider provider;
 * FakeApp app;
 * wire(app, true, provider.meter());
 * XRPL_METRIC_COUNTER_INC(app, "my_total", "desc");
 * auto data = provider.collect();
 * EXPECT_EQ(counterValue(data, "my_total", {}), 1);
 * @endcode
 *
 * Example usage -- edge case: prove a metric was NOT emitted at all:
 * @code
 * wire(app, false, provider.meter());       // registry disabled
 * XRPL_METRIC_COUNTER_INC(app, "my_total", "desc");
 * EXPECT_EQ(provider.collect().count("my_total"), 0u);
 * @endcode
 *
 * @note Reports kCumulative temporality, so counter totals accumulate across
 * repeated collect() calls rather than resetting -- the assertions below
 * therefore collect exactly once per test unless stated otherwise.
 * @note Test-only and not thread-safe by itself: collect() must not run
 * concurrently with the macro calls it measures. Every test here is
 * single-threaded, which satisfies that.
 */
class CollectOnDemandReader final : public otel_sdk::MetricReader
{
public:
    /**
     * Pull one collection and flatten it to metric name -> labels -> point.
     * @return The points produced by this collection. Metrics that were never
     * recorded are absent from the returned map.
     */
    [[nodiscard]] CollectedMetrics
    collect()
    {
        CollectedMetrics out;
        Collect([&out](otel_sdk::ResourceMetrics& resourceMetrics) {
            for (auto const& scope : resourceMetrics.scope_metric_data_)
            {
                for (auto const& metric : scope.metric_data_)
                {
                    for (auto const& point : metric.point_data_attr_)
                        out[metric.instrument_descriptor.name_][point.attributes] =
                            point.point_data;
                }
            }
            return true;
        });
        return out;
    }

    /**
     * Cumulative so counter totals are absolute, not per-interval deltas.
     */
    [[nodiscard]] otel_sdk::AggregationTemporality
    GetAggregationTemporality(otel_sdk::InstrumentType) const noexcept override
    {
        return otel_sdk::AggregationTemporality::kCumulative;
    }

private:
    /**
     * Nothing is buffered outside the SDK, so a flush always succeeds.
     */
    bool
    OnForceFlush(std::chrono::microseconds) noexcept override
    {
        return true;
    }

    /**
     * No exporter thread or socket to tear down.
     */
    bool
    OnShutDown(std::chrono::microseconds) noexcept override
    {
        return true;
    }
};

/**
 * Installs a bare SDK MeterProvider that has a CollectOnDemandReader attached,
 * and restores the previous global provider on scope exit.
 *
 * This is ScopedBareProvider plus a reader: the plain ScopedBareProvider has no
 * reader, so the SDK creates no storage and every recorded value is discarded.
 * Attaching a reader is what makes the recorded values observable, which is the
 * whole point of the value-asserting tests.
 *
 * @note The reader must be attached BEFORE any instrument is created, because
 * Meter::RegisterSyncMetricStorage() binds storage to the collectors that exist
 * at instrument-creation time. The constructor guarantees that ordering.
 */
class CollectingProvider
{
public:
    CollectingProvider()
    {
        reader_ = std::make_shared<CollectOnDemandReader>();
        // Attach the reader first, then publish the provider globally, so any
        // instrument created afterwards gets storage wired to this reader.
        sdkProvider_ = opentelemetry::sdk::metrics::MeterProviderFactory::Create();
        sdkProvider_->AddMetricReader(reader_);
        previous_ = opentelemetry::metrics::Provider::GetMeterProvider();
        opentelemetry::metrics::Provider::SetMeterProvider(
            opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider>(sdkProvider_));
    }

    ~CollectingProvider()
    {
        opentelemetry::metrics::Provider::SetMeterProvider(previous_);
    }

    CollectingProvider(CollectingProvider const&) = delete;
    CollectingProvider&
    operator=(CollectingProvider const&) = delete;

    /**
     * @return A real meter whose instruments feed this provider's reader.
     */
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
    meter() const
    {
        return sdkProvider_->GetMeter("xrpld_test", "1.0.0");
    }

    /**
     * @return The points from one synchronous collection.
     */
    [[nodiscard]] CollectedMetrics
    collect() const
    {
        return reader_->collect();
    }

private:
    /**
     * The on-demand reader; owned jointly with the provider's collector.
     */
    std::shared_ptr<CollectOnDemandReader> reader_;

    /**
     * SDK provider that owns the metric storage the reader collects from.
     */
    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> sdkProvider_;

    /**
     * Previous global provider, restored on scope exit.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::MeterProvider> previous_;
};

/**
 * Builds the attribute key for a single-label series, e.g. {"outcome","empty"}.
 * @return A PointAttributes usable as a lookup key into PointsByAttrs.
 */
[[nodiscard]] otel_sdk::PointAttributes
attrs(std::string const& key, std::string const& value)
{
    otel_sdk::PointAttributes out;
    out.SetAttribute(key, value);
    return out;
}

/**
 * Builds the attribute key for a two-label series, e.g. site + outcome.
 * @return A PointAttributes carrying exactly the two given labels.
 */
[[nodiscard]] otel_sdk::PointAttributes
attrs(
    std::string const& key1,
    std::string const& value1,
    std::string const& key2,
    std::string const& value2)
{
    otel_sdk::PointAttributes out;
    out.SetAttribute(key1, value1);
    out.SetAttribute(key2, value2);
    return out;
}

/**
 * Reads the exact accumulated value of one counter time series.
 *
 * A uint64 Counter aggregates into a SumPointData holding an int64_t, so the
 * value is unwrapped through both variants. Fails the calling test (via
 * std::map::at) if the metric or the exact label set is missing -- absence is
 * itself a defect for these assertions.
 *
 * @return The counter total for that exact label set.
 */
[[nodiscard]] std::int64_t
counterValue(
    CollectedMetrics const& data,
    std::string const& metric,
    otel_sdk::PointAttributes const& labels)
{
    auto const& point = data.at(metric).at(labels);
    auto const& sum = opentelemetry::nostd::get<otel_sdk::SumPointData>(point);
    return opentelemetry::nostd::get<std::int64_t>(sum.value_);
}

/**
 * Reads the exact last observed value of one Int64ObservableGauge series.
 * An observable gauge aggregates as last-value, holding an int64_t.
 * @return The observed value for that exact label set (may be negative).
 */
[[nodiscard]] std::int64_t
gaugeValue(
    CollectedMetrics const& data,
    std::string const& metric,
    otel_sdk::PointAttributes const& labels)
{
    auto const& point = data.at(metric).at(labels);
    auto const& last = opentelemetry::nostd::get<otel_sdk::LastValuePointData>(point);
    return opentelemetry::nostd::get<std::int64_t>(last.value_);
}

/**
 * Reads the sample count and sum of an unlabeled double Histogram.
 *
 * The macros record histograms with no labels, so the metric has exactly one
 * series whose key is the empty attribute set.
 *
 * @return {count, sum} for the single unlabeled series.
 */
[[nodiscard]] std::pair<std::uint64_t, double>
histogramCountAndSum(CollectedMetrics const& data, std::string const& metric)
{
    auto const& point = data.at(metric).at(otel_sdk::PointAttributes{});
    auto const& hist = opentelemetry::nostd::get<otel_sdk::HistogramPointData>(point);
    return {hist.count_, opentelemetry::nostd::get<double>(hist.sum_)};
}

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

/**
 * Builds a FakeApp wired to an explicitly supplied meter -- used with a
 * CollectingProvider so the recorded values are collectable.
 *
 * @param app      The duck-typed app the macros will be driven through.
 * @param enabled  What the macros' isEnabled() gate will observe.
 * @param meter    The meter the macros will create their instruments on.
 */
void
wire(
    FakeApp& app,
    bool enabled,
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter)
{
    app.registry().configure(enabled, std::move(meter));
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

    // Own the state exactly as a real caller would (Use Case 5 in the
    // design doc) -- the macro's callback reads through this atomic on
    // every collection tick, it does not own the value itself.
    std::atomic<std::int64_t> queueDepth{0};
    XRPL_METRIC_OBSERVABLE_GAUGE_REGISTER(
        app,
        "test_macro_observable_gauge",
        "Test observable gauge for macro unit test",
        [&queueDepth] { return queueDepth.load(); });

    // There is no application-level read-back API (Use Case 4) -- this
    // test can only prove registration doesn't crash and that meter() was
    // consulted to create the observable instrument. It does NOT assert the
    // observed value reaches Prometheus; that is Task 3b's docker-harness
    // job, not this hermetic unit test.
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

// -----------------------------------------------------------------
// Fresh-node sync diagnostics metrics.
//
// The tests below assert the EXACT recorded values and the EXACT label shape of
// the eight sync-diagnostics metrics, using a real SDK MeterProvider with a
// CollectOnDemandReader attached. They drive the same macros production uses,
// through the existing FakeApp, so what is proved is the real emit path:
//   dns_resolve_total{outcome}                 OverlayImpl::reportDnsResolve
//   dns_resolve_latency_ms                     (same)
//   overlay_connect_total{outcome}             ConnectAttempt::reportOutcome
//   overlay_dial_latency_ms                    (same)
//   handshake_negotiation_fail_total{reason}   Handshake throwNegotiationFailure
//   unl_fetch_total{site,outcome}              ValidatorSite::reportFetchOutcome
//   unl_quorum{metric}                         MetricsRegistry::registerUnlQuorumGauge
//   clock_close_offset_seconds{metric}         MetricsRegistry::registerClockSkewGauge
//
// The two observable gauges are registered directly on the SDK meter, mirroring
// the production callback shape, because the real MetricsRegistry's enabled path
// cannot be linked into this standalone binary (see the file header).
// -----------------------------------------------------------------

// dns_resolve_total must tally each outcome separately and dns_resolve_latency_ms
// must record every sample: one "resolved", two "empty", and two known latency
// samples whose count and sum are checked exactly.
TEST(MetricMacros, dns_resolve_records_exact_counts_and_latency)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // outcome=resolved once; outcome=empty twice. Both go through the single
    // production call site, so the label value is the only difference -- exactly
    // how OverlayImpl::reportDnsResolve() emits it.
    for (bool const resolved : {true, false, false})
    {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            "dns_resolve_total",
            "Peer hostname resolutions, by outcome",
            {{"outcome", std::string(resolved ? "resolved" : "empty")}});
    }

    // Two latency samples with known values: 1.5 ms + 2.5 ms = 4.0 ms.
    for (double const ms : {1.5, 2.5})
    {
        XRPL_METRIC_HISTOGRAM_RECORD(
            app,
            "dns_resolve_latency_ms",
            "Time taken to resolve a configured peer hostname, in milliseconds",
            ms);
    }

    auto const data = provider.collect();

    // Exactly two series, one per outcome value -- the labeled counter must not
    // collapse "resolved" and "empty" into a single series.
    ASSERT_EQ(data.at("dns_resolve_total").size(), 2u);
    EXPECT_EQ(counterValue(data, "dns_resolve_total", attrs("outcome", "resolved")), 1);
    EXPECT_EQ(counterValue(data, "dns_resolve_total", attrs("outcome", "empty")), 2);

    // The label key is exactly "outcome" and it is the ONLY label present.
    auto const& firstKey = data.at("dns_resolve_total").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "outcome");

    // Histogram: exactly the two samples recorded, summing to exactly 4.0 ms.
    auto const [count, sum] = histogramCountAndSum(data, "dns_resolve_latency_ms");
    EXPECT_EQ(count, 2u);
    EXPECT_DOUBLE_EQ(sum, 4.0);

    // Unlabeled histogram: exactly one series, with an empty label set.
    ASSERT_EQ(data.at("dns_resolve_latency_ms").size(), 1u);
    EXPECT_TRUE(data.at("dns_resolve_latency_ms").begin()->first.empty());
}

// overlay_connect_total must keep every terminal outcome in its own series (a
// "timeout" must never be folded into "tcp_fail"), and overlay_dial_latency_ms
// must record each dial's duration.
TEST(MetricMacros, overlay_connect_records_exact_counts_per_outcome)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // Three distinct outcomes with distinct multiplicities: connected x1,
    // tcp_fail x3, timeout x2.
    auto const bump = [&app](char const* outcome) {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            "overlay_connect_total",
            "Outbound peer connection attempts, by terminal outcome",
            {{"outcome", std::string(outcome)}});
    };
    bump("connected");
    bump("tcp_fail");
    bump("tcp_fail");
    bump("tcp_fail");
    bump("timeout");
    bump("timeout");

    // Dial latencies: 10.0 + 20.0 + 30.5 = 60.5 ms across 3 samples.
    for (double const ms : {10.0, 20.0, 30.5})
    {
        XRPL_METRIC_HISTOGRAM_RECORD(
            app,
            "overlay_dial_latency_ms",
            "Time from starting an outbound peer dial to its terminal outcome, in milliseconds",
            ms);
    }

    auto const data = provider.collect();

    // Three distinct outcomes stay three distinct series.
    ASSERT_EQ(data.at("overlay_connect_total").size(), 3u);
    EXPECT_EQ(counterValue(data, "overlay_connect_total", attrs("outcome", "connected")), 1);
    EXPECT_EQ(counterValue(data, "overlay_connect_total", attrs("outcome", "tcp_fail")), 3);
    EXPECT_EQ(counterValue(data, "overlay_connect_total", attrs("outcome", "timeout")), 2);

    // NEGATIVE: an outcome that was never emitted has no series at all, so the
    // counts above are not an artifact of some catch-all series.
    EXPECT_EQ(data.at("overlay_connect_total").count(attrs("outcome", "tls_fail")), 0u);

    auto const [count, sum] = histogramCountAndSum(data, "overlay_dial_latency_ms");
    EXPECT_EQ(count, 3u);
    EXPECT_DOUBLE_EQ(sum, 60.5);
}

// handshake_negotiation_fail_total must keep each rejection reason distinct;
// throwNegotiationFailure() routes every branch through one call site, so the
// `reason` label is the only thing separating them.
TEST(MetricMacros, handshake_negotiation_fail_keeps_reasons_distinct)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // wrong_network twice, self_connection once, clock_skew once.
    for (char const* reason : {"wrong_network", "wrong_network", "self_connection", "clock_skew"})
    {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            "handshake_negotiation_fail_total",
            "Peer handshake negotiations rejected, by reason",
            {{"reason", std::string(reason)}});
    }

    auto const data = provider.collect();

    ASSERT_EQ(data.at("handshake_negotiation_fail_total").size(), 3u);
    EXPECT_EQ(
        counterValue(data, "handshake_negotiation_fail_total", attrs("reason", "wrong_network")),
        2);
    EXPECT_EQ(
        counterValue(data, "handshake_negotiation_fail_total", attrs("reason", "self_connection")),
        1);
    EXPECT_EQ(
        counterValue(data, "handshake_negotiation_fail_total", attrs("reason", "clock_skew")), 1);

    // The label key is exactly "reason", and nothing else rides along.
    auto const& firstKey = data.at("handshake_negotiation_fail_total").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "reason");

    // NEGATIVE: a reason from the production set that was not emitted here has
    // no series, proving reasons are not being merged.
    EXPECT_EQ(
        data.at("handshake_negotiation_fail_total").count(attrs("reason", "bad_public_key")), 0u);
}

// unl_fetch_total carries TWO labels, so the series identity is the (site,
// outcome) PAIR. This is the highest-value signal of the eight: if the pair did
// not form the key, one failing list site would be masked by a healthy one.
TEST(MetricMacros, unl_fetch_total_keys_series_on_site_and_outcome_pair)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    auto const bump = [&app](char const* site, char const* outcome) {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            "unl_fetch_total",
            "Validator list fetch attempts, by site and outcome",
            {{"site", std::string(site)}, {"outcome", std::string(outcome)}});
    };

    constexpr char const* kSiteA = "https://a.example.com/vl.json";
    constexpr char const* kSiteB = "https://b.example.com/vl.json";

    // Same outcome, two DIFFERENT sites -> must be two series.
    bump(kSiteA, "accepted");
    bump(kSiteB, "accepted");
    bump(kSiteB, "accepted");
    // Same site, a SECOND outcome -> must be its own series.
    bump(kSiteA, "fetch_error");
    bump(kSiteA, "fetch_error");
    bump(kSiteA, "fetch_error");

    auto const data = provider.collect();

    // Three distinct (site, outcome) pairs -> exactly three series.
    ASSERT_EQ(data.at("unl_fetch_total").size(), 3u);

    // Two different sites with the SAME outcome are distinct, with their own
    // exact counts -- site B's two successes do not inflate site A's one.
    EXPECT_EQ(
        counterValue(data, "unl_fetch_total", attrs("site", kSiteA, "outcome", "accepted")), 1);
    EXPECT_EQ(
        counterValue(data, "unl_fetch_total", attrs("site", kSiteB, "outcome", "accepted")), 2);

    // The SAME site with two outcomes is also distinct: A's 3 fetch_errors do
    // not merge into A's 1 accepted.
    EXPECT_EQ(
        counterValue(data, "unl_fetch_total", attrs("site", kSiteA, "outcome", "fetch_error")), 3);

    // NEGATIVE: a pair never emitted (site B erroring) has no series, so the
    // per-site failure signal is genuinely per-site.
    EXPECT_EQ(
        data.at("unl_fetch_total").count(attrs("site", kSiteB, "outcome", "fetch_error")), 0u);

    // Every series key carries exactly the two expected label names.
    for (auto const& [labels, point] : data.at("unl_fetch_total"))
    {
        ASSERT_EQ(labels.size(), 2u);
        EXPECT_EQ(labels.count("site"), 1u);
        EXPECT_EQ(labels.count("outcome"), 1u);
    }
}

// unl_quorum observes two series from ONE callback, mirroring
// MetricsRegistry::registerUnlQuorumGauge(): trusted_keys and quorum under the
// `metric` label. Registered on the SDK meter directly because that production
// method cannot be linked here (see the file header).
TEST(MetricMacros, unl_quorum_gauge_observes_exact_trusted_keys_and_quorum)
{
    CollectingProvider const provider;

    // Values the callback will report, owned by the test exactly as the real
    // registry reads them live from ValidatorList on each collection tick.
    struct Observed
    {
        std::int64_t trustedKeys;
        std::int64_t quorum;
    };
    Observed observed{5, 4};

    // Keep the instrument alive for the whole test: destroying the handle
    // deregisters the callback (ObservableInstrument's destructor calls
    // CleanupCallback), which is why the real registry holds it in a member.
    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        "unl_quorum", "Trusted UNL key count vs required quorum");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<Observed const*>(state);
            // Same Observe() form the production callback uses.
            auto observe = [&](char const* name, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{"metric", name}});
            };
            observe("trusted_keys", self->trustedKeys);
            observe("quorum", self->quorum);
        },
        &observed);

    auto const data = provider.collect();

    // Exactly two series, one per `metric` value, with the exact values.
    ASSERT_EQ(data.at("unl_quorum").size(), 2u);
    EXPECT_EQ(gaugeValue(data, "unl_quorum", attrs("metric", "trusted_keys")), 5);
    EXPECT_EQ(gaugeValue(data, "unl_quorum", attrs("metric", "quorum")), 4);

    // The label key is exactly "metric".
    auto const& firstKey = data.at("unl_quorum").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "metric");
}

// clock_close_offset_seconds must carry a NEGATIVE offset through unchanged --
// that is the real-world case (local clock ahead of the network) and the reason
// the production gauge is an Int64ObservableGauge rather than an unsigned
// counter. Mirrors MetricsRegistry::registerClockSkewGauge().
TEST(MetricMacros, clock_skew_gauge_observes_exact_negative_offset)
{
    CollectingProvider const provider;

    // -3 s: this node's clock runs 3 seconds ahead of network close time.
    std::int64_t offsetSeconds = -3;

    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        "clock_close_offset_seconds", "Network close time offset from the local clock, in seconds");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* value = static_cast<std::int64_t const*>(state);
            auto observe = [&](char const* name, std::int64_t v) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(v, {{"metric", name}});
            };
            observe("offset", *value);
        },
        &offsetSeconds);

    auto const data = provider.collect();

    // Exactly one series, and the negative value survived the round trip: not
    // clamped to 0, not reinterpreted as a large unsigned value.
    ASSERT_EQ(data.at("clock_close_offset_seconds").size(), 1u);
    EXPECT_EQ(gaugeValue(data, "clock_close_offset_seconds", attrs("metric", "offset")), -3);

    auto const& firstKey = data.at("clock_close_offset_seconds").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "metric");
}

// RUNTIME-DISABLED no-op proof: with the registry disabled, every one of the
// four sync-diagnostics counter/histogram families emits NOTHING -- no series
// exists for any of those metric names, and meter() is never consulted, so not
// even an instrument was created. This is the runtime counterpart to the
// compile-time no-op proof in MetricsRegistry.cpp.
TEST(MetricMacros, sync_diagnostics_metrics_emit_nothing_when_registry_disabled)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/false, provider.meter());

    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        "dns_resolve_total",
        "Peer hostname resolutions, by outcome",
        {{"outcome", std::string("resolved")}});
    XRPL_METRIC_HISTOGRAM_RECORD(
        app,
        "dns_resolve_latency_ms",
        "Time taken to resolve a configured peer hostname, in milliseconds",
        1.5);
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        "overlay_connect_total",
        "Outbound peer connection attempts, by terminal outcome",
        {{"outcome", std::string("connected")}});
    XRPL_METRIC_HISTOGRAM_RECORD(
        app,
        "overlay_dial_latency_ms",
        "Time from starting an outbound peer dial to its terminal outcome, in milliseconds",
        10.0);
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        "handshake_negotiation_fail_total",
        "Peer handshake negotiations rejected, by reason",
        {{"reason", std::string("wrong_network")}});
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        "unl_fetch_total",
        "Validator list fetch attempts, by site and outcome",
        {{"site", std::string("https://a.example.com/vl.json")},
         {"outcome", std::string("accepted")}});

    auto const data = provider.collect();

    // No series at all for any of the six metric names -- not a zero-valued
    // series, but total absence: the instruments were never even created.
    EXPECT_EQ(data.count("dns_resolve_total"), 0u);
    EXPECT_EQ(data.count("dns_resolve_latency_ms"), 0u);
    EXPECT_EQ(data.count("overlay_connect_total"), 0u);
    EXPECT_EQ(data.count("overlay_dial_latency_ms"), 0u);
    EXPECT_EQ(data.count("handshake_negotiation_fail_total"), 0u);
    EXPECT_EQ(data.count("unl_fetch_total"), 0u);

    // Nothing else leaked in either: the collection is completely empty.
    EXPECT_EQ(data.size(), 0u);

    // Cause, not just state: the isEnabled() gate short-circuited before the
    // macro ever asked for a meter.
    EXPECT_EQ(app.registry().meterCalls(), 0);
}

// -----------------------------------------------------------------
// Sync-state diagnostics (WP-A2).
//
// Asserts the EXACT values and label shapes of the five sync-state signals:
//   state_changes_total{from,to}    NetworkOPsImp::setMode
//   sync_state{metric}             MetricsRegistry::registerSyncStateGauge
//                                    initial_full_duration_us
//                                    network_ledger_gate
//                                    server_stall_seconds
//                                    ledgers_behind
//   server_stall_events_total      MetricsRegistry::registerStallEventsCounter
//
// The counter goes through the same macro production uses. The two observable
// instruments are registered directly on the SDK meter, mirroring the
// production callback shape, because the real MetricsRegistry's enabled path
// cannot be linked into this standalone binary (see the file header).
// -----------------------------------------------------------------

// state_changes_total is keyed on the (from, to) PAIR, so a transition edge is
// its own series. This is the whole point of the label: an unlabelled total
// cannot tell a clean tracking->connected->full climb from full->connected
// flapping, because both produce the same count.
TEST(MetricMacros, state_changes_total_keys_series_on_from_to_pair)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // Mirrors the production call site: one macro invocation, the label values
    // supplied by strOperatingMode() on the previous and new mode.
    auto const transition = [&app](char const* from, char const* to) {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            "state_changes_total",
            "Total operating mode changes",
            {{"from", std::string(from)}, {"to", std::string(to)}});
    };

    // A clean climb: disconnected -> connected -> syncing -> full, once each.
    transition("disconnected", "connected");
    transition("connected", "syncing");
    transition("syncing", "full");
    // Then flapping: full -> connected twice more, and connected -> full twice.
    transition("full", "connected");
    transition("full", "connected");
    transition("connected", "full");
    transition("connected", "full");

    auto const data = provider.collect();

    // Six distinct (from, to) pairs -> exactly six series.
    ASSERT_EQ(data.at("state_changes_total").size(), 6u);

    // The climb edges, each traversed exactly once.
    EXPECT_EQ(
        counterValue(data, "state_changes_total", attrs("from", "disconnected", "to", "connected")),
        1);
    EXPECT_EQ(
        counterValue(data, "state_changes_total", attrs("from", "connected", "to", "syncing")), 1);
    EXPECT_EQ(counterValue(data, "state_changes_total", attrs("from", "syncing", "to", "full")), 1);

    // The flap edges carry their own exact counts and do not merge into the
    // climb edges above: full->connected is 2, not folded into connected->full.
    EXPECT_EQ(
        counterValue(data, "state_changes_total", attrs("from", "full", "to", "connected")), 2);
    EXPECT_EQ(
        counterValue(data, "state_changes_total", attrs("from", "connected", "to", "full")), 2);

    // Direction matters: connected->syncing exists, syncing->connected does not,
    // proving the pair is ordered rather than an unordered edge set.
    EXPECT_EQ(
        data.at("state_changes_total").count(attrs("from", "syncing", "to", "connected")), 0u);

    // NEGATIVE: a mode pair never emitted has no series at all.
    EXPECT_EQ(data.at("state_changes_total").count(attrs("from", "tracking", "to", "full")), 0u);

    // Every series key carries exactly the two expected label names and nothing
    // else -- no stray dimension inflating the cardinality.
    for (auto const& [labels, point] : data.at("state_changes_total"))
    {
        ASSERT_EQ(labels.size(), 2u);
        EXPECT_EQ(labels.count("from"), 1u);
        EXPECT_EQ(labels.count("to"), 1u);
    }
}

// sync_state fans four independent signals out of ONE callback under the
// `metric` label, mirroring MetricsRegistry::registerSyncStateGauge(). The
// values chosen are the diagnostically interesting combination: never reached
// FULL (0 duration) while the gate is still closed, the loop is stalled, and
// the node trails the network.
TEST(MetricMacros, sync_state_gauge_observes_exact_stuck_node_values)
{
    CollectingProvider const provider;

    // The live values the callback reports, owned by the test exactly as the
    // real registry reads them from NetworkOPs/LoadManager on each tick.
    struct Observed
    {
        std::int64_t initialFullDurationUs;
        std::int64_t networkLedgerGate;
        std::int64_t serverStallSeconds;
        std::int64_t ledgersBehind;
    };
    // A node that never synced: no FULL yet, gate closed, 42 s stalled, 150
    // ledgers behind (network tip 250 vs our validated 100).
    Observed observed{0, 1, 42, 150};

    // Keep the instrument alive for the whole test: destroying the handle
    // deregisters the callback, which is why the real registry holds a member.
    auto gauge =
        provider.meter()->CreateInt64ObservableGauge("sync_state", "Sync-pipeline health signals");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<Observed const*>(state);
            // Same Observe() form the production callback uses.
            auto observe = [&](char const* name, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{"metric", name}});
            };
            observe("initial_full_duration_us", self->initialFullDurationUs);
            observe("network_ledger_gate", self->networkLedgerGate);
            observe("server_stall_seconds", self->serverStallSeconds);
            observe("ledgers_behind", self->ledgersBehind);
        },
        &observed);

    auto const data = provider.collect();

    // Exactly four series, one per `metric` value -- the four signals must not
    // collapse into a single series.
    ASSERT_EQ(data.at("sync_state").size(), 4u);

    // Zero is a REAL observed value here, not a missing series: it is the
    // "never reached FULL" signal, so the series must exist and read 0.
    ASSERT_EQ(data.at("sync_state").count(attrs("metric", "initial_full_duration_us")), 1u);
    EXPECT_EQ(gaugeValue(data, "sync_state", attrs("metric", "initial_full_duration_us")), 0);

    EXPECT_EQ(gaugeValue(data, "sync_state", attrs("metric", "network_ledger_gate")), 1);
    EXPECT_EQ(gaugeValue(data, "sync_state", attrs("metric", "server_stall_seconds")), 42);
    EXPECT_EQ(gaugeValue(data, "sync_state", attrs("metric", "ledgers_behind")), 150);

    // The label key is exactly "metric" and it is the only label present.
    auto const& firstKey = data.at("sync_state").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "metric");

    // NEGATIVE: the stall EPISODE count is deliberately NOT a sync_state
    // series -- it is a separate cumulative instrument, so querying it here
    // must find nothing.
    EXPECT_EQ(data.at("sync_state").count(attrs("metric", "server_stall_events")), 0u);

    // A healthy node reports the complementary values through the same
    // callback: synced in 12.5 s, gate open, no stall, at the tip.
    observed = Observed{12'500'000, 0, 0, 0};
    auto const healthy = provider.collect();
    EXPECT_EQ(
        gaugeValue(healthy, "sync_state", attrs("metric", "initial_full_duration_us")), 12'500'000);
    EXPECT_EQ(gaugeValue(healthy, "sync_state", attrs("metric", "network_ledger_gate")), 0);
    EXPECT_EQ(gaugeValue(healthy, "sync_state", attrs("metric", "server_stall_seconds")), 0);
    EXPECT_EQ(gaugeValue(healthy, "sync_state", attrs("metric", "ledgers_behind")), 0);
}

// server_stall_events_total is a cumulative ObservableCounter, not a gauge
// series. It must aggregate as a Sum (so rate() is meaningful) and be
// unlabelled, mirroring MetricsRegistry::registerStallEventsCounter().
TEST(MetricMacros, stall_events_counter_observes_exact_cumulative_count)
{
    CollectingProvider const provider;

    // Three stall episodes reported so far by the load-monitor thread.
    std::int64_t stallEpisodes = 3;

    auto counter = provider.meter()->CreateInt64ObservableCounter(
        "server_stall_events_total", "Total server main-loop stall episodes");
    counter->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* value = static_cast<std::int64_t const*>(state);
            // Production observes this with NO labels.
            opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                ->Observe(*value);
        },
        &stallEpisodes);

    auto const data = provider.collect();

    // Exactly one unlabelled series, read through counterValue() -- which
    // unwraps a SumPointData, so this also proves the instrument aggregates as
    // a counter and not as a last-value gauge.
    ASSERT_EQ(data.at("server_stall_events_total").size(), 1u);
    EXPECT_TRUE(data.at("server_stall_events_total").begin()->first.empty());
    EXPECT_EQ(counterValue(data, "server_stall_events_total", otel_sdk::PointAttributes{}), 3);

    // Monotonic: a later collection sees the higher total, not a delta.
    stallEpisodes = 5;
    EXPECT_EQ(
        counterValue(provider.collect(), "server_stall_events_total", otel_sdk::PointAttributes{}),
        5);
}

// RUNTIME-DISABLED no-op proof for the counter half of WP-A2: with the registry
// disabled, the setMode call site emits NOTHING -- no series for
// state_changes_total, and meter() is never consulted, so not even an
// instrument was created.
TEST(MetricMacros, state_changes_total_emits_nothing_when_registry_disabled)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/false, provider.meter());

    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        "state_changes_total",
        "Total operating mode changes",
        {{"from", std::string("connected")}, {"to", std::string("full")}});

    auto const data = provider.collect();

    // Total absence, not a zero-valued series.
    EXPECT_EQ(data.count("state_changes_total"), 0u);
    EXPECT_EQ(data.size(), 0u);

    // Cause, not just state: the isEnabled() gate short-circuited before the
    // macro asked for a meter.
    EXPECT_EQ(app.registry().meterCalls(), 0);
}

// -----------------------------------------------------------------
// Acquire + SHAMap sync diagnostics (WP-A3).
//
// Asserts the EXACT values and label shapes of the five acquire signals:
//   sync_acquire_source_total{source}      InboundLedger::init
//   sync_acquire_no_progress_total         InboundLedger::onTimer
//   sync_addnode_total{outcome}            InboundLedger::recordBatchOutcome
//   sync_acquire{metric}                   MetricsRegistry::registerSyncAcquireGauge
//                                            missing_state_nodes_max
//                                            missing_tx_nodes_max
//                                            received_data_depth
//                                            in_flight
//   shamap_cache_hit_rate{metric}          MetricsRegistry::registerCacheHitRateDetailGauge
//
// The counters go through the same macros production uses. The two observable
// instruments are registered directly on the SDK meter, mirroring the production
// callback shape, because the real MetricsRegistry's enabled path cannot be
// linked into this standalone binary (see the file header).
// -----------------------------------------------------------------

// sync_acquire_source_total splits acquires by whether the local node store
// already held the ledger. This is the disk-bound vs peer-bound distinction, so
// the two sources must never collapse into one series.
TEST(MetricMacros, acquire_source_splits_local_and_network)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // Mirrors the production call site in InboundLedger::init(): one macro
    // invocation whose label is derived from complete_ after the first tryDB().
    auto const acquire = [&app](bool localComplete) {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            "sync_acquire_source_total",
            "Ledger acquires by where the data came from",
            {{"source", std::string(localComplete ? "local" : "network")}});
    };
    // One satisfied locally, two needing the network.
    acquire(true);
    acquire(false);
    acquire(false);

    auto const data = provider.collect();

    ASSERT_EQ(data.at("sync_acquire_source_total").size(), 2u);
    EXPECT_EQ(counterValue(data, "sync_acquire_source_total", attrs("source", "local")), 1);
    EXPECT_EQ(counterValue(data, "sync_acquire_source_total", attrs("source", "network")), 2);

    // The label key is exactly "source" and nothing rides along with it.
    auto const& firstKey = data.at("sync_acquire_source_total").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "source");

    // NEGATIVE: a source value that was never emitted has no series, so the
    // counts above are not an artifact of a catch-all series.
    EXPECT_EQ(data.at("sync_acquire_source_total").count(attrs("source", "fetch_pack")), 0u);
}

// sync_acquire_no_progress_total counts ONLY timeouts where no node arrived.
// InboundLedger::onTimer reaches the macro exclusively on its !wasProgress
// branch, so a tick that made progress must leave the total unchanged -- that
// is the whole difference between "slow" and "stuck".
TEST(MetricMacros, acquire_no_progress_counts_only_stalled_timeouts)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // Stands in for onTimer(): the guard lives at the call site in production,
    // so the harness reproduces the guard rather than the macro alone.
    auto const onTimer = [&app](bool wasProgress) {
        if (!wasProgress)
        {
            XRPL_METRIC_COUNTER_INC(
                app,
                "sync_acquire_no_progress_total",
                "Ledger-acquire timeouts where no new node arrived");
        }
    };

    onTimer(/*wasProgress=*/false);
    onTimer(/*wasProgress=*/false);

    // Exactly two stalled timeouts so far, on one unlabelled series.
    auto const stalled = provider.collect();
    ASSERT_EQ(stalled.at("sync_acquire_no_progress_total").size(), 1u);
    EXPECT_TRUE(stalled.at("sync_acquire_no_progress_total").begin()->first.empty());
    EXPECT_EQ(
        counterValue(stalled, "sync_acquire_no_progress_total", otel_sdk::PointAttributes{}), 2);

    // A tick that DID make progress must not advance the counter: still 2.
    onTimer(/*wasProgress=*/true);
    EXPECT_EQ(
        counterValue(
            provider.collect(), "sync_acquire_no_progress_total", otel_sdk::PointAttributes{}),
        2);

    // A further stalled tick does advance it, proving the counter is live and
    // the unchanged reading above was the guard working, not a dead instrument.
    onTimer(/*wasProgress=*/false);
    EXPECT_EQ(
        counterValue(
            provider.collect(), "sync_acquire_no_progress_total", otel_sdk::PointAttributes{}),
        3);
}

// sync_addnode_total separates useful progress from wasted work. All three
// outcomes come from ONE aggregated batch tally, added after the per-node loop
// has finished, so each outcome must land on its own series with its exact count.
TEST(MetricMacros, addnode_outcomes_record_exact_batch_tallies)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // Mirrors InboundLedger::recordBatchOutcome(): three _ADD calls per batch,
    // each skipped when its tally is zero (a zero Add would create a series that
    // says "we saw invalid nodes", which would be false).
    auto const emitBatch = [&app](int good, int duplicate, int invalid) {
        auto const emit = [&app](char const* outcome, int count) {
            if (count <= 0)
                return;
            XRPL_METRIC_COUNTER_ADD_LABELED(
                app,
                "sync_addnode_total",
                "SHAMap nodes received during ledger acquire, by outcome",
                static_cast<std::uint64_t>(count),
                {{"outcome", std::string(outcome)}});
        };
        emit("good", good);
        emit("duplicate", duplicate);
        emit("invalid", invalid);
    };

    // One batch: 5 good, 2 duplicate, 1 invalid.
    emitBatch(5, 2, 1);

    auto const oneBatch = provider.collect();
    ASSERT_EQ(oneBatch.at("sync_addnode_total").size(), 3u);
    EXPECT_EQ(counterValue(oneBatch, "sync_addnode_total", attrs("outcome", "good")), 5);
    EXPECT_EQ(counterValue(oneBatch, "sync_addnode_total", attrs("outcome", "duplicate")), 2);
    EXPECT_EQ(counterValue(oneBatch, "sync_addnode_total", attrs("outcome", "invalid")), 1);

    // Every series key carries exactly the one expected label name.
    for (auto const& [labels, point] : oneBatch.at("sync_addnode_total"))
    {
        ASSERT_EQ(labels.size(), 1u);
        EXPECT_EQ(labels.count("outcome"), 1u);
    }

    // A second batch accumulates per outcome rather than replacing: 3 more good
    // and 4 more duplicates, no invalid this time.
    emitBatch(3, 4, 0);
    auto const twoBatches = provider.collect();
    EXPECT_EQ(counterValue(twoBatches, "sync_addnode_total", attrs("outcome", "good")), 8);
    EXPECT_EQ(counterValue(twoBatches, "sync_addnode_total", attrs("outcome", "duplicate")), 6);
    // The zero-tally outcome did NOT advance: still exactly 1 from the first
    // batch, so an all-good batch cannot inflate the invalid series.
    EXPECT_EQ(counterValue(twoBatches, "sync_addnode_total", attrs("outcome", "invalid")), 1);

    // Still exactly three series after two batches: the zero-tally guard means a
    // batch never invents a series for an outcome it did not observe.
    EXPECT_EQ(twoBatches.at("sync_addnode_total").size(), 3u);

    // NEGATIVE: an outcome value outside the production set has no series, so
    // the three counts above are not an artifact of a catch-all series.
    EXPECT_EQ(twoBatches.at("sync_addnode_total").count(attrs("outcome", "stale")), 0u);
}

// sync_acquire fans four values out of ONE aggregated snapshot, mirroring
// MetricsRegistry::registerSyncAcquireGauge(). The values chosen are the
// headline stuck-sync reading: two acquires in flight, the state tree still
// missing nodes, a backed-up stash.
TEST(MetricMacros, sync_acquire_gauge_observes_exact_stuck_acquire_values)
{
    CollectingProvider const provider;

    // The snapshot the callback reports, owned by the test exactly as the real
    // registry reads it from InboundLedgers on each collection tick.
    struct Observed
    {
        std::int64_t maxMissingStateNodes;
        std::int64_t maxMissingTxNodes;
        std::int64_t receivedDataDepth;
        std::int64_t inFlight;
    };
    // A stuck acquire: 256 state nodes still outstanding (the sweep cap), the tx
    // tree already done, 4 packets stashed, 2 acquires running.
    Observed observed{256, 0, 4, 2};

    // Keep the instrument alive for the whole test: destroying the handle
    // deregisters the callback, which is why the real registry holds a member.
    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        "sync_acquire", "Aggregate ledger-acquire progress across in-flight acquires");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<Observed const*>(state);
            // Same Observe() form the production callback uses.
            auto observe = [&](char const* name, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{"metric", name}});
            };
            observe("missing_state_nodes_max", self->maxMissingStateNodes);
            observe("missing_tx_nodes_max", self->maxMissingTxNodes);
            observe("received_data_depth", self->receivedDataDepth);
            observe("in_flight", self->inFlight);
        },
        &observed);

    auto const stuck = provider.collect();

    // Exactly four series, one per `metric` value.
    ASSERT_EQ(stuck.at("sync_acquire").size(), 4u);
    EXPECT_EQ(gaugeValue(stuck, "sync_acquire", attrs("metric", "missing_state_nodes_max")), 256);
    EXPECT_EQ(gaugeValue(stuck, "sync_acquire", attrs("metric", "received_data_depth")), 4);
    EXPECT_EQ(gaugeValue(stuck, "sync_acquire", attrs("metric", "in_flight")), 2);

    // Zero is a REAL reading here, not a missing series: it says the tx tree
    // needs nothing while the state tree is still stuck, which is exactly the
    // per-map split this signal exists to provide.
    ASSERT_EQ(stuck.at("sync_acquire").count(attrs("metric", "missing_tx_nodes_max")), 1u);
    EXPECT_EQ(gaugeValue(stuck, "sync_acquire", attrs("metric", "missing_tx_nodes_max")), 0);

    // The label key is exactly "metric" and it is the only label present. This
    // is the cardinality guard: a ledger_seq label here would mint a new series
    // per ledger acquired.
    auto const& firstKey = stuck.at("sync_acquire").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "metric");
    EXPECT_EQ(stuck.at("sync_acquire").count(attrs("metric", "ledger_seq")), 0u);

    // A shrinking count is the "slow but alive" reading, and an idle node
    // reports all zeros with in_flight=0 -- distinguishable from a stuck node
    // only because in_flight is exported alongside.
    observed = Observed{128, 0, 1, 2};
    EXPECT_EQ(
        gaugeValue(provider.collect(), "sync_acquire", attrs("metric", "missing_state_nodes_max")),
        128);

    observed = Observed{0, 0, 0, 0};
    auto const idle = provider.collect();
    EXPECT_EQ(gaugeValue(idle, "sync_acquire", attrs("metric", "missing_state_nodes_max")), 0);
    EXPECT_EQ(gaugeValue(idle, "sync_acquire", attrs("metric", "in_flight")), 0);
}

// shamap_cache_hit_rate reports the tree-node cache rate normalized to 0.0-1.0,
// mirroring MetricsRegistry::registerCacheHitRateDetailGauge(). The scaling is
// the part worth pinning: TaggedCache::getHitRate() returns 0-100, and the
// dashboard panel uses percentunit, so an unnormalized value would render as
// 9000% instead of 90%.
TEST(MetricMacros, shamap_cache_hit_rate_gauge_normalizes_to_unit_fraction)
{
    CollectingProvider const provider;

    // What TaggedCache::getHitRate() would return: 90 means 90%.
    float rawHitRatePercent = 90.0F;

    auto gauge = provider.meter()->CreateDoubleObservableGauge(
        "shamap_cache_hit_rate", "SHAMap tree-node cache hit rate (0.0-1.0), by cache");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* raw = static_cast<float const*>(state);
            // Same normalization the production callback performs.
            opentelemetry::nostd::get<
                opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(
                result)
                ->Observe(static_cast<double>(*raw / 100.0F), {{"metric", "treenode"}});
        },
        &rawHitRatePercent);

    auto const warm = provider.collect();

    // Exactly one series: the full-below cache is deliberately not reported
    // (its TaggedCache hit accounting writes members getHitRate() never reads,
    // so it would be a hard-wired zero).
    ASSERT_EQ(warm.at("shamap_cache_hit_rate").size(), 1u);
    EXPECT_EQ(warm.at("shamap_cache_hit_rate").count(attrs("metric", "full_below")), 0u);

    // 90 percent arrives as exactly 0.9, not 90 and not 9000.
    auto const& warmPoint = warm.at("shamap_cache_hit_rate").at(attrs("metric", "treenode"));
    auto const& warmLast = opentelemetry::nostd::get<otel_sdk::LastValuePointData>(warmPoint);
    EXPECT_DOUBLE_EQ(opentelemetry::nostd::get<double>(warmLast.value_), 0.9);

    // A cold cache reads exactly 0.0 -- the fresh-sync case, where every lookup
    // goes to the node store.
    rawHitRatePercent = 0.0F;
    auto const cold = provider.collect();
    auto const& coldPoint = cold.at("shamap_cache_hit_rate").at(attrs("metric", "treenode"));
    auto const& coldLast = opentelemetry::nostd::get<otel_sdk::LastValuePointData>(coldPoint);
    EXPECT_DOUBLE_EQ(opentelemetry::nostd::get<double>(coldLast.value_), 0.0);

    // A fully warm cache reads exactly 1.0, pinning the upper bound of the
    // normalized range.
    rawHitRatePercent = 100.0F;
    auto const full = provider.collect();
    auto const& fullPoint = full.at("shamap_cache_hit_rate").at(attrs("metric", "treenode"));
    auto const& fullLast = opentelemetry::nostd::get<otel_sdk::LastValuePointData>(fullPoint);
    EXPECT_DOUBLE_EQ(opentelemetry::nostd::get<double>(fullLast.value_), 1.0);
}

// RUNTIME-DISABLED no-op proof for the counter half of WP-A3: with the registry
// disabled, all three acquire counters emit NOTHING -- no series at all, and
// meter() is never consulted, so not even an instrument was created.
TEST(MetricMacros, acquire_counters_emit_nothing_when_registry_disabled)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/false, provider.meter());

    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        "sync_acquire_source_total",
        "Ledger acquires by where the data came from",
        {{"source", std::string("network")}});
    XRPL_METRIC_COUNTER_INC(
        app, "sync_acquire_no_progress_total", "Ledger-acquire timeouts where no new node arrived");
    XRPL_METRIC_COUNTER_ADD_LABELED(
        app,
        "sync_addnode_total",
        "SHAMap nodes received during ledger acquire, by outcome",
        static_cast<std::uint64_t>(5),
        {{"outcome", std::string("good")}});

    auto const data = provider.collect();

    // Total absence, not zero-valued series: the instruments never existed.
    EXPECT_EQ(data.count("sync_acquire_source_total"), 0u);
    EXPECT_EQ(data.count("sync_acquire_no_progress_total"), 0u);
    EXPECT_EQ(data.count("sync_addnode_total"), 0u);
    EXPECT_EQ(data.size(), 0u);

    // Cause, not just state: the isEnabled() gate short-circuited before the
    // macros asked for a meter.
    EXPECT_EQ(app.registry().meterCalls(), 0);
}

#endif  // XRPL_ENABLE_TELEMETRY
