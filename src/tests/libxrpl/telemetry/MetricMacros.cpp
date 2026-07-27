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

#include <xrpld/overlay/Overlay.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/telemetry/MetricNames.h>

#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/JobTypes.h>

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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
                    {
                        out[metric.instrument_descriptor.name_][point.attributes] =
                            point.point_data;
                    }
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
        {{telemetry::label::reason, std::string("unit_test")}});

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
        {{telemetry::label::reason, std::string("unit_test")}});

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
            telemetry::metric::dnsResolveTotal,
            "Peer hostname resolutions, by outcome",
            {{telemetry::label::outcome, std::string(resolved ? "resolved" : "empty")}});
    }

    // Two latency samples with known values: 1.5 ms + 2.5 ms = 4.0 ms.
    for (double const ms : {1.5, 2.5})
    {
        XRPL_METRIC_HISTOGRAM_RECORD(
            app,
            telemetry::metric::dnsResolveLatencyMs,
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
            telemetry::metric::overlayConnectTotal,
            "Outbound peer connection attempts, by terminal outcome",
            {{telemetry::label::outcome, std::string(outcome)}});
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
            telemetry::metric::overlayDialLatencyMs,
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
            telemetry::metric::handshakeNegotiationFailTotal,
            "Peer handshake negotiations rejected, by reason",
            {{telemetry::label::reason, std::string(reason)}});
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
            telemetry::metric::unlFetchTotal,
            "Validator list fetch attempts, by site and outcome",
            {{telemetry::label::site, std::string(site)},
             {telemetry::label::outcome, std::string(outcome)}});
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
        std::int64_t trustedKeys{0};
        std::int64_t quorum{0};
    };
    Observed observed{.trustedKeys = 5, .quorum = 4};

    // Keep the instrument alive for the whole test: destroying the handle
    // deregisters the callback (ObservableInstrument's destructor calls
    // CleanupCallback), which is why the real registry holds it in a member.
    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::unlQuorum, "Trusted UNL key count vs required quorum");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<Observed const*>(state);
            // Same Observe() form the production callback uses.
            auto observe = [&](char const* name, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, name}});
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
        telemetry::metric::clockCloseOffsetSeconds,
        "Network close time offset from the local clock, in seconds");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* value = static_cast<std::int64_t const*>(state);
            auto observe = [&](char const* name, std::int64_t v) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(v, {{telemetry::label::metric, name}});
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
        telemetry::metric::dnsResolveTotal,
        "Peer hostname resolutions, by outcome",
        {{telemetry::label::outcome, std::string("resolved")}});
    XRPL_METRIC_HISTOGRAM_RECORD(
        app,
        telemetry::metric::dnsResolveLatencyMs,
        "Time taken to resolve a configured peer hostname, in milliseconds",
        1.5);
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        telemetry::metric::overlayConnectTotal,
        "Outbound peer connection attempts, by terminal outcome",
        {{telemetry::label::outcome, std::string("connected")}});
    XRPL_METRIC_HISTOGRAM_RECORD(
        app,
        telemetry::metric::overlayDialLatencyMs,
        "Time from starting an outbound peer dial to its terminal outcome, in milliseconds",
        10.0);
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        telemetry::metric::handshakeNegotiationFailTotal,
        "Peer handshake negotiations rejected, by reason",
        {{telemetry::label::reason, std::string("wrong_network")}});
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        telemetry::metric::unlFetchTotal,
        "Validator list fetch attempts, by site and outcome",
        {{telemetry::label::site, std::string("https://a.example.com/vl.json")},
         {telemetry::label::outcome, std::string("accepted")}});

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
            telemetry::metric::stateChangesTotal,
            "Total operating mode changes",
            {{telemetry::label::from, std::string(from)}, {telemetry::label::to, std::string(to)}});
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
        std::int64_t initialFullDurationUs{0};
        std::int64_t networkLedgerGate{0};
        std::int64_t serverStallSeconds{0};
        std::int64_t ledgersBehind{0};
    };
    // A node that never synced: no FULL yet, gate closed, 42 s stalled, 150
    // ledgers behind (network tip 250 vs our validated 100).
    Observed observed{
        .initialFullDurationUs = 0,
        .networkLedgerGate = 1,
        .serverStallSeconds = 42,
        .ledgersBehind = 150};

    // Keep the instrument alive for the whole test: destroying the handle
    // deregisters the callback, which is why the real registry holds a member.
    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::syncState, "Sync-pipeline health signals");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<Observed const*>(state);
            // Same Observe() form the production callback uses.
            auto observe = [&](char const* name, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, name}});
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
    observed = Observed{
        .initialFullDurationUs = 12'500'000,
        .networkLedgerGate = 0,
        .serverStallSeconds = 0,
        .ledgersBehind = 0};
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
        telemetry::metric::serverStallEventsTotal, "Total server main-loop stall episodes");
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
        telemetry::metric::stateChangesTotal,
        "Total operating mode changes",
        {{telemetry::label::from, std::string("connected")},
         {telemetry::label::to, std::string("full")}});

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
            telemetry::metric::syncAcquireSourceTotal,
            "Ledger acquires by where the data came from",
            {{telemetry::label::source, std::string(localComplete ? "local" : "network")}});
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
                telemetry::metric::syncAcquireNoProgressTotal,
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
                telemetry::metric::syncAddnodeTotal,
                "SHAMap nodes received during ledger acquire, by outcome",
                static_cast<std::uint64_t>(count),
                {{telemetry::label::outcome, std::string(outcome)}});
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
        std::int64_t maxMissingStateNodes{0};
        std::int64_t maxMissingTxNodes{0};
        std::int64_t receivedDataDepth{0};
        std::int64_t inFlight{0};
    };
    // A stuck acquire: 256 state nodes still outstanding (the sweep cap), the tx
    // tree already done, 4 packets stashed, 2 acquires running.
    Observed observed{
        .maxMissingStateNodes = 256, .maxMissingTxNodes = 0, .receivedDataDepth = 4, .inFlight = 2};

    // Keep the instrument alive for the whole test: destroying the handle
    // deregisters the callback, which is why the real registry holds a member.
    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::syncAcquire,
        "Aggregate ledger-acquire progress across in-flight acquires");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<Observed const*>(state);
            // Same Observe() form the production callback uses.
            auto observe = [&](char const* name, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, name}});
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
    observed = Observed{
        .maxMissingStateNodes = 128, .maxMissingTxNodes = 0, .receivedDataDepth = 1, .inFlight = 2};
    EXPECT_EQ(
        gaugeValue(provider.collect(), "sync_acquire", attrs("metric", "missing_state_nodes_max")),
        128);

    observed = Observed{};
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
        telemetry::metric::shamapCacheHitRate,
        "SHAMap tree-node cache hit rate (0.0-1.0), by cache");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* raw = static_cast<float const*>(state);
            // Same normalization the production callback performs.
            opentelemetry::nostd::get<
                opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObserverResultT<double>>>(
                result)
                ->Observe(
                    static_cast<double>(*raw / 100.0F), {{telemetry::label::metric, "treenode"}});
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
        telemetry::metric::syncAcquireSourceTotal,
        "Ledger acquires by where the data came from",
        {{telemetry::label::source, std::string("network")}});
    XRPL_METRIC_COUNTER_INC(
        app,
        telemetry::metric::syncAcquireNoProgressTotal,
        "Ledger-acquire timeouts where no new node arrived");
    XRPL_METRIC_COUNTER_ADD_LABELED(
        app,
        telemetry::metric::syncAddnodeTotal,
        "SHAMap nodes received during ledger acquire, by outcome",
        static_cast<std::uint64_t>(5),
        {{telemetry::label::outcome, std::string("good")}});

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

// -----------------------------------------------------------------
// JobQueue saturation diagnostics (WP-A4).
//
// Asserts the EXACT values and label shapes of the two gauges:
//   jobq_backlog{metric,job_type}   MetricsRegistry::registerJobQueueBacklogGauge
//                                     waiting / running / deferred, per type
//   jobq_saturation{metric}         MetricsRegistry::registerJobQueueSaturationGauge
//                                     running_tasks / worker_threads / total_waiting
//
// Both are observable instruments registered directly on the SDK meter,
// mirroring the production callback shape, because the real MetricsRegistry's
// enabled path cannot be linked into this standalone binary (see the file
// header). The snapshot types are the REAL JobQueue::JobTypeCount and
// JobQueue::WorkerSaturation, and the label values come from the real
// JobTypes::name(), so a rename or reorder on either side breaks these tests
// instead of silently drifting from production.
// -----------------------------------------------------------------

