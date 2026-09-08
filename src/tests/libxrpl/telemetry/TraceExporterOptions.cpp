// The whole file is telemetry-only: makeTraceExporterOptions() and the OTel
// exporter options type it returns are both declared behind
// XRPL_ENABLE_TELEMETRY, so without it there is nothing here to test.
#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/telemetry/Telemetry.h>

#include <gtest/gtest.h>

#include <fstream>
#include <string>

using namespace xrpl;

namespace {

/**
 * Distinct placeholder paths for the three TLS files.
 *
 * They are deliberately different from one another in more than a suffix, so a
 * certificate written into the key field, or a CA bundle written into either,
 * shows up as an inequality naming both paths rather than as a near-miss.
 */
namespace tlsPath {
constexpr char const* ca = "/etc/xrpl/tls/collector-ca-bundle.pem";
constexpr char const* clientCert = "/etc/xrpl/tls/node-client-certificate.pem";
constexpr char const* clientKey = "/etc/xrpl/tls/node-client-private-key.pem";
}  // namespace tlsPath

constexpr char const* kHttpsEndpoint = "https://collector.example:4318/v1/traces";

/**
 * Build a Setup with mutual TLS configured and nothing else set.
 *
 * The struct is filled directly rather than parsed, so these cases isolate the
 * Setup-to-exporter mapping. The end-to-end case at the bottom of this file
 * covers the config-file side.
 *
 * @param useTls  Value for Setup::useTls; the only thing the cases vary.
 * @return The populated Setup.
 */
telemetry::Telemetry::Setup
makeMtlsSetup(bool useTls)
{
    telemetry::Telemetry::Setup setup;
    setup.enabled = true;
    setup.tracesEndpoint = kHttpsEndpoint;
    setup.useTls = useTls;
    setup.tlsCertPath = tlsPath::ca;
    setup.tlsClientCertPath = tlsPath::clientCert;
    setup.tlsClientKeyPath = tlsPath::clientKey;
    return setup;
}

/**
 * Write a placeholder certificate file at the given path.
 *
 * makeTelemetrySetup() only needs the file to exist and be readable; nothing
 * checks that the contents parse as PEM.
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

}  // namespace

TEST(TraceExporterOptions, mtls_paths_reach_the_matching_exporter_fields)
{
    // The assertion the exporter boundary was missing: each configured path
    // lands in its own field. The three paths differ, so swapping the client
    // certificate and key, or writing the CA bundle into a client field, fails
    // here instead of failing as a TLS handshake error on a live collector.
    auto const opts = telemetry::makeTraceExporterOptions(makeMtlsSetup(true));

    EXPECT_EQ(opts.url, kHttpsEndpoint);
    EXPECT_EQ(opts.ssl_ca_cert_path, tlsPath::ca);
    EXPECT_EQ(opts.ssl_client_cert_path, tlsPath::clientCert);
    EXPECT_EQ(opts.ssl_client_key_path, tlsPath::clientKey);
}

TEST(TraceExporterOptions, one_way_tls_leaves_the_client_fields_empty)
{
    // The one-way TLS control: a CA bundle and no client identity. The client
    // fields must stay empty, so an unset client certificate cannot pick up a
    // path from somewhere else in Setup.
    auto setup = makeMtlsSetup(true);
    setup.tlsClientCertPath.clear();
    setup.tlsClientKeyPath.clear();

    auto const opts = telemetry::makeTraceExporterOptions(setup);

    EXPECT_EQ(opts.url, kHttpsEndpoint);
    EXPECT_EQ(opts.ssl_ca_cert_path, tlsPath::ca);
    EXPECT_EQ(opts.ssl_client_cert_path, "");
    EXPECT_EQ(opts.ssl_client_key_path, "");
}

TEST(TraceExporterOptions, use_tls_off_passes_no_tls_paths_at_all)
{
    // Every path is configured and use_tls is off, so all three fields must
    // stay empty while the URL still goes through. This is the only case that
    // distinguishes "gated on use_tls" from "always copied".
    auto const opts = telemetry::makeTraceExporterOptions(makeMtlsSetup(false));

    EXPECT_EQ(opts.url, kHttpsEndpoint);
    EXPECT_EQ(opts.ssl_ca_cert_path, "");
    EXPECT_EQ(opts.ssl_client_cert_path, "");
    EXPECT_EQ(opts.ssl_client_key_path, "");
}

TEST(TraceExporterOptions, config_section_reaches_the_exporter_options)
{
    // The whole path in one case: a [telemetry] section with an https endpoint
    // and two different real files, parsed by makeTelemetrySetup() and then
    // mapped. Nothing between the config file and the exporter is stubbed, so a
    // break anywhere along it lands here.
    TempDir const dir;
    auto const cert = writeCertFile(dir.file("node-client-certificate.pem"));
    auto const key = writeCertFile(dir.file("node-client-private-key.pem"));
    ASSERT_NE(cert, key);

    Section section;
    section.set("enabled", "1");
    section.set("traces_endpoint", kHttpsEndpoint);
    section.set("use_tls", "1");
    section.set("tls_client_cert", cert);
    section.set("tls_client_key", key);

    auto const setup = telemetry::makeTelemetrySetup(section, "nHUtest123", "2.0.0", 0);
    auto const opts = telemetry::makeTraceExporterOptions(setup);

    EXPECT_EQ(opts.url, kHttpsEndpoint);
    EXPECT_EQ(opts.ssl_client_cert_path, cert);
    EXPECT_EQ(opts.ssl_client_key_path, key);
    // No tls_ca_cert in the section, so the exporter keeps its own trust store.
    EXPECT_EQ(opts.ssl_ca_cert_path, "");
}

#endif  // XRPL_ENABLE_TELEMETRY
