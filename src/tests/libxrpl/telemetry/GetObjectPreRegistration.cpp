/**
 * @file GetObjectPreRegistration.cpp
 * Unit tests for preRegisterGetObjectCounters().
 *
 * ApplicationImp::setup() calls it to create getobject_rejected_total at 0 for
 * each refusal reason. These tests make the same call on a fake app and read
 * the points back through a real SDK provider.
 *
 * @code
 *   GetObjectFakeApp --getMetricsRegistry()--> GetObjectFakeRegistry
 *                                                  |
 *                                                  | meter()
 *                                                  v
 *   GetObjectPipeline:  MeterProvider --> ManualMetricReader --> collect()
 * @endcode
 *
 * Compiled only when XRPL_ENABLE_TELEMETRY is defined: the macros are no-ops
 * otherwise, and the SDK is not on the link line.
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/GetObjectMetricNames.h>
#include <xrpl/telemetry/MetricMacros.h>
#include <xrpl/telemetry/PreRegisteredCounters.h>

#include <gtest/gtest.h>
#include <helpers/CollectedCounters.h>
#include <helpers/ManualMetricReader.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

namespace {

/**
 * Duck-typed stand-in for telemetry::MetricsRegistry. The macros read only
 * recording() and meter() from it.
 */
class GetObjectFakeRegistry
{
public:
    /**
     * @param meter The meter the macros create their instruments on.
     */
    explicit GetObjectFakeRegistry(
        opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter)
        : meter_(std::move(meter))
    {
    }

    /**
     * @return Always true: these tests run with a live pipeline.
     */
    [[nodiscard]] static bool
    recording() noexcept
    {
        return true;
    }

    /**
     * @return The meter given at construction.
     */
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
    meter() const noexcept
    {
        return meter_;
    }

private:
    /**
     * The meter handed to the macros.
     */
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter_;
};

/**
 * Duck-typed stand-in for ServiceRegistry. The macros read only
 * getMetricsRegistry() from it.
 */
class GetObjectFakeApp
{
public:
    /**
     * @param meter The meter the registry hands to the macros.
     */
    explicit GetObjectFakeApp(opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter> meter)
        : registry_(std::move(meter))
    {
    }

    /**
     * @return The fake registry.
     */
    [[nodiscard]] GetObjectFakeRegistry*
    getMetricsRegistry() noexcept
    {
        return &registry_;
    }

private:
    /**
     * The registry the macros read.
     */
    GetObjectFakeRegistry registry_;
};

/**
 * A real SDK provider with one reader that collects only when asked.
 *
 * The reader is attached before any instrument exists, so it sees every
 * point, including the zeros.
 */
class GetObjectPipeline
{
public:
    GetObjectPipeline()
    {
        provider_->AddMetricReader(reader_);
    }

    /**
     * @return A meter whose instruments feed the reader.
     */
    [[nodiscard]] opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>
    meter() const
    {
        return provider_->GetMeter("xrpld_test", "1.0.0");
    }

    /**
     * @return Every counter one collection saw, by name.
     */
    [[nodiscard]] std::map<std::string, xrpl::test::CollectedCounter>
    collect() const
    {
        return xrpl::test::collectCounters(*reader_);
    }

private:
    /**
     * The on-demand reader, shared with the provider.
     */
    std::shared_ptr<xrpl::test::ManualMetricReader> reader_ =
        std::make_shared<xrpl::test::ManualMetricReader>();

    /**
     * The provider that owns the storage the reader collects from.
     */
    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> provider_ =
        opentelemetry::sdk::metrics::MeterProviderFactory::Create();
};

}  // namespace

// Both refusal reasons exist at 0 before any request is refused, on one stream.
// Without the pre-registration call, collect() has no entry for the counter.
TEST(GetObjectPreRegistration, both_refusal_reasons_start_at_zero)
{
    GetObjectPipeline const pipeline;
    GetObjectFakeApp app{pipeline.meter()};

    xrpl::telemetry::preRegisterGetObjectCounters(app);

    using namespace xrpl::telemetry;
    xrpl::test::CollectedCounter const expected{
        .streams = 1,
        .points = {
            {xrpl::test::CounterLabels{{kLabelReason, kReasonOversize}}, 0},
            {xrpl::test::CounterLabels{{kLabelReason, kReasonMalformedLedgerHash}}, 0}}};
    auto const counters = pipeline.collect();
    ASSERT_EQ(counters.count(kGetObjectRejectedTotal), 1u);
    EXPECT_EQ(counters.at(kGetObjectRejectedTotal), expected);
}

// A refusal recorded the way PeerImp records one lands on its zero series:
// it reads 1 there, the other reason stays 0, and no second stream appears,
// because both calls pass the same name and description.
TEST(GetObjectPreRegistration, a_refusal_lands_on_its_zero_series)
{
    GetObjectPipeline const pipeline;
    GetObjectFakeApp app{pipeline.meter()};

    xrpl::telemetry::preRegisterGetObjectCounters(app);
    using namespace xrpl::telemetry;
    XRPL_METRIC_COUNTER_INC_LABELED(
        app,
        kGetObjectRejectedTotal,
        kGetObjectRejectedTotalDesc,
        {{kLabelReason, std::string(kReasonOversize)}});

    xrpl::test::CollectedCounter const expected{
        .streams = 1,
        .points = {
            {xrpl::test::CounterLabels{{kLabelReason, kReasonOversize}}, 1},
            {xrpl::test::CounterLabels{{kLabelReason, kReasonMalformedLedgerHash}}, 0}}};
    auto const counters = pipeline.collect();
    ASSERT_EQ(counters.count(kGetObjectRejectedTotal), 1u);
    EXPECT_EQ(counters.at(kGetObjectRejectedTotal), expected);
}

#endif  // XRPL_ENABLE_TELEMETRY