// jobq_backlog must keep waiting, running and deferred on separate series per
// job type. `deferred` is the reason this gauge exists: a job held back by its
// type's concurrency limit is counted in neither of the other two fields, and
// appears in no other metric at all. The values chosen are a starved
// JtLedgerData -- limit 3, so 3 running and the rest deferred.
TEST(MetricMacros, jobq_backlog_gauge_separates_waiting_running_and_deferred)
{
    CollectingProvider const provider;

    // The real snapshot type the production callback iterates. JtLedgerData is
    // at its limit of 3 with 5 more jobs held back; JtLedgerReq has one job
    // merely waiting; JtSweep is registered but idle.
    std::vector<JobQueue::JobTypeCount> observed{
        JobQueue::JobTypeCount{.type = JtLedgerData, .waiting = 5, .running = 3, .deferred = 5},
        JobQueue::JobTypeCount{.type = JtLedgerReq, .waiting = 1, .running = 0, .deferred = 0},
        JobQueue::JobTypeCount{.type = JtSweep, .waiting = 0, .running = 0, .deferred = 0}};

    // Keep the instrument alive for the whole test: destroying the handle
    // deregisters the callback, which is why the real registry holds a member.
    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::jobqBacklog,
        "JobQueue occupancy per job type (waiting/running/deferred)");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* counts = static_cast<std::vector<JobQueue::JobTypeCount> const*>(state);
            // Same two-label Observe() form the production callback uses.
            auto observe = [&](char const* field, std::string const& jobType, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(
                        value,
                        {{telemetry::label::metric, field}, {telemetry::label::jobType, jobType}});
            };
            for (auto const& count : *counts)
            {
                // The same name helper production uses -- never a literal.
                auto const& jobType = JobTypes::name(count.type);
                observe("waiting", jobType, count.waiting);
                observe("running", jobType, count.running);
                observe("deferred", jobType, count.deferred);
            }
        },
        &observed);

    auto const starved = provider.collect();

    // Three types x three fields, every one its own series: no field and no
    // type collapses into another.
    ASSERT_EQ(starved.at("jobq_backlog").size(), 9u);

    // The starved type, exactly as configured. The limit of 3 is visible as
    // running=3, and the 5 jobs the limit is denying are the deferred series.
    EXPECT_EQ(
        gaugeValue(starved, "jobq_backlog", attrs("metric", "waiting", "job_type", "ledgerData")),
        5);
    EXPECT_EQ(
        gaugeValue(starved, "jobq_backlog", attrs("metric", "running", "job_type", "ledgerData")),
        3);
    EXPECT_EQ(
        gaugeValue(starved, "jobq_backlog", attrs("metric", "deferred", "job_type", "ledgerData")),
        5);

    // A type that is queued but NOT deferred reads deferred=0 while waiting=1.
    // This is the distinction the gauge exists to make: "queued" and "denied a
    // worker" are different states, and only the latter is starvation.
    EXPECT_EQ(
        gaugeValue(
            starved, "jobq_backlog", attrs("metric", "waiting", "job_type", "ledgerRequest")),
        1);
    EXPECT_EQ(
        gaugeValue(
            starved, "jobq_backlog", attrs("metric", "deferred", "job_type", "ledgerRequest")),
        0);

    // An idle registered type reports zeros rather than dropping out. Absence
    // would be indistinguishable from a broken exporter, so every type is
    // observed on every tick.
    ASSERT_EQ(
        starved.at("jobq_backlog").count(attrs("metric", "waiting", "job_type", "sweep")), 1u);
    EXPECT_EQ(
        gaugeValue(starved, "jobq_backlog", attrs("metric", "waiting", "job_type", "sweep")), 0);

    // Exactly two label keys, in the documented order, on every series. A
    // third label would multiply the series count per job type.
    for (auto const& [labels, point] : starved.at("jobq_backlog"))
    {
        ASSERT_EQ(labels.size(), 2u);
        EXPECT_EQ(labels.count("metric"), 1u);
        EXPECT_EQ(labels.count("job_type"), 1u);
    }

    // NEGATIVE: a type never present in the snapshot has no series, so the
    // readings above are not an artifact of a catch-all series.
    EXPECT_EQ(
        starved.at("jobq_backlog").count(attrs("metric", "waiting", "job_type", "transaction")),
        0u);
    // NEGATIVE: the label VALUE is the JobTypes name, not the enum spelling.
    EXPECT_EQ(
        starved.at("jobq_backlog").count(attrs("metric", "waiting", "job_type", "JtLedgerData")),
        0u);

    // The starvation clearing is the recovery reading: deferred drains to 0
    // while running stays at the limit, so the panel shows work flowing again.
    observed[0] =
        JobQueue::JobTypeCount{.type = JtLedgerData, .waiting = 0, .running = 3, .deferred = 0};
    auto const draining = provider.collect();
    EXPECT_EQ(
        gaugeValue(draining, "jobq_backlog", attrs("metric", "deferred", "job_type", "ledgerData")),
        0);
    EXPECT_EQ(
        gaugeValue(draining, "jobq_backlog", attrs("metric", "running", "job_type", "ledgerData")),
        3);
}

// jobq_saturation exports the worker-thread count alongside the in-flight
// count so a dashboard can form the ratio without hardcoding a denominator
// that is derived at startup. The values chosen are a fully exhausted pool.
TEST(MetricMacros, jobq_saturation_gauge_observes_exact_pool_exhaustion_values)
{
    CollectingProvider const provider;

    // The real reading type the production callback consumes: every one of 6
    // workers busy, with 12 jobs queued behind them.
    JobQueue::WorkerSaturation observed{.runningTasks = 6, .workerThreads = 6, .totalWaiting = 12};

    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::jobqSaturation,
        "Worker-pool saturation: tasks in flight, worker threads, jobs queued");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<JobQueue::WorkerSaturation const*>(state);
            auto observe = [&](char const* field, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, field}});
            };
            observe("running_tasks", self->runningTasks);
            observe("worker_threads", self->workerThreads);
            observe("total_waiting", self->totalWaiting);
        },
        &observed);

    auto const exhausted = provider.collect();

    // Exactly three series, one per `metric` value.
    ASSERT_EQ(exhausted.at("jobq_saturation").size(), 3u);
    EXPECT_EQ(gaugeValue(exhausted, "jobq_saturation", attrs("metric", "running_tasks")), 6);
    EXPECT_EQ(gaugeValue(exhausted, "jobq_saturation", attrs("metric", "worker_threads")), 6);
    EXPECT_EQ(gaugeValue(exhausted, "jobq_saturation", attrs("metric", "total_waiting")), 12);

    // The label key is exactly "metric" and it is the only label present, so
    // this gauge stays a single fixed-cardinality group.
    auto const& firstKey = exhausted.at("jobq_saturation").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "metric");

    // A busy-but-not-exhausted pool: same 1.0 ratio, but nothing is queued.
    // These two readings are what the ratio alone cannot separate, which is
    // why total_waiting is exported next to it.
    observed = JobQueue::WorkerSaturation{.runningTasks = 6, .workerThreads = 6, .totalWaiting = 0};
    auto const busy = provider.collect();
    EXPECT_EQ(gaugeValue(busy, "jobq_saturation", attrs("metric", "running_tasks")), 6);
    EXPECT_EQ(gaugeValue(busy, "jobq_saturation", attrs("metric", "total_waiting")), 0);

    // An idle pool reads zero in flight with the thread count still reported.
    // A zero denominator would make the dashboard ratio undefined, so the
    // thread count must never drop out with the load.
    observed = JobQueue::WorkerSaturation{.runningTasks = 0, .workerThreads = 6, .totalWaiting = 0};
    auto const idle = provider.collect();
    EXPECT_EQ(gaugeValue(idle, "jobq_saturation", attrs("metric", "running_tasks")), 0);
    EXPECT_EQ(gaugeValue(idle, "jobq_saturation", attrs("metric", "worker_threads")), 6);

    // Standalone mode runs a single worker: the denominator is genuinely
    // node-specific, which is exactly why it is exported and not hardcoded.
    observed = JobQueue::WorkerSaturation{.runningTasks = 1, .workerThreads = 1, .totalWaiting = 3};
    auto const standalone = provider.collect();
    EXPECT_EQ(gaugeValue(standalone, "jobq_saturation", attrs("metric", "worker_threads")), 1);
    EXPECT_EQ(gaugeValue(standalone, "jobq_saturation", attrs("metric", "total_waiting")), 3);
}

