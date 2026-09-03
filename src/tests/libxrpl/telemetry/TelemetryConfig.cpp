#include <xrpl/basics/FileUtilities.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/telemetry/Telemetry.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <string>

using namespace xrpl;

using ::testing::AllOf;
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
 * clientCert and clientKey are the paths written to those keys. They name files
 * that do not exist, so they suit only the cases the readability check cannot
 * reach: telemetry off, or use_tls off. A case with enabled=1 and use_tls=1
 * must write real files with writeCertFile() below instead. They are
 * declared as `char const*` so they pass to Section::set() (which takes
 * `std::string const&`) and compare against the parsed std::string members
 * without an explicit conversion, exactly as a literal would.
 *
 * pairingError, useTlsError and readError are message fragments. All three
 * guards throw std::runtime_error, so the exception type alone cannot tell
 * them apart. Each fragment occurs in exactly one of the three messages, so
 * matching it proves which guard fired.
 */
namespace mtls {
constexpr char const* keyClientCert = "tls_client_cert";
constexpr char const* keyClientKey = "tls_client_key";
constexpr char const* clientCert = "/etc/ssl/client.pem";
constexpr char const* clientKey = "/etc/ssl/client.key";
constexpr char const* pairingError = "must be set together";
constexpr char const* useTlsError = "require use_tls=1";
constexpr char const* readError = "cannot be read";

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

/**
 * Write a placeholder certificate file at the given path.
 *
 * The parser only needs the file to exist and be readable, so the contents are
 * irrelevant — nothing checks that they parse as PEM. The stream state is
 * asserted, so a failed write shows up as a setup failure here rather than as a
 * confusing failure in the case under test.
 *
 * @param path  Where to write the file, typically from TempDir::file().
 * @return The same path, ready to pass to Section::set().
 */
std::string
writeCertFile(std::string const& path)
{
    std::ofstream out{path};
    out << "placeholder\n";
    out.close();
    EXPECT_TRUE(out.good()) << "could not create " << path;
    return path;
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
    EXPECT_EQ(s.tracesEndpoint, "http://localhost:4318/v1/traces");
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
    // The CA path has to name a real file: with enabled=1 and use_tls=1 the
    // parser opens it, so a placeholder path would make this case throw.
    TempDir const dir;
    auto const caCert = mtls::writeCertFile(dir.file("ca.pem"));
    Section section;
    section.set("enabled", "1");
    section.set("service_name", "my-rippled");
    section.set("service_instance_id", "custom-id");
    section.set("exporter", "otlp_http");
    section.set("traces_endpoint", "http://collector:4318/v1/traces");
    section.set("use_tls", "1");
    section.set("tls_ca_cert", caCert);
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
    EXPECT_EQ(setup.tracesEndpoint, "http://collector:4318/v1/traces");
    EXPECT_TRUE(setup.useTls);
    EXPECT_EQ(setup.tlsCertPath, caCert);
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
    // Telemetry on and use_tls=1, so all three checks run and none may fire.
    // Both paths have to name real files, because the parser opens them here.
    // No CA bundle is set, which is the case this covers: mTLS against a
    // collector whose certificate the system CA store already vouches for.
    TempDir const dir;
    auto const cert = mtls::writeCertFile(dir.file("client.pem"));
    auto const key = mtls::writeCertFile(dir.file("client.key"));
    Section section = mtls::makeSection(true);
    section.set("use_tls", "1");
    section.set(mtls::keyClientCert, cert);
    section.set(mtls::keyClientKey, key);

    auto const setup = mtls::parseSection(section);
    EXPECT_TRUE(setup.enabled);
    EXPECT_TRUE(setup.useTls);
    EXPECT_TRUE(setup.tlsCertPath.empty());
    EXPECT_EQ(setup.tlsClientCertPath, cert);
    EXPECT_EQ(setup.tlsClientKeyPath, key);
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
    // Telemetry is on so the checks run, and this config must pass all of
    // them: one-way TLS with a CA bundle and no client certificate. The CA
    // path has to name a real file, because the parser opens it here.
    TempDir const dir;
    auto const caCert = mtls::writeCertFile(dir.file("ca.pem"));
    Section section = mtls::makeSection(true);
    section.set("use_tls", "1");
    section.set("tls_ca_cert", caCert);

    auto const setup = mtls::parseSection(section);
    EXPECT_TRUE(setup.enabled);
    EXPECT_TRUE(setup.useTls);
    EXPECT_EQ(setup.tlsCertPath, caCert);
    EXPECT_TRUE(setup.tlsClientCertPath.empty());
    EXPECT_TRUE(setup.tlsClientKeyPath.empty());
}

TEST(TelemetryConfig, tls_missing_client_cert_file_throws)
{
    // Both client paths are set and use_tls=1, so neither contradiction guard
    // can fire and the readability check is the only reachable throw. Only the
    // certificate is absent, so the message must name that key and that path.
    //
    // This case and the two below use an absent file. A file that exists but
    // denies read permission is deliberately not covered: a test process
    // running as root reads it anyway, so the case would not be reliable.
    TempDir const dir;
    auto const absentCert = dir.file("absent.pem");
    Section section = mtls::makeSection(true);
    section.set("use_tls", "1");
    section.set(mtls::keyClientCert, absentCert);
    section.set(mtls::keyClientKey, mtls::writeCertFile(dir.file("k.pem")));

    EXPECT_THAT(
        [&section] { mtls::parseSection(section); },
        ThrowsMessage<std::runtime_error>(AllOf(
            HasSubstr(mtls::readError), HasSubstr(mtls::keyClientCert), HasSubstr(absentCert))));
}

TEST(TelemetryConfig, tls_missing_client_key_file_throws)
{
    // The mirror image of the case above: the certificate is readable and only
    // the private key is absent, so the key's name must appear instead.
    TempDir const dir;
    auto const absentKey = dir.file("absent.key");
    Section section = mtls::makeSection(true);
    section.set("use_tls", "1");
    section.set(mtls::keyClientCert, mtls::writeCertFile(dir.file("c.pem")));
    section.set(mtls::keyClientKey, absentKey);

    EXPECT_THAT(
        [&section] { mtls::parseSection(section); },
        ThrowsMessage<std::runtime_error>(AllOf(
            HasSubstr(mtls::readError), HasSubstr(mtls::keyClientKey), HasSubstr(absentKey))));
}

TEST(TelemetryConfig, tls_missing_ca_cert_file_throws)
{
    // One-way TLS with no client certificate, so the CA bundle is the only
    // path checked.
    TempDir const dir;
    auto const absentCa = dir.file("absent-ca.pem");
    Section section = mtls::makeSection(true);
    section.set("use_tls", "1");
    section.set("tls_ca_cert", absentCa);

    EXPECT_THAT(
        [&section] { mtls::parseSection(section); },
        ThrowsMessage<std::runtime_error>(
            AllOf(HasSubstr(mtls::readError), HasSubstr("tls_ca_cert"), HasSubstr(absentCa))));
}

TEST(TelemetryConfig, tls_readable_files_are_accepted)
{
    // Full mTLS with all three files present and readable: parsing must
    // succeed and keep every path verbatim.
    TempDir const dir;
    auto const ca = mtls::writeCertFile(dir.file("ca.pem"));
    auto const cert = mtls::writeCertFile(dir.file("c.pem"));
    auto const key = mtls::writeCertFile(dir.file("k.pem"));
    Section section = mtls::makeSection(true);
    section.set("use_tls", "1");
    section.set("tls_ca_cert", ca);
    section.set(mtls::keyClientCert, cert);
    section.set(mtls::keyClientKey, key);

    telemetry::Telemetry::Setup setup;
    ASSERT_NO_THROW(setup = mtls::parseSection(section));
    EXPECT_TRUE(setup.enabled);
    EXPECT_TRUE(setup.useTls);
    EXPECT_EQ(setup.tlsCertPath, ca);
    EXPECT_EQ(setup.tlsClientCertPath, cert);
    EXPECT_EQ(setup.tlsClientKeyPath, key);
}

TEST(TelemetryConfig, tls_paths_not_checked_when_telemetry_disabled)
{
    // Telemetry off, so the files are never opened and absent paths must not
    // stop the node from booting. use_tls stays 1 here, so the `enabled` gate
    // is the only thing that can be suppressing the check.
    TempDir const dir;
    auto const absentCert = dir.file("absent.pem");
    auto const absentKey = dir.file("absent.key");
    Section section = mtls::makeSection(false);
    section.set("use_tls", "1");
    section.set(mtls::keyClientCert, absentCert);
    section.set(mtls::keyClientKey, absentKey);

    telemetry::Telemetry::Setup setup;
    ASSERT_NO_THROW(setup = mtls::parseSection(section));
    EXPECT_FALSE(setup.enabled);
    EXPECT_TRUE(setup.useTls);
    EXPECT_EQ(setup.tlsClientCertPath, absentCert);
    EXPECT_EQ(setup.tlsClientKeyPath, absentKey);
}

TEST(TelemetryConfig, tls_ca_cert_not_checked_when_use_tls_off)
{
    // With TLS off the exporter never reads the CA path, so a missing file
    // must not stop startup. Telemetry stays on here, so the use_tls gate is
    // the only thing that can be suppressing the check. The client-cert keys
    // cannot be used for this case: they trip the use_tls contradiction guard
    // before any file is opened.
    TempDir const dir;
    auto const absentCa = dir.file("absent-ca.pem");
    Section section = mtls::makeSection(true);
    section.set("tls_ca_cert", absentCa);

    telemetry::Telemetry::Setup setup;
    ASSERT_NO_THROW(setup = mtls::parseSection(section));
    EXPECT_TRUE(setup.enabled);
    EXPECT_FALSE(setup.useTls);
    EXPECT_EQ(setup.tlsCertPath, absentCa);
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
