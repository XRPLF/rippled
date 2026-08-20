#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/telemetry/Telemetry.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <stdexcept>

using namespace xrpl;

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;

namespace {

/**
 * Shared inputs for the mutual TLS (mTLS) tests of makeTelemetrySetup().
 *
 * keyClientCert and keyClientKey are the config key names, named once so every
 * test below spells them the same way, mirroring the `key::` constants the
 * parser itself uses. A misspelling cannot hide here: the throwing case that
 * names the misspelled key stops throwing, the use_tls case throws the pairing
 * message instead and fails its matcher, and the value cases see an empty path
 * or an unexpected throw. Tests that never set the key are unaffected. One
 * source of truth still keeps the two files from drifting apart.
 *
 * clientCert and clientKey are the paths written to those keys. They are
 * declared as `char const*` so they pass to Section::set() (which takes
 * `std::string const&`) and compare against the parsed std::string members
 * without an explicit conversion, exactly as a literal would.
 *
 * pairingError and useTlsError are message fragments. Both guards throw
 * std::runtime_error, so the exception type alone cannot tell them apart.
 * Each fragment occurs in exactly one of the two messages, so matching it
 * proves which guard fired.
 */
namespace mtls {
constexpr char const* keyClientCert = "tls_client_cert";
constexpr char const* keyClientKey = "tls_client_key";
constexpr char const* clientCert = "/etc/ssl/client.pem";
constexpr char const* clientKey = "/etc/ssl/client.key";
constexpr char const* pairingError = "must be set together";
constexpr char const* useTlsError = "require use_tls=1";

/**
 * Build a [telemetry] section carrying only the `enabled` key.
 *
 * Every mTLS test states `enabled` explicitly, because the validation
 * guards run only when telemetry is on. Each test then adds the TLS keys its
 * own case needs on top of the returned section.
 *
 * @param telemetryEnabled  Value written to the `enabled` key.
 * @return The section, ready for further set() calls.
 */
Section
makeSection(bool telemetryEnabled)
{
    Section section;
    section.set("enabled", telemetryEnabled ? "1" : "0");
    return section;
}

/**
 * Parse a [telemetry] section with a fixed placeholder node identity.
 *
 * Keeps the node key, version and network ID out of the individual cases,
 * which vary only in their TLS keys.
 *
 * @param section  The section to parse.
 * @return The populated Setup struct.
 */
telemetry::Telemetry::Setup
parseSection(Section const& section)
{
    return telemetry::makeTelemetrySetup(section, "nHUtest123", "2.0.0", 0);
}
}  // namespace mtls

}  // namespace

TEST(TelemetryConfig, setup_defaults)
{
    telemetry::Telemetry::Setup const s;
    EXPECT_FALSE(s.enabled);
    EXPECT_EQ(s.serviceName, "xrpld");
    EXPECT_TRUE(s.serviceVersion.empty());
    EXPECT_TRUE(s.serviceInstanceId.empty());
    EXPECT_EQ(s.exporterEndpoint, "http://localhost:4318/v1/traces");
    EXPECT_FALSE(s.useTls);
    EXPECT_TRUE(s.tlsCertPath.empty());
    EXPECT_DOUBLE_EQ(s.samplingRatio, 1.0);
    EXPECT_EQ(s.batchSize, 512u);
    EXPECT_EQ(s.batchDelay, std::chrono::milliseconds{5000});
    EXPECT_EQ(s.maxQueueSize, 2048u);
    EXPECT_EQ(s.networkId, 0u);
    EXPECT_EQ(s.networkType, "mainnet");
    EXPECT_TRUE(s.traceTransactions);
    EXPECT_TRUE(s.traceConsensus);
    EXPECT_TRUE(s.traceRpc);
    EXPECT_TRUE(s.tracePeer);
    EXPECT_TRUE(s.traceLedger);
}