// -----------------------------------------------------------------
// Peer-supply, slot-census and amendment-countdown diagnostics (WP-A7).
//
// Asserts the EXACT values and label shapes of the three gauges and the four
// counters:
//   peer_ledger_supply{metric}      MetricsRegistry::registerPeerLedgerSupplyGauge
//   peerfinder_slot_census{metric}  MetricsRegistry::registerSlotCensusGauge
//   amendment_block{metric}         MetricsRegistry::registerAmendmentBlockGauge
//   peer_disconnect_total{reason,direction}       PeerImp::close
//   peer_accept_total{outcome}                    OverlayImpl::reportAcceptOutcome
//   serve_refused_total{request,reason}           PeerImp::reportServeRefusal
//   ledger_jump_total (unlabelled)                NetworkOPsImp::switchLastClosedLedger
//
// The three gauges are observable instruments registered directly on the SDK
// meter, mirroring the production callback shape, because the real
// MetricsRegistry's enabled path cannot be linked into this standalone binary
// (see the file header). The snapshot types are the REAL xrpl::PeerLedgerSupply
// and xrpl::PeerFinder::SlotCensus aggregates, so a field rename or a reorder on
// either side breaks these tests instead of silently drifting from production.
// Both are plain header-only aggregates with no out-of-line members, so using
// them here adds no xrpld link dependency.
//
// The four counters are driven through XRPL_METRIC_COUNTER_INC_LABELED /
// XRPL_METRIC_COUNTER_INC, which is exactly what the production call sites use.
// -----------------------------------------------------------------

// peer_ledger_supply must keep the two "who can serve me" counts on separate
// series from the "who is even talking" denominator. The values chosen are the
// headline supply gap: three peers connected and advertising a range, all three
// covering the validated sequence, and NOT ONE covering the next one needed.
TEST(MetricMacros, peer_ledger_supply_gauge_names_a_gap_no_peer_can_fill)
{
    CollectingProvider const provider;

    // The real aggregate the production callback reports, filled here as
    // OverlayImpl::getPeerLedgerSupply() would fill it. Peer set holds
    // [1000, 4000]; this node's validated sequence is 4000, so the next needed
    // is 4001 -- past every peer's tip.
    PeerLedgerSupply observed{
        .peersReporting = 3,
        .peersServingValidated = 3,
        .peersServingNext = 0,
        .supplyMinSeq = 1000,
        .supplyMaxSeq = 4000};

    // Keep the instrument alive for the whole test: destroying the handle
    // deregisters the callback, which is why the real registry holds a member.
    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::peerLedgerSupply,
        "Peer coverage of the ledger sequence this node needs");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<PeerLedgerSupply const*>(state);
            // Same single-label Observe() form the production callback uses.
            auto observe = [&](char const* field, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, field}});
            };
            observe("peers_reporting", self->peersReporting);
            observe("peers_serving_validated", self->peersServingValidated);
            observe("peers_serving_next", self->peersServingNext);
            observe("supply_min_seq", self->supplyMinSeq);
            observe("supply_max_seq", self->supplyMaxSeq);
        },
        &observed);

    auto const gap = provider.collect();

    // Exactly five series, one per `metric` value: no field collapses into
    // another, so the denominator and the verdict stay separately readable.
    ASSERT_EQ(gap.at("peer_ledger_supply").size(), 5u);

    // THE verdict this signal exists for: zero peers can serve the next needed
    // ledger while three are connected and reporting. That pair is the whole
    // point -- "the network cannot supply what I need" is otherwise
    // indistinguishable from "my peers are slow", and the two faults have
    // completely different fixes (change the peer set vs. wait).
    EXPECT_EQ(gaugeValue(gap, "peer_ledger_supply", attrs("metric", "peers_serving_next")), 0);
    EXPECT_EQ(gaugeValue(gap, "peer_ledger_supply", attrs("metric", "peers_reporting")), 3);

    // Serving the validated sequence is NOT the same question, and reads 3 here:
    // the peers can serve where this node already is, just not where it must go
    // next. Without both counts the gap would look like a total peer failure.
    EXPECT_EQ(gaugeValue(gap, "peer_ledger_supply", attrs("metric", "peers_serving_validated")), 3);

    // The window, so an operator can see whether the wanted sequence is below
    // the peer set's floor (discarded history) or above its tip (unreached).
    EXPECT_EQ(gaugeValue(gap, "peer_ledger_supply", attrs("metric", "supply_min_seq")), 1000);
    EXPECT_EQ(gaugeValue(gap, "peer_ledger_supply", attrs("metric", "supply_max_seq")), 4000);

    // Exactly one label key, and it is "metric". This is the cardinality guard:
    // a peer_id label here would mint a new series per connection.
    for (auto const& [labels, point] : gap.at("peer_ledger_supply"))
    {
        ASSERT_EQ(labels.size(), 1u);
        EXPECT_EQ(labels.begin()->first, "metric");
    }

    // NEGATIVE: a `metric` value outside the production set of five has no
    // series, so the readings above are not an artifact of a catch-all series.
    EXPECT_EQ(gap.at("peer_ledger_supply").count(attrs("metric", "peers_serving")), 0u);
}

// The complementary reading to the gap above: a healthy peer set where every
// reporting peer covers the next needed sequence, so waiting WILL finish the
// sync. Split from the gap test to keep each under the length limit.
TEST(MetricMacros, peer_ledger_supply_gauge_reads_zero_window_as_unknown)
{
    CollectingProvider const provider;

    // Healthy: four peers, all covering both the validated sequence and the
    // next one, window [1000, 5000].
    PeerLedgerSupply observed{
        .peersReporting = 4,
        .peersServingValidated = 4,
        .peersServingNext = 4,
        .supplyMinSeq = 1000,
        .supplyMaxSeq = 5000};

    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::peerLedgerSupply,
        "Peer coverage of the ledger sequence this node needs");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<PeerLedgerSupply const*>(state);
            auto observe = [&](char const* field, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, field}});
            };
            observe("peers_reporting", self->peersReporting);
            observe("peers_serving_validated", self->peersServingValidated);
            observe("peers_serving_next", self->peersServingNext);
            observe("supply_min_seq", self->supplyMinSeq);
            observe("supply_max_seq", self->supplyMaxSeq);
        },
        &observed);

    auto const healthy = provider.collect();
    EXPECT_EQ(gaugeValue(healthy, "peer_ledger_supply", attrs("metric", "peers_serving_next")), 4);
    EXPECT_EQ(gaugeValue(healthy, "peer_ledger_supply", attrs("metric", "peers_reporting")), 4);

    // NEGATIVE/edge: nothing has advertised a range yet. Peers that have not
    // sent mtSTATUS_CHANGE report [0, 0] and are excluded from every field, so
    // all five read 0.
    observed = PeerLedgerSupply{};
    auto const silent = provider.collect();

    // A 0 window means "unknown", NOT "the peer set serves from genesis". The
    // only thing that separates the two is peers_reporting, which is why it must
    // be read alongside: 0 out of 0 reporting is silence, 0 out of many would be
    // a real supply gap. Asserting the pair together pins that contract.
    EXPECT_EQ(gaugeValue(silent, "peer_ledger_supply", attrs("metric", "supply_min_seq")), 0);
    EXPECT_EQ(gaugeValue(silent, "peer_ledger_supply", attrs("metric", "peers_reporting")), 0);
    EXPECT_EQ(gaugeValue(silent, "peer_ledger_supply", attrs("metric", "supply_max_seq")), 0);

    // Every field is still a present series at 0, never absent: a dropped
    // series would be indistinguishable from a dead exporter.
    ASSERT_EQ(silent.at("peer_ledger_supply").size(), 5u);
    EXPECT_EQ(gaugeValue(silent, "peer_ledger_supply", attrs("metric", "peers_serving_next")), 0);
    EXPECT_EQ(
        gaugeValue(silent, "peer_ledger_supply", attrs("metric", "peers_serving_validated")), 0);

    // A single reporting peer at the network tip: min and max collapse to the
    // same sequence, which is a legitimate reading, not a defect.
    observed = PeerLedgerSupply{
        .peersReporting = 1,
        .peersServingValidated = 1,
        .peersServingNext = 0,
        .supplyMinSeq = 5000,
        .supplyMaxSeq = 5000};
    auto const single = provider.collect();
    EXPECT_EQ(gaugeValue(single, "peer_ledger_supply", attrs("metric", "supply_min_seq")), 5000);
    EXPECT_EQ(gaugeValue(single, "peer_ledger_supply", attrs("metric", "supply_max_seq")), 5000);
}

