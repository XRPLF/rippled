/**
 * @file GetMeter.cpp
 * Unit tests for the direct OTel metrics API surface:
 * - xrpl::telemetry::Telemetry::getMeter() on the disabled (noop) path.
 * - The global MeterProvider contract that beast's OTelCollector shim
 * relies on (GetMeterProvider()->GetMeter(kMeterName, kMeterVersion)).
 *
 * These tests deliberately exercise the global-provider level rather than a
 * fully started TelemetryImpl: TelemetryImpl::start() spins up an OTLP HTTP
 * exporter with background export threads and network connect attempts, which
 * is unsuitable for a hermetic unit test. The design spec permits testing the
 * metrics unlock at the provider level, which is the exact path the beast shim
 * and getMeter() delegate to.
 *
 * Compiled only when XRPL_ENABLE_TELEMETRY is defined; the metrics API and SDK
 * headers exist only in that build.
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/telemetry/Telemetry.h>

#include <gtest/gtest.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>

#include <cstdint>
#include <memory>
#include <string>

using namespace xrpl;
using namespace xrpl::telemetry;

namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;

// ---------------------------------------------------------------
// 1. Disabled path: Telemetry::getMeter() returns a usable noop meter.
//    makeTelemetry() with enabled=false yields NullTelemetryOtel, whose
//    getMeter() serves a meter from a process-wide NoopMeterProvider. The
//    meter and any instrument built from it must be non-null, and Add()
//    (positive and negative) must be inert but safe.
// ---------------------------------------------------------------
TEST(GetMeter, disabled_path_returns_usable_noop_meter)
{
    Telemetry::Setup setup;
    setup.enabled = false;

    beast::Journal::Sink& sink = beast::Journal::getNullSink();
    beast::Journal const journal(sink);
    auto telemetry = makeTelemetry(setup, journal);
    ASSERT_NE(telemetry, nullptr);

    // getMeter() must return a concrete (non-null) meter, never a raw nullptr.
    auto meter = telemetry->getMeter();
    ASSERT_TRUE(static_cast<bool>(meter));

    // A synchronous UpDownCounter — one of the three instrument kinds the
    // direct API unlocks — must be creatable and non-null.
    auto upDown = meter->CreateInt64UpDownCounter("test_open_ledgers");
    ASSERT_TRUE(static_cast<bool>(upDown));

    // Both increment and decrement paths must execute without error (inert on
    // the noop instrument, but exercised here to prove both directions work).
    upDown->Add(static_cast<int64_t>(1));
    upDown->Add(static_cast<int64_t>(-1));
}

// ---------------------------------------------------------------
// 2. Global-provider contract (the beast OTelCollector shim path).
//    After a real SDK MeterProvider is registered via SetMeterProvider(),
//    GetMeterProvider()->GetMeter(kMeterName, kMeterVersion) — the exact call
//    the shim makes — must return a non-null meter, and an UpDownCounter built
//    from it must be non-null and accept positive and negative Add().
// ---------------------------------------------------------------
TEST(GetMeter, global_provider_meter_accepts_updown_counter)
{
    // Preserve and later restore the process-wide provider so this test does
    // not leak state into other telemetry tests in the same binary.
    auto const previous = metrics_api::Provider::GetMeterProvider();

    // A views-less SDK MeterProvider with no reader is sufficient to prove the
    // API contract: it hands out a real (non-noop) Meter that creates working
    // instruments. No exporter/reader means no background threads or network.
    std::shared_ptr<metrics_sdk::MeterProvider> const sdkProvider =
        metrics_sdk::MeterProviderFactory::Create();
    metrics_api::Provider::SetMeterProvider(
        opentelemetry::nostd::shared_ptr<metrics_api::MeterProvider>(sdkProvider));

    // Fetch the meter exactly as the beast shim does: kMeterName + kMeterVersion
    // from the global provider.
    auto meter = metrics_api::Provider::GetMeterProvider()->GetMeter(
        std::string(kMeterName), std::string(kMeterVersion));
    ASSERT_TRUE(static_cast<bool>(meter));

    auto upDown = meter->CreateInt64UpDownCounter("test_in_flight_requests");
    ASSERT_TRUE(static_cast<bool>(upDown));

    // Positive path: value goes up.
    upDown->Add(static_cast<int64_t>(1));
    // Negative path: UpDownCounter permits decrement (unlike a plain Counter).
    upDown->Add(static_cast<int64_t>(-1));

    // Restore the previous global provider.
    metrics_api::Provider::SetMeterProvider(previous);
}

#endif  // XRPL_ENABLE_TELEMETRY