TEST(TelemetryConfig, parse_empty_section)
{
    Section const section;
    auto setup = telemetry::makeTelemetrySetup(section, "nHUtest123", "2.0.0", 0);

    EXPECT_FALSE(setup.enabled);
    EXPECT_EQ(setup.serviceName, "xrpld");
    EXPECT_EQ(setup.serviceVersion, "2.0.0");
    EXPECT_EQ(setup.serviceInstanceId, "nHUtest123");
    EXPECT_DOUBLE_EQ(setup.samplingRatio, 1.0);
    EXPECT_TRUE(setup.traceRpc);
    EXPECT_TRUE(setup.traceTransactions);
    EXPECT_TRUE(setup.traceConsensus);
    EXPECT_TRUE(setup.tracePeer);
    EXPECT_TRUE(setup.traceLedger);
}

TEST(TelemetryConfig, parse_full_section)
{
    Section section;
    section.set("enabled", "1");
    section.set("service_name", "my-rippled");
    section.set("service_instance_id", "custom-id");
    section.set("exporter", "otlp_http");
    section.set("endpoint", "http://collector:4318/v1/traces");
    section.set("use_tls", "1");
    section.set("tls_ca_cert", "/etc/ssl/ca.pem");
    section.set("batch_size", "256");
    section.set("batch_delay_ms", "3000");
    section.set("max_queue_size", "4096");
    section.set("trace_transactions", "0");
    section.set("trace_consensus", "0");
    section.set("trace_rpc", "1");
    section.set("trace_peer", "1");
    section.set("trace_ledger", "0");

    auto setup = telemetry::makeTelemetrySetup(section, "nHUtest123", "2.0.0", 1);

    EXPECT_TRUE(setup.enabled);
    EXPECT_EQ(setup.serviceName, "my-rippled");
    EXPECT_EQ(setup.serviceInstanceId, "custom-id");
    EXPECT_EQ(setup.exporterEndpoint, "http://collector:4318/v1/traces");
    EXPECT_TRUE(setup.useTls);
    EXPECT_EQ(setup.tlsCertPath, "/etc/ssl/ca.pem");
    EXPECT_EQ(setup.batchSize, 256u);
    EXPECT_EQ(setup.batchDelay, std::chrono::milliseconds{3000});
    EXPECT_EQ(setup.maxQueueSize, 4096u);
    EXPECT_FALSE(setup.traceTransactions);
    EXPECT_FALSE(setup.traceConsensus);
    EXPECT_TRUE(setup.traceRpc);
    EXPECT_TRUE(setup.tracePeer);
    EXPECT_FALSE(setup.traceLedger);
}

TEST(TelemetryConfig, mtls_cert_and_key_both_set)
{
    // Telemetry on and use_tls=1, so both guards run and neither may fire.
    Section section = mtls::makeSection(true);
    section.set("use_tls", "1");
    section.set(mtls::keyClientCert, mtls::clientCert);
    section.set(mtls::keyClientKey, mtls::clientKey);

    auto const setup = mtls::parseSection(section);
    EXPECT_TRUE(setup.enabled);
    EXPECT_TRUE(setup.useTls);
    EXPECT_EQ(setup.tlsClientCertPath, mtls::clientCert);
    EXPECT_EQ(setup.tlsClientKeyPath, mtls::clientKey);
}

TEST(TelemetryConfig, mtls_cert_without_key_throws)
{
    // Only the cert is set, so the pairing guard is the one that must fire.
    Section section = mtls::makeSection(true);
    section.set(mtls::keyClientCert, mtls::clientCert);

    EXPECT_THAT(
        [&section] { mtls::parseSection(section); },
        ThrowsMessage<std::runtime_error>(HasSubstr(mtls::pairingError)));
}

TEST(TelemetryConfig, mtls_key_without_cert_throws)
{
    // Only the key is set, the mirror image of the case above.
    Section section = mtls::makeSection(true);
    section.set(mtls::keyClientKey, mtls::clientKey);

    EXPECT_THAT(
        [&section] { mtls::parseSection(section); },
        ThrowsMessage<std::runtime_error>(HasSubstr(mtls::pairingError)));
}