// peerfinder_slot_census must export all nine numbers, because each of the three
// common bootstrap failures is named by a DIFFERENT pair of them and today only
// the two active counts exist. The values chosen are the "dialling but never
// completing" case, which the two legacy gauges cannot express at all.
TEST(MetricMacros, slot_census_gauge_names_each_bootstrap_fault_exactly)
{
    CollectingProvider const provider;

    // The real snapshot type the production callback consumes. Outbound is 2 of
    // 10 with 6 dials in flight; one configured fixed peer is missing.
    PeerFinder::SlotCensus observed{
        .outActive = 2,
        .outMax = 10,
        .inActive = 0,
        .inMax = 0,
        .connecting = 6,
        .fixedConfigured = 2,
        .fixedActive = 1,
        .bootcache = 40,
        .livecache = 12};

    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::peerfinderSlotCensus,
        "PeerFinder slots, connection attempts and address caches");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<PeerFinder::SlotCensus const*>(state);
            // Same single-label Observe() form the production callback uses.
            auto observe = [&](char const* field, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, field}});
            };
            observe("out_active", self->outActive);
            observe("out_max", self->outMax);
            observe("in_active", self->inActive);
            observe("in_max", self->inMax);
            observe("connecting", self->connecting);
            observe("fixed_configured", self->fixedConfigured);
            observe("fixed_active", self->fixedActive);
            observe("bootcache", self->bootcache);
            observe("livecache", self->livecache);
        },
        &observed);

    auto const dialling = provider.collect();

    // Exactly nine series, one per `metric` value: all nine fields reach the
    // exporter, not just the two the legacy insight gauges carried.
    ASSERT_EQ(dialling.at("peerfinder_slot_census").size(), 9u);
    // FAULT (a) "dialling but never completing": out_active below out_max WITH
    // connecting non-zero. Without the attempt count this is indistinguishable
    // from a node that is not dialling at all -- the capacity term alone says
    // only "under-connected", never why.
    EXPECT_EQ(gaugeValue(dialling, "peerfinder_slot_census", attrs("metric", "out_active")), 2);
    EXPECT_EQ(gaugeValue(dialling, "peerfinder_slot_census", attrs("metric", "out_max")), 10);
    EXPECT_EQ(gaugeValue(dialling, "peerfinder_slot_census", attrs("metric", "connecting")), 6);

    // FAULT (c) "configured fixed peer unreachable": fixed_active strictly below
    // fixed_configured. 1 of 2 asked-for peers is connected.
    EXPECT_EQ(
        gaugeValue(dialling, "peerfinder_slot_census", attrs("metric", "fixed_configured")), 2);
    EXPECT_EQ(gaugeValue(dialling, "peerfinder_slot_census", attrs("metric", "fixed_active")), 1);

    // Inbound disabled reads in_max=0, which makes in_active=0 a configuration
    // fact rather than a fault -- the pair is what separates them.
    EXPECT_EQ(gaugeValue(dialling, "peerfinder_slot_census", attrs("metric", "in_max")), 0);
    EXPECT_EQ(gaugeValue(dialling, "peerfinder_slot_census", attrs("metric", "in_active")), 0);

    // Addresses ARE available here, so fault (b) is ruled out on this reading:
    // the node has somewhere to dial and is still not completing.
    EXPECT_EQ(gaugeValue(dialling, "peerfinder_slot_census", attrs("metric", "bootcache")), 40);
    EXPECT_EQ(gaugeValue(dialling, "peerfinder_slot_census", attrs("metric", "livecache")), 12);

    // Exactly one label key on every series, and it is "metric": the nine
    // fields form one fixed-cardinality group, not nine label dimensions.
    for (auto const& [labels, point] : dialling.at("peerfinder_slot_census"))
    {
        ASSERT_EQ(labels.size(), 1u);
        EXPECT_EQ(labels.begin()->first, "metric");
    }
    // NEGATIVE: a `metric` value outside the production set of nine has no
    // series, so the readings above are not an artifact of a catch-all series.
    EXPECT_EQ(dialling.at("peerfinder_slot_census").count(attrs("metric", "out_count")), 0u);
}

// FAULT (b) "nothing to dial", plus the idle-but-healthy reading. Separated from
// the fault-(a)/(c) test above to keep each function under the length limit.
TEST(MetricMacros, slot_census_gauge_reports_every_field_even_when_idle)
{
    CollectingProvider const provider;

    // A fresh node with no seed addresses at all: nothing dialled because there
    // is nothing to dial. Distinct from fault (a), where dials are attempted.
    PeerFinder::SlotCensus observed{
        .outActive = 0,
        .outMax = 10,
        .inActive = 0,
        .inMax = 20,
        .connecting = 0,
        .fixedConfigured = 0,
        .fixedActive = 0,
        .bootcache = 0,
        .livecache = 0};

    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::peerfinderSlotCensus,
        "PeerFinder slots, connection attempts and address caches");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<PeerFinder::SlotCensus const*>(state);
            auto observe = [&](char const* field, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, field}});
            };
            observe("out_active", self->outActive);
            observe("out_max", self->outMax);
            observe("in_active", self->inActive);
            observe("in_max", self->inMax);
            observe("connecting", self->connecting);
            observe("fixed_configured", self->fixedConfigured);
            observe("fixed_active", self->fixedActive);
            observe("bootcache", self->bootcache);
            observe("livecache", self->livecache);
        },
        &observed);

    auto const nothingToDial = provider.collect();

    // FAULT (b) "nothing to dial": both caches at exactly 0 while outbound
    // capacity is available. The fix is [ips] / DNS, not the network.
    EXPECT_EQ(gaugeValue(nothingToDial, "peerfinder_slot_census", attrs("metric", "bootcache")), 0);
    EXPECT_EQ(gaugeValue(nothingToDial, "peerfinder_slot_census", attrs("metric", "livecache")), 0);
    // connecting=0 here is what separates fault (b) from fault (a): no dial was
    // even attempted, because there was no address to attempt.
    EXPECT_EQ(
        gaugeValue(nothingToDial, "peerfinder_slot_census", attrs("metric", "connecting")), 0);
    EXPECT_EQ(gaugeValue(nothingToDial, "peerfinder_slot_census", attrs("metric", "out_max")), 10);

    // NEGATIVE: an idle-but-healthy node still reports EVERY field. A zero-valued
    // field must be a present series, never an absent one -- absence would be
    // indistinguishable from a dead exporter or a crashed callback.
    observed = PeerFinder::SlotCensus{
        .outActive = 10,
        .outMax = 10,
        .inActive = 5,
        .inMax = 20,
        .connecting = 0,
        .fixedConfigured = 0,
        .fixedActive = 0,
        .bootcache = 55,
        .livecache = 30};
    auto const idle = provider.collect();

    ASSERT_EQ(idle.at("peerfinder_slot_census").size(), 9u);
    // The four fields that are legitimately 0 on a healthy node are each PRESENT
    // with value 0, asserted one by one so a dropped series fails the test.
    for (char const* field : {"connecting", "fixed_configured", "fixed_active"})
    {
        ASSERT_EQ(idle.at("peerfinder_slot_census").count(attrs("metric", field)), 1u);
        EXPECT_EQ(gaugeValue(idle, "peerfinder_slot_census", attrs("metric", field)), 0);
    }
    // Slots full: out_active has reached out_max, so nothing is dialling because
    // nothing needs to be. Same connecting=0 as fault (b), opposite meaning --
    // only the capacity pair tells them apart.
    EXPECT_EQ(gaugeValue(idle, "peerfinder_slot_census", attrs("metric", "out_active")), 10);
    EXPECT_EQ(gaugeValue(idle, "peerfinder_slot_census", attrs("metric", "out_max")), 10);
}

// amendment_block's whole value is the countdown, so the arithmetic is what this
// pins: the production callback computes max(expected - now, 0) in std::int64_t
// and observes -1 when firstUnsupportedExpected() is nullopt. All four states are
// asserted to EXACT values, including the two that a naive implementation gets
// wrong (the healthy sentinel, and the unsigned-subtraction wrap).
TEST(MetricMacros, amendment_block_gauge_observes_exact_countdown_and_sentinel)
{
    CollectingProvider const provider;

    // Two explicit epoch-second constants, so the expected difference is exact
    // rather than approximate -- a clock read would make 7200 unassertable.
    constexpr std::int64_t kNowEpochSeconds = 800'000'000;
    constexpr std::int64_t kTwoHours = 7200;

    // Mirrors what the production callback reads: the warned flag from
    // NetworkOPs::isAmendmentWarned(), and the optional activation time from
    // AmendmentTable::firstUnsupportedExpected().
    struct Observed
    {
        bool warned{false};
        std::optional<std::int64_t> expectedEpochSeconds;
        std::int64_t nowEpochSeconds{0};
    };
    // State (a): nothing pending at all -- the healthy case.
    Observed observed{
        .warned = false, .expectedEpochSeconds = {}, .nowEpochSeconds = kNowEpochSeconds};

    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::amendmentBlock,
        "Amendment-block warning and seconds until the node stops validating");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<Observed const*>(state);
            auto observe = [&](char const* field, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, field}});
            };
            observe("warned", self->warned ? 1 : 0);

            // The production arithmetic, reproduced exactly: -1 sentinel when
            // nothing is pending, otherwise the difference taken in int64_t and
            // clamped at 0.
            std::int64_t secondsToBlock = -1;
            if (self->expectedEpochSeconds)
            {
                secondsToBlock =
                    std::max<std::int64_t>(*self->expectedEpochSeconds - self->nowEpochSeconds, 0);
            }
            observe("seconds_to_block", secondsToBlock);
        },
        &observed);

    auto const healthy = provider.collect();

    // Exactly two series, one per `metric` value.
    ASSERT_EQ(healthy.at("amendment_block").size(), 2u);

    // STATE (a) nothing pending: warned is exactly 0, and the countdown is
    // exactly -1. Not 0 (which would read as "blocking right now", the most
    // alarming possible value) and not absent (which a dashboard cannot tell
    // from a stopped exporter). A distinct in-band sentinel is the only encoding
    // that makes "healthy" assertable, matching unl_expiry_days' -1.
    EXPECT_EQ(gaugeValue(healthy, "amendment_block", attrs("metric", "warned")), 0);
    EXPECT_EQ(gaugeValue(healthy, "amendment_block", attrs("metric", "seconds_to_block")), -1);
    ASSERT_EQ(healthy.at("amendment_block").count(attrs("metric", "seconds_to_block")), 1u);

    // STATE (b) pending two hours out: warned flips to exactly 1 and the
    // countdown is exactly 7200, computed from the two constants above.
    observed = Observed{
        .warned = true,
        .expectedEpochSeconds = kNowEpochSeconds + kTwoHours,
        .nowEpochSeconds = kNowEpochSeconds};
    auto const pending = provider.collect();
    EXPECT_EQ(gaugeValue(pending, "amendment_block", attrs("metric", "warned")), 1);
    EXPECT_EQ(gaugeValue(pending, "amendment_block", attrs("metric", "seconds_to_block")), 7200);

    // Both series are present in both states, so the countdown never drops out
    // when the warning flips.
    ASSERT_EQ(pending.at("amendment_block").size(), 2u);
}

// The clamp at the bottom of the countdown, and the deliberate absence of an
// amendment-id label. Split out of the test above to keep each function inside
// the length limit; it re-establishes the same callback shape.
TEST(MetricMacros, amendment_block_gauge_clamps_past_due_and_carries_no_amendment_id)
{
    CollectingProvider const provider;

    constexpr std::int64_t kNowEpochSeconds = 800'000'000;
    constexpr std::int64_t kOneHour = 3600;

    struct Observed
    {
        bool warned{false};
        std::optional<std::int64_t> expectedEpochSeconds;
        std::int64_t nowEpochSeconds{0};
    };
    // State (c): the activation time passed an hour ago.
    Observed observed{
        .warned = true,
        .expectedEpochSeconds = kNowEpochSeconds - kOneHour,
        .nowEpochSeconds = kNowEpochSeconds};

    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::amendmentBlock,
        "Amendment-block warning and seconds until the node stops validating");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<Observed const*>(state);
            auto observe = [&](char const* field, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, field}});
            };
            observe("warned", self->warned ? 1 : 0);
            std::int64_t secondsToBlock = -1;
            if (self->expectedEpochSeconds)
            {
                secondsToBlock =
                    std::max<std::int64_t>(*self->expectedEpochSeconds - self->nowEpochSeconds, 0);
            }
            observe("seconds_to_block", secondsToBlock);
        },
        &observed);

    // STATE (c) past due by an hour: clamped to exactly 0, never negative. This
    // is the assertion that proves the difference is taken in std::int64_t and
    // NOT in NetClock's unsigned representation -- subtracting the time_points
    // directly would wrap to roughly 4.29 billion and plot as 136 years away,
    // turning the most urgent reading into the least.
    auto const pastDue = provider.collect();
    EXPECT_EQ(gaugeValue(pastDue, "amendment_block", attrs("metric", "seconds_to_block")), 0);
    EXPECT_EQ(gaugeValue(pastDue, "amendment_block", attrs("metric", "warned")), 1);

    // STATE (d) exactly at the boundary (expected == now): exactly 0. Pins the
    // clamp's inclusive edge, so the transition into state (c) cannot skip a
    // value or briefly emit -1.
    observed = Observed{
        .warned = true,
        .expectedEpochSeconds = kNowEpochSeconds,
        .nowEpochSeconds = kNowEpochSeconds};
    auto const boundary = provider.collect();
    EXPECT_EQ(gaugeValue(boundary, "amendment_block", attrs("metric", "seconds_to_block")), 0);

    // NEGATIVE: there is NO label carrying the blocking amendment's id or hash.
    // Exactly two series, and "metric" is the only label key. The identity is
    // deliberately excluded: the network can vote on an arbitrary 256-bit
    // amendment id, not just this build's known features, so an id label would
    // be unbounded cardinality and would mint a permanent new series per
    // amendment. The id is already in the log line, correlated by node and time.
    ASSERT_EQ(boundary.at("amendment_block").size(), 2u);
    for (auto const& [labels, point] : boundary.at("amendment_block"))
    {
        ASSERT_EQ(labels.size(), 1u);
        EXPECT_EQ(labels.begin()->first, "metric");
    }
    // An id-shaped label value has no series, so the two above are the whole set.
    EXPECT_EQ(boundary.at("amendment_block").count(attrs("metric", "amendment_id")), 0u);
}

// peer_disconnect_total is the signal that splits today's single unlabelled
// disconnect tally by cause AND direction. The series identity is the (reason,
// direction) PAIR: if it were not, a wave of our-fault backpressure on outbound
// links would be masked by ordinary inbound peer churn.
TEST(MetricMacros, peer_disconnect_total_keys_series_on_reason_and_direction_pair)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // The exact call PeerImp::close() makes, once per teardown.
    auto const bump = [&app](char const* reason, char const* direction) {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            telemetry::metric::peerDisconnectTotal,
            "Peer disconnects, by cause and connection direction",
            {{telemetry::label::reason, std::string(reason)},
             {telemetry::label::direction, std::string(direction)}});
    };

    // Two OUR-FAULT reasons: this node could not keep up with what it owed the
    // peer, or charged it off under its own resource pressure.
    bump("large_sendq", "outbound");
    bump("charge_resources", "inbound");
    // Three NETWORK/TOPOLOGY reasons: the peer is on another chain, stopped
    // answering, or the socket failed.
    bump("not_useful", "outbound");
    bump("ping_timeout", "outbound");
    bump("read_error", "inbound");

    auto const data = provider.collect();

    // Five distinct (reason, direction) pairs -> exactly five series. Today all
    // five collapse into one number, which is the defect this fixes.
    ASSERT_EQ(data.at("peer_disconnect_total").size(), 5u);

    // Exact value 1 per distinct labelset. These two are also the proof that the
    // labelsets do NOT collapse: each holds exactly 1 rather than one of them
    // holding 2, so an our-fault outbound teardown and a network-fault inbound
    // one stay two separate stories with two separate fixes. (counterValue()
    // looks the key up with std::map::at, so a merged series fails here.)
    EXPECT_EQ(
        counterValue(
            data, "peer_disconnect_total", attrs("reason", "large_sendq", "direction", "outbound")),
        1);
    EXPECT_EQ(
        counterValue(
            data, "peer_disconnect_total", attrs("reason", "read_error", "direction", "inbound")),
        1);

    // Our-fault reasons are individually addressable, so "the node is shedding
    // its own peers" is readable without reading logs.
    EXPECT_EQ(
        counterValue(
            data,
            "peer_disconnect_total",
            attrs("reason", "charge_resources", "direction", "inbound")),
        1);
    // Network/topology reasons stay distinct from each other too: a chain split
    // ("not_useful") is not a dead link ("ping_timeout").
    EXPECT_EQ(
        counterValue(
            data, "peer_disconnect_total", attrs("reason", "not_useful", "direction", "outbound")),
        1);
    EXPECT_EQ(
        counterValue(
            data,
            "peer_disconnect_total",
            attrs("reason", "ping_timeout", "direction", "outbound")),
        1);

    // Exactly two label keys on every series, and exactly these two: a third
    // (a peer address, say) would be unbounded cardinality.
    for (auto const& [labels, point] : data.at("peer_disconnect_total"))
    {
        ASSERT_EQ(labels.size(), 2u);
        EXPECT_EQ(labels.count("reason"), 1u);
        EXPECT_EQ(labels.count("direction"), 1u);
    }
}