TEST(TelemetryConfig, mtls_cert_key_without_use_tls_throws)
{
    // Both paths are set, so the pairing guard cannot fire; use_tls is absent
    // and defaults to 0, so the use_tls guard is the only reachable throw.
    Section section = mtls::makeSection(true);
    section.set(mtls::keyClientCert, mtls::clientCert);
    section.set(mtls::keyClientKey, mtls::clientKey);

    EXPECT_THAT(
        [&section] { mtls::parseSection(section); },
        ThrowsMessage<std::runtime_error>(HasSubstr(mtls::useTlsError)));
}

TEST(TelemetryConfig, mtls_contradiction_ignored_when_telemetry_disabled)
{
    // The use_tls contradiction with telemetry off: parsing must succeed so a
    // stale cert line cannot stop the node from booting.
    Section section = mtls::makeSection(false);
    section.set(mtls::keyClientCert, mtls::clientCert);
    section.set(mtls::keyClientKey, mtls::clientKey);

    auto const setup = mtls::parseSection(section);
    EXPECT_FALSE(setup.enabled);
    EXPECT_FALSE(setup.useTls);
    EXPECT_EQ(setup.tlsClientCertPath, mtls::clientCert);
    EXPECT_EQ(setup.tlsClientKeyPath, mtls::clientKey);
}

TEST(TelemetryConfig, mtls_cert_without_key_ignored_when_telemetry_disabled)
{
    // The pairing violation with telemetry off: also parsed, not rejected.
    Section section = mtls::makeSection(false);
    section.set(mtls::keyClientCert, mtls::clientCert);

    auto const setup = mtls::parseSection(section);
    EXPECT_FALSE(setup.enabled);
    EXPECT_FALSE(setup.useTls);
    EXPECT_EQ(setup.tlsClientCertPath, mtls::clientCert);
    EXPECT_TRUE(setup.tlsClientKeyPath.empty());
}

TEST(TelemetryConfig, mtls_default_no_client_tls_is_accepted)
{
    // The documented default with telemetry on: no client certificate, and
    // use_tls absent so it defaults to 0. Both guards run and neither may
    // fire. The use_tls guard tests the certificate path first; drop that
    // conjunct and this config is rejected, so no default node could boot.
    Section const section = mtls::makeSection(true);

    telemetry::Telemetry::Setup setup;
    ASSERT_NO_THROW(setup = mtls::parseSection(section));
    EXPECT_TRUE(setup.enabled);
    EXPECT_FALSE(setup.useTls);
    EXPECT_TRUE(setup.tlsClientCertPath.empty());
    EXPECT_TRUE(setup.tlsClientKeyPath.empty());
}

TEST(TelemetryConfig, mtls_neither_set_is_one_way_tls)
{
    // Telemetry is on so the guards run, and this config must pass both:
    // one-way TLS with a CA bundle and no client certificate.
    Section section = mtls::makeSection(true);
    section.set("use_tls", "1");
    section.set("tls_ca_cert", "/etc/ssl/ca.pem");

    auto const setup = mtls::parseSection(section);
    EXPECT_TRUE(setup.enabled);
    EXPECT_TRUE(setup.useTls);
    EXPECT_EQ(setup.tlsCertPath, "/etc/ssl/ca.pem");
    EXPECT_TRUE(setup.tlsClientCertPath.empty());
    EXPECT_TRUE(setup.tlsClientKeyPath.empty());
}

TEST(TelemetryConfig, null_telemetry_factory)
{
    telemetry::Telemetry::Setup setup;
    setup.enabled = false;

    beast::Journal::Sink& sink = beast::Journal::getNullSink();
    beast::Journal const j(sink);
    auto tel = telemetry::makeTelemetry(setup, j);
    EXPECT_TRUE(tel != nullptr);
    EXPECT_FALSE(tel->isEnabled());
    EXPECT_FALSE(tel->shouldTraceRpc());
    EXPECT_FALSE(tel->shouldTraceTransactions());
    EXPECT_FALSE(tel->shouldTraceConsensus());
    EXPECT_FALSE(tel->shouldTracePeer());
    EXPECT_FALSE(tel->shouldTraceLedger());

    // start/stop should be no-ops without crashing
    tel->start();
    tel->stop();
}