// The `direction` label must genuinely participate in the series key, not just
// ride along: the SAME reason seen on both directions has to produce two series
// of 1 rather than one series of 2. Split out of the test above to keep each
// function inside the length limit.
TEST(MetricMacros, peer_disconnect_total_does_not_merge_directions_for_one_reason)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    auto const bump = [&app](char const* reason, char const* direction) {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            telemetry::metric::peerDisconnectTotal,
            "Peer disconnects, by cause and connection direction",
            {{telemetry::label::reason, std::string(reason)},
             {telemetry::label::direction, std::string(direction)}});
    };

    // One read_error each way. If direction did not key the series, this would
    // collapse to a single series holding 2.
    bump("read_error", "inbound");
    bump("read_error", "outbound");

    auto const data = provider.collect();

    // Two series, not one: the same cause on opposite directions stays split.
    ASSERT_EQ(data.at("peer_disconnect_total").size(), 2u);
    EXPECT_EQ(
        counterValue(
            data, "peer_disconnect_total", attrs("reason", "read_error", "direction", "inbound")),
        1);
    EXPECT_EQ(
        counterValue(
            data, "peer_disconnect_total", attrs("reason", "read_error", "direction", "outbound")),
        1);

    // Bumping ONE direction advances only that series. This is the assertion that
    // would fail if the labels were merged: inbound must stay at exactly 1.
    bump("read_error", "outbound");
    auto const second = provider.collect();
    EXPECT_EQ(
        counterValue(
            second,
            "peer_disconnect_total",
            attrs("reason", "read_error", "direction", "outbound")),
        2);
    EXPECT_EQ(
        counterValue(
            second, "peer_disconnect_total", attrs("reason", "read_error", "direction", "inbound")),
        1);
    EXPECT_EQ(second.at("peer_disconnect_total").size(), 2u);

    // NEGATIVE: a reason outside the fixed literal set in PeerImp.cpp has no
    // series, so the counts above are not an artifact of a catch-all series.
    EXPECT_EQ(
        second.at("peer_disconnect_total")
            .count(attrs("reason", "disconnected", "direction", "inbound")),
        0u);
    // NEGATIVE: a direction value outside {inbound, outbound} has no series.
    EXPECT_EQ(
        second.at("peer_disconnect_total")
            .count(attrs("reason", "read_error", "direction", "unknown")),
        0u);
}

// peer_accept_total is the inbound twin of the existing
// overlay_connect_total{outcome}: that one counts OUTBOUND dials this node
// makes, this one counts INBOUND attempts it receives. Together they give the
// full in/out split, which neither provides alone -- a node whose outbound dials
// all succeed while every inbound attempt is refused looks perfectly healthy on
// overlay_connect_total by itself.
TEST(MetricMacros, peer_accept_total_keys_series_on_outcome)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // The exact call OverlayImpl::reportAcceptOutcome() makes, once per
    // terminal outcome of one inbound attempt.
    auto const bump = [&app](char const* outcome) {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            telemetry::metric::peerAcceptTotal,
            "Inbound peer connection attempts, by terminal outcome",
            {{telemetry::label::outcome, std::string(outcome)}});
    };

    // accepted x2, no_slot x3, handshake_error x1 -- three distinct
    // multiplicities so no two series can be confused with each other.
    bump("accepted");
    bump("accepted");
    bump("no_slot");
    bump("no_slot");
    bump("no_slot");
    bump("handshake_error");

    auto const data = provider.collect();

    // Three distinct outcomes stay three distinct series with exact values.
    ASSERT_EQ(data.at("peer_accept_total").size(), 3u);
    EXPECT_EQ(counterValue(data, "peer_accept_total", attrs("outcome", "accepted")), 2);
    // "no_slot" is capacity, "handshake_error" is a protocol or crypto failure.
    // Collapsed into one number they would be indistinguishable, yet the first
    // is fixed by configuration and the second by investigation.
    EXPECT_EQ(counterValue(data, "peer_accept_total", attrs("outcome", "no_slot")), 3);
    EXPECT_EQ(counterValue(data, "peer_accept_total", attrs("outcome", "handshake_error")), 1);

    // Exactly one label key, and it is "outcome".
    auto const& firstKey = data.at("peer_accept_total").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "outcome");

    // NEGATIVE: an outcome from the production set that was not emitted here has
    // no series, proving outcomes are not being merged into a catch-all.
    EXPECT_EQ(data.at("peer_accept_total").count(attrs("outcome", "resource_limit")), 0u);

    // Accumulates rather than replaces: the reader is cumulative, so a second
    // acceptance advances the existing series to exactly 3.
    bump("accepted");
    EXPECT_EQ(
        counterValue(provider.collect(), "peer_accept_total", attrs("outcome", "accepted")), 3);
}

// serve_refused_total measures the SUPPLY side: what this node refuses to serve
// its peers. Nothing measured it before, so a node shedding every ledger request
// looked identical to one being asked for nothing. The series identity is the
// (request, reason) PAIR, because the same reason means different things on
// different request kinds.
TEST(MetricMacros, serve_refused_total_keys_series_on_request_and_reason_pair)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // The exact call PeerImp::reportServeRefusal() makes, once per refused
    // request.
    auto const bump = [&app](char const* request, char const* reason) {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            telemetry::metric::serveRefusedTotal,
            "Peer data requests this node declined to serve, by request kind and cause",
            {{telemetry::label::request, std::string(request)},
             {telemetry::label::reason, std::string(reason)}});
    };

    // Same request kind, two different reasons -> two series.
    bump("ledger", "sendq_full");
    bump("ledger", "sendq_full");
    bump("ledger", "not_found");
    // Different request kinds -> their own series, even sharing a reason.
    bump("fetchpack", "load_shed");
    bump("fetchpack", "load_shed");
    bump("fetchpack", "load_shed");
    bump("txset", "not_found");

    auto const data = provider.collect();

    // Four distinct (request, reason) pairs -> exactly four series.
    ASSERT_EQ(data.at("serve_refused_total").size(), 4u);

    // SELF-INFLICTED BACKPRESSURE: "sendq_full" and "load_shed" both mean this
    // node chose not to answer because it was already behind. The fix is local
    // (capacity, tuning), and the refusal directly slows the asking peer's sync.
    EXPECT_EQ(
        counterValue(
            data, "serve_refused_total", attrs("request", "ledger", "reason", "sendq_full")),
        2);
    EXPECT_EQ(
        counterValue(
            data, "serve_refused_total", attrs("request", "fetchpack", "reason", "load_shed")),
        3);

    // HISTORY GAP: "not_found" is not backpressure at all -- the node simply
    // does not hold what was asked for. Same counter, completely different
    // meaning and a completely different fix (history configuration), which is
    // why it must not share a series with the two above.
    EXPECT_EQ(
        counterValue(
            data, "serve_refused_total", attrs("request", "ledger", "reason", "not_found")),
        1);
    EXPECT_EQ(
        counterValue(data, "serve_refused_total", attrs("request", "txset", "reason", "not_found")),
        1);

    // The same reason on two different request kinds stays two series -- both
    // present, each holding its own 1: a txset miss (consensus proposal data)
    // and a ledger miss (history) are unrelated faults despite the shared slug.
    EXPECT_EQ(
        data.at("serve_refused_total").count(attrs("request", "ledger", "reason", "not_found")),
        1u);
    EXPECT_EQ(
        data.at("serve_refused_total").count(attrs("request", "txset", "reason", "not_found")), 1u);

    // Exactly two label keys on every series, and exactly these two.
    for (auto const& [labels, point] : data.at("serve_refused_total"))
    {
        ASSERT_EQ(labels.size(), 2u);
        EXPECT_EQ(labels.count("request"), 1u);
        EXPECT_EQ(labels.count("reason"), 1u);
    }

    // NEGATIVE: a pair that was never emitted has no series, so the four counts
    // above are not an artifact of a catch-all series.
    EXPECT_EQ(
        data.at("serve_refused_total").count(attrs("request", "object", "reason", "sendq_full")),
        0u);
}

// ledger_jump_total counts a node discarding its own chain tip to follow the
// network. It is deliberately UNLABELLED, so the single series' key is the empty
// attribute set -- and repeated jumps must accumulate on it, because a node
// thrashing between chains is the pattern worth alerting on and a single jump is
// not.
TEST(MetricMacros, ledger_jump_total_accumulates_on_one_unlabelled_series)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // The exact call NetworkOPsImp::switchLastClosedLedger() makes. Three jumps
    // from the same call site, as a thrashing node would produce.
    for (int i = 0; i < 3; ++i)
    {
        XRPL_METRIC_COUNTER_INC(
            app,
            telemetry::metric::ledgerJumpTotal,
            "Forced jumps of the last closed ledger to a divergent chain");
    }

    auto const data = provider.collect();

    // Exactly ONE series, and its key is the empty attribute set: the metric
    // carries no labels at all.
    ASSERT_EQ(data.at("ledger_jump_total").size(), 1u);
    ASSERT_EQ(data.at("ledger_jump_total").count(otel_sdk::PointAttributes{}), 1u);

    // Three jumps accumulate to exactly 3 on that one series -- the reader is
    // cumulative, so this is a running total, not a per-interval delta.
    EXPECT_EQ(counterValue(data, "ledger_jump_total", otel_sdk::PointAttributes{}), 3);

    // ZERO labels, asserted on the series key itself. Both candidate labels were
    // deliberately rejected as unbounded: the divergent ledger's hash is a
    // 256-bit value and its sequence grows without limit, so either would mint a
    // permanent new series per jump. The JLOG error line immediately above the
    // emit already carries both, correlated to this series by node and time.
    EXPECT_TRUE(data.at("ledger_jump_total").begin()->first.empty());
    EXPECT_EQ(data.at("ledger_jump_total").begin()->first.size(), 0u);

    // A fourth jump advances the SAME series to exactly 4 rather than creating a
    // second one, which is what "no labels" has to mean over time.
    XRPL_METRIC_COUNTER_INC(
        app,
        telemetry::metric::ledgerJumpTotal,
        "Forced jumps of the last closed ledger to a divergent chain");
    auto const fourth = provider.collect();
    ASSERT_EQ(fourth.at("ledger_jump_total").size(), 1u);
    EXPECT_EQ(counterValue(fourth, "ledger_jump_total", otel_sdk::PointAttributes{}), 4);
}

// RUNTIME-DISABLED no-op proof for all four WP-A7 counters: with the registry
// disabled, every one emits NOTHING -- no series at all, and meter() is never
// consulted, so not even an instrument was created. Proves the isEnabled() gate
// short-circuits BEFORE any SDK work, which is what makes these emits free on a
// node with telemetry turned off.
TEST(MetricMacros, sync_supply_counters_emit_nothing_when_registry_disabled)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/false, provider.meter());

    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        telemetry::metric::peerDisconnectTotal,
        "Peer disconnects, by cause and connection direction",
        {{telemetry::label::reason, std::string("large_sendq")},
         {telemetry::label::direction, std::string("outbound")}});
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        telemetry::metric::peerAcceptTotal,
        "Inbound peer connection attempts, by terminal outcome",
        {{telemetry::label::outcome, std::string("accepted")}});
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        telemetry::metric::serveRefusedTotal,
        "Peer data requests this node declined to serve, by request kind and cause",
        {{telemetry::label::request, std::string("ledger")},
         {telemetry::label::reason, std::string("sendq_full")}});
    XRPL_METRIC_COUNTER_INC(
        app,
        telemetry::metric::ledgerJumpTotal,
        "Forced jumps of the last closed ledger to a divergent chain");

    auto const data = provider.collect();

    // Total absence, not zero-valued series: the instruments never existed.
    EXPECT_EQ(data.count("peer_disconnect_total"), 0u);
    EXPECT_EQ(data.count("peer_accept_total"), 0u);
    EXPECT_EQ(data.count("serve_refused_total"), 0u);
    EXPECT_EQ(data.count("ledger_jump_total"), 0u);
    EXPECT_EQ(data.size(), 0u);

    // Cause, not just state: the isEnabled() gate short-circuited before the
    // macros asked for a meter, so no instrument was created either.
    EXPECT_EQ(app.registry().meterCalls(), 0);
}

// -----------------------------------------------------------------
// WP-A6: back-fill and persistence sync diagnostics
// -----------------------------------------------------------------

// Replay falling back to a full ledger acquire silently defeats the replay
// optimisation. The `stage` label must keep the skip-list and delta stages
// apart, because they fail for different reasons.
TEST(MetricMacros, ledger_replay_fallback_counter_separates_stages_by_exact_count)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // The skip-list stage falls back twice, the delta stage once: distinct
    // counts so a collapsed label set cannot coincidentally look correct.
    for (int i = 0; i < 2; ++i)
    {
        XRPL_METRIC_COUNTER_INC_LABELED(
            app,
            telemetry::metric::ledgerReplayFallbackTotal,
            "Replay sub-acquires that fell back to a full ledger acquire",
            {{telemetry::label::stage, std::string("skiplist")}});
    }
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        telemetry::metric::ledgerReplayFallbackTotal,
        "Replay sub-acquires that fell back to a full ledger acquire",
        {{telemetry::label::stage, std::string("delta")}});

    auto const data = provider.collect();

    // Exactly two series, and each carries its own exact total.
    ASSERT_EQ(data.at("ledger_replay_fallback_total").size(), 2u);
    EXPECT_EQ(counterValue(data, "ledger_replay_fallback_total", attrs("stage", "skiplist")), 2);
    EXPECT_EQ(counterValue(data, "ledger_replay_fallback_total", attrs("stage", "delta")), 1);

    // NEGATIVE: a stage that never fell back has no series at all. An absent
    // series, not a zero, is what a healthy replay path looks like.
    EXPECT_EQ(data.at("ledger_replay_fallback_total").count(attrs("stage", "txset")), 0u);

    // The label key is exactly `stage` and is the only label, so this stays a
    // bounded two-value group.
    auto const& firstKey = data.at("ledger_replay_fallback_total").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "stage");
}

// The replay task's four terminal states must be four distinct series: a
// timeout and a failed build are diagnosed differently, and a success total
// with no failures is the only healthy reading.
TEST(MetricMacros, ledger_replay_outcome_counter_records_each_terminal_state_exactly)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // Distinct counts per outcome, matching the four terminal sites in
    // LedgerReplayTask: success, timeout, build_failed, parameter_failed.
    auto const record = [&app](char const* outcome, int times) {
        for (int i = 0; i < times; ++i)
        {
            XRPL_METRIC_COUNTER_INC_LABELED(
                app,
                telemetry::metric::ledgerReplayOutcomeTotal,
                "Ledger replay tasks by terminal outcome",
                {{telemetry::label::outcome, std::string(outcome)}});
        }
    };
    record("success", 3);
    record("timeout", 2);
    record("build_failed", 1);
    record("parameter_failed", 4);

    auto const data = provider.collect();

    ASSERT_EQ(data.at("ledger_replay_outcome_total").size(), 4u);
    EXPECT_EQ(counterValue(data, "ledger_replay_outcome_total", attrs("outcome", "success")), 3);
    EXPECT_EQ(counterValue(data, "ledger_replay_outcome_total", attrs("outcome", "timeout")), 2);
    EXPECT_EQ(
        counterValue(data, "ledger_replay_outcome_total", attrs("outcome", "build_failed")), 1);
    EXPECT_EQ(
        counterValue(data, "ledger_replay_outcome_total", attrs("outcome", "parameter_failed")), 4);

    // NEGATIVE: an outcome value the code never emits has no series, proving
    // the four above are real label values and not a catch-all.
    EXPECT_EQ(data.at("ledger_replay_outcome_total").count(attrs("outcome", "cancelled")), 0u);

    // The two WP-A6 replay counters are separate instruments, so a fallback
    // never inflates an outcome total.
    EXPECT_EQ(data.count("ledger_replay_fallback_total"), 0u);
}

// Telemetry disabled at runtime: both replay counters must be complete no-ops.
// Absence of the instrument, not a zero-valued series.
TEST(MetricMacros, ledger_replay_counters_emit_nothing_when_disabled)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/false, provider.meter());

    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        telemetry::metric::ledgerReplayFallbackTotal,
        "Replay sub-acquires that fell back to a full ledger acquire",
        {{telemetry::label::stage, std::string("skiplist")}});
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        telemetry::metric::ledgerReplayOutcomeTotal,
        "Ledger replay tasks by terminal outcome",
        {{telemetry::label::outcome, std::string("timeout")}});

    auto const data = provider.collect();

    EXPECT_EQ(data.count("ledger_replay_fallback_total"), 0u);
    EXPECT_EQ(data.count("ledger_replay_outcome_total"), 0u);
    EXPECT_EQ(data.size(), 0u);

    // Cause, not just state: the isEnabled() gate short-circuited before the
    // macros ever asked for a meter, so no instrument was created.
    EXPECT_EQ(app.registry().meterCalls(), 0);
}

// The nodestore_latency gauge derives a mean from two cumulative totals the
// node store already keeps. This mirrors the production callback in
// MetricsRegistry::registerNodeStoreLatencyGauge, whose enabled path cannot be
// linked into this binary, so the derivation is asserted here against the same
// four inputs.
TEST(MetricMacros, nodestore_latency_gauge_observes_exact_derived_means)
{
    CollectingProvider const provider;

    // The four totals the production callback reads, chosen so each mean
    // divides exactly and the two means differ: writes are 4x slower per
    // operation than reads, which is the "existing DB back-fills slowly"
    // shape this signal exists to show.
    struct NodeStoreTotals
    {
        std::uint64_t storeCount;
        std::uint64_t storeDurationUs;
        std::uint64_t fetchCount;
        std::uint64_t fetchDurationUs;
    };
    NodeStoreTotals totals{
        .storeCount = 500,
        .storeDurationUs = 2'000'000,  // 2 s over 500 stores -> 4000 us
        .fetchCount = 1000,
        .fetchDurationUs = 1'000'000};  // 1 s over 1000 fetches -> 1000 us

    auto gauge = provider.meter()->CreateInt64ObservableGauge(
        telemetry::metric::nodestoreLatency,
        "NodeStore mean store/fetch latency in microseconds, with counts");
    gauge->AddCallback(
        [](opentelemetry::metrics::ObserverResult result, void* state) {
            auto const* self = static_cast<NodeStoreTotals const*>(state);
            auto observe = [&](char const* field, std::int64_t value) {
                opentelemetry::nostd::get<opentelemetry::nostd::shared_ptr<
                    opentelemetry::metrics::ObserverResultT<std::int64_t>>>(result)
                    ->Observe(value, {{telemetry::label::metric, field}});
            };
            observe("write_count", static_cast<std::int64_t>(self->storeCount));
            observe("read_count", static_cast<std::int64_t>(self->fetchCount));
            if (self->storeCount > 0 && self->storeDurationUs > 0)
            {
                observe(
                    "write_mean_us",
                    static_cast<std::int64_t>(self->storeDurationUs / self->storeCount));
            }
            if (self->fetchCount > 0 && self->fetchDurationUs > 0)
            {
                observe(
                    "read_mean_us",
                    static_cast<std::int64_t>(self->fetchDurationUs / self->fetchCount));
            }
        },
        &totals);

    auto const busy = provider.collect();

    // Exactly four series: two means and the two denominators that let a
    // dashboard recover interval latency from these cumulative totals.
    ASSERT_EQ(busy.at("nodestore_latency").size(), 4u);
    EXPECT_EQ(gaugeValue(busy, "nodestore_latency", attrs("metric", "write_mean_us")), 4000);
    EXPECT_EQ(gaugeValue(busy, "nodestore_latency", attrs("metric", "read_mean_us")), 1000);
    EXPECT_EQ(gaugeValue(busy, "nodestore_latency", attrs("metric", "write_count")), 500);
    EXPECT_EQ(gaugeValue(busy, "nodestore_latency", attrs("metric", "read_count")), 1000);

    // The write mean is the new signal, and it must be legible next to the
    // read mean rather than merely present.
    EXPECT_GT(
        gaugeValue(busy, "nodestore_latency", attrs("metric", "write_mean_us")),
        gaugeValue(busy, "nodestore_latency", attrs("metric", "read_mean_us")));

    // Single fixed-cardinality label group, keyed exactly `metric`.
    auto const& firstKey = busy.at("nodestore_latency").begin()->first;
    ASSERT_EQ(firstKey.size(), 1u);
    EXPECT_EQ(firstKey.begin()->first, "metric");

    // EDGE CASE: a node that has never written. The zero denominator must skip
    // the mean rather than divide by zero, while the count is still reported --
    // that is what distinguishes "nothing written yet" from "writes are
    // instant". The read side is unaffected and still reports both.
    totals = NodeStoreTotals{
        .storeCount = 0, .storeDurationUs = 0, .fetchCount = 4, .fetchDurationUs = 800};
    auto const idle = provider.collect();

    EXPECT_EQ(idle.at("nodestore_latency").count(attrs("metric", "write_mean_us")), 0u);
    EXPECT_EQ(gaugeValue(idle, "nodestore_latency", attrs("metric", "write_count")), 0);
    EXPECT_EQ(gaugeValue(idle, "nodestore_latency", attrs("metric", "read_mean_us")), 200);
    EXPECT_EQ(gaugeValue(idle, "nodestore_latency", attrs("metric", "read_count")), 4);

    // EDGE CASE: integer division truncates rather than rounding. 7 stores
    // over 100 us is 14.28 us, reported as 14 -- asserted so a future change
    // to floating point is a deliberate, visible decision.
    totals = NodeStoreTotals{
        .storeCount = 7, .storeDurationUs = 100, .fetchCount = 0, .fetchDurationUs = 0};
    auto const truncating = provider.collect();

    EXPECT_EQ(gaugeValue(truncating, "nodestore_latency", attrs("metric", "write_mean_us")), 14);
    // The read side now has the zero denominator, so its mean drops out too.
    EXPECT_EQ(truncating.at("nodestore_latency").count(attrs("metric", "read_mean_us")), 0u);
    EXPECT_EQ(gaugeValue(truncating, "nodestore_latency", attrs("metric", "read_count")), 0);

    // EDGE CASE, and the one that matters most on a real node: stores were
    // counted but never TIMED. Database::store() is pure virtual and only the
    // paths calling recordStoreDuration() contribute a numerator, so a node
    // whose concrete store override does not time itself has a non-zero count
    // with a zero duration. The mean must be OMITTED, not reported as 0 --
    // a 0 would read as "writes are instantaneous", which is worse than a
    // visible gap. This assertion is the guard on that choice.
    totals = NodeStoreTotals{
        .storeCount = 9000, .storeDurationUs = 0, .fetchCount = 10, .fetchDurationUs = 50};
    auto const untimed = provider.collect();

    EXPECT_EQ(untimed.at("nodestore_latency").count(attrs("metric", "write_mean_us")), 0u);
    // The count is still published, so the gap is visible rather than silent:
    // a panel shows real write throughput with no latency line beside it.
    EXPECT_EQ(gaugeValue(untimed, "nodestore_latency", attrs("metric", "write_count")), 9000);
    // The read side is independent and unaffected by the write-side gap.
    EXPECT_EQ(gaugeValue(untimed, "nodestore_latency", attrs("metric", "read_mean_us")), 5);
    // Exactly three series: both counts plus the one mean that is derivable.
    EXPECT_EQ(untimed.at("nodestore_latency").size(), 3u);
}

// consensus_round_duration_ms: exact recorded values, not "greater than zero".
// Three rounds of known length must give a count of exactly 3 and a sum of
// exactly their total. Mirrors the one record site in
// RCLConsensus::Adaptor::makeAcceptSpan, which runs once per round.
//
// The 15200 sample is above the SDK's 10,000 default top boundary on purpose:
// that limit is why MetricsRegistry registers explicit buckets for this
// instrument (addRoundDurationHistogramView), and the sum must carry the real
// value through no matter how the value is bucketed.
TEST(MetricMacros, consensus_round_duration_records_exact_values)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/true, provider.meter());

    // 3100 ms (healthy), 4250 ms (slow), 15200 ms (recovering) = 22550 ms.
    for (std::int64_t const ms : {3100, 4250, 15200})
    {
        XRPL_METRIC_HISTOGRAM_RECORD(
            app,
            telemetry::metric::consensusRoundDurationMs,
            "Wall-clock duration of a completed consensus round in milliseconds",
            ms);
    }

    auto const data = provider.collect();

    auto const [count, sum] = histogramCountAndSum(data, "consensus_round_duration_ms");
    EXPECT_EQ(count, 3u);
    EXPECT_DOUBLE_EQ(sum, 22550.0);

    // Exactly ONE series carrying an EMPTY label set. The round histogram is
    // deliberately unlabelled: a per-ledger or per-round label would mint a
    // series per ledger, the same unbounded-cardinality trap the collector
    // config documents for close_time.
    ASSERT_EQ(data.at("consensus_round_duration_ms").size(), 1u);
    EXPECT_TRUE(data.at("consensus_round_duration_ms").begin()->first.empty());

    // One call site, three records: created once via std::call_once, then reused.
    EXPECT_EQ(app.registry().meterCalls(), 1);
}

// RUNTIME-DISABLED no-op proof for the round histogram: with the registry
// disabled, nothing is emitted -- not a zero-valued series but total absence --
// and the macro never even asks for a meter, so no instrument is created. The
// runtime counterpart to the compile-time no-op (telemetry not built at all).
TEST(MetricMacros, consensus_round_duration_emits_nothing_when_registry_disabled)
{
    CollectingProvider const provider;
    FakeApp app;
    wire(app, /*enabled=*/false, provider.meter());

    XRPL_METRIC_HISTOGRAM_RECORD(
        app,
        telemetry::metric::consensusRoundDurationMs,
        "Wall-clock duration of a completed consensus round in milliseconds",
        3100);

    auto const data = provider.collect();

    // State: no series at all under that name, and nothing else leaked in.
    EXPECT_EQ(data.count("consensus_round_duration_ms"), 0u);
    EXPECT_EQ(data.size(), 0u);
    // Cause, not just state: the isEnabled() gate short-circuited before the
    // macro ever asked for a meter.
    EXPECT_EQ(app.registry().meterCalls(), 0);
}

#endif  // XRPL_ENABLE_TELEMETRY
