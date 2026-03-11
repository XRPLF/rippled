#include <test/jtx.h>

#include <xrpld/core/ConfigSections.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.grpc.pb.h>

#include <boost/filesystem.hpp>

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

constexpr std::string_view kCA_CERT_CONTENT =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

constexpr std::string_view kSERVER_CERT_CONTENT =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFlDCCA3ygAwIBAgIUXlAnHAM2/N1x11BsnxVkQPqt8iMwDQYJKoZIhvcNAQEL\n"
    "BQAwVzELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx\n"
    "GDAWBgNVBAoMD1JpcHBsZWQgVGVzdCBDQTEQMA4GA1UEAwwHVGVzdCBDQTAeFw0y\n"
    "NjAyMTYxNDA4MzFaFw0yNzAyMTYxNDA4MzFaMFExCzAJBgNVBAYTAlVTMQ0wCwYD\n"
    "VQQIDARUZXN0MQ0wCwYDVQQHDARUZXN0MRAwDgYDVQQKDAdSaXBwbGVkMRIwEAYD\n"
    "VQQDDAlsb2NhbGhvc3QwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQCe\n"
    "0C+Xo3VpTRPjP+cvCsEU0EbiL9Pghknwj6HwVZeBQidEp5ooqY4m1K6/HnAM3vWg\n"
    "H8a5nxesqXnrHo1Od7xwaVOACp/doSeZCnBbnopNjAnAC/SmlGO1gaF4koFRqOrj\n"
    "d7TZXN+dp+4WuhNcfHrkswRPeQOukRf3aEwAdPPAq90J698R3yck47Ylphk8v2nB\n"
    "tBObnFWAYi+HJooe57Mmqupj2OmNTazer0nLTQEwVgvdnGweIiR6YVcnGOBf6Lu2\n"
    "YK4Dwl1RFJurxO7TL43ziq2WvmpEHvHJ62YT4ze3Ik5EVOrYUjjADhKAoc9etBMP\n"
    "b+sQqnBRzPJBtNE8c65gIlrwG6B+oJ7A4/J1aYd9n85k/z2ppKNAZTv8HBxeU2bu\n"
    "vGNxD+qHr6j4r2nlbsJH/0mcb5IJykAFhwowM2/Q4h9yTrLDVNEUs8PcshPW7O9f\n"
    "hKdl8blybSZ395aXRVqk9umGdkwKGjuSZJSQxIMJhHWpf5AobL8DDZ2vuUwmA69O\n"
    "tD7i7XUBqAkMB+VBfSp1XMqRIM1PCfhXrTb9yTiqNRZYRAMIzxvkB6ZIxRJjJDLf\n"
    "gPqpIS6lKZedl4K/FFwBRh/1Qyd8gK1ozk2VIZERkiBf49xy9vz79Ky3Sr4VMpwJ\n"
    "3S46MJXb/60e+4O+piLcXchGYZcFHXafIXkq/4crRQIDAQABo14wXDAaBgNVHREE\n"
    "EzARgglsb2NhbGhvc3SHBH8AAAEwHQYDVR0OBBYEFO2AI+yJSHRRGKINyX7uzAJV\n"
    "Mc3nMB8GA1UdIwQYMBaAFJNBOu5/8HSlS+phdKTCejdt5LrbMA0GCSqGSIb3DQEB\n"
    "CwUAA4ICAQAtO0GWUYBVqEaeAICdX0h9EpDHuKJXPLMPt/aOqQY0sI5ryks+/QLe\n"
    "0ksOde2AmDW4maWi9toILT7WZg7qRGQGZc0Pk0LgU/JANVJaQPHPJvQcgcm2A8CT\n"
    "Td36+K4MLrDd8mViCczaUebmO182RKIcSV47XfQbEpQLVwZKC7AufNRaRhUko5X3\n"
    "lNb7KgZTNcQAZ5gvfJZ3b+3GzbuRz7WOaPyE94yy3D+6ML7XEd7rNoYKFpsrWjh8\n"
    "0XWJQlX41X6orsi6g9pzprtOgl2PoJvGBSAd/Pp4jxnRVNflxxAiFaoWFxksOsuu\n"
    "CiKQkB696AhRcPZyFk60ubeEo3epv2u1kjHSP3HTxe1iIO97qsL7zxfEB5lx6ubB\n"
    "SylV2s23ysCCSqjbHkGgI/vVkHOPWHsCMdLjPuBUB5K9rfT0pBrCVss6mTr8D/7m\n"
    "uy2rRWMXvPVKlxY7FhRTCpWMwBbe7xojGtniqXWsQml2nv/wMiUbtfwvNhFbqFjK\n"
    "zwI9GnULtjmtpXkGzHGOaiG8ZAsc7cgsahKdFq+EfgmeHV4Gy01mb+UIT3npdpeu\n"
    "YSUo7dfqdzPSZcKJgtsnhta24qyFlw93YlN1LJj/Mt0YKRNKcbiRYTSmh0e0bjM+\n"
    "Uv20DqFMwRtULPOka+qw22vzEUvZcX2SdEcGpb8Ej1oHrgo4wE0gCQ==\n"
    "-----END CERTIFICATE-----\n";

constexpr std::string_view kSERVER_KEY_CONTENT =
    "-----BEGIN PRIVATE KEY-----\n"
    "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\n"
    "5dPKrvLYFOCfSjtu7qDDs9AH7FOo0VI=\n"
    "-----END PRIVATE KEY-----\n";

constexpr std::string_view kCLIENT_CERT_CONTENT =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFgTCCA2mgAwIBAgIUXlAnHAM2/N1x11BsnxVkQPqt8iQwDQYJKoZIhvcNAQEL\n"
    "BQAwVzELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx\n"
    "GDAWBgNVBAoMD1JpcHBsZWQgVGVzdCBDQTEQMA4GA1UEAwwHVGVzdCBDQTAeFw0y\n"
    "NjAyMTYxNDA4MzRaFw0yNzAyMTYxNDA4MzRaMFoxCzAJBgNVBAYTAlVTMQ0wCwYD\n"
    "VQQIDARUZXN0MQ0wCwYDVQQHDARUZXN0MRcwFQYDVQQKDA5SaXBwbGVkIENsaWVu\n"
    "dDEUMBIGA1UEAwwLdGVzdC1jbGllbnQwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAw\n"
    "ggIKAoICAQDFHA1Jv5xk/7lS/iB03DEYPgvGEmbhXbqss87zFFgY+ZlZCW88CdF/\n"
    "4MhrjD+Xr+ASQHaDWPMF5VQeVhiP0mwB+C6gFVBjYofFqGOGSmBg5qAdKgkHw4Y5\n"
    "Ev+FaQNhb2BVfSGQnKJKI441Okqq7zC1UspH6bT2YZcE6QoF8Kg02KO66MZfWdyz\n"
    "xPmU4eVDnOcx5J+6ytF+OHzOcGPhGaLuUFaAxe7CKIChfau/NZ8Li/CB24g5+cgG\n"
    "V6CFxLuP6rad7BNKA2OUMTPPoi1UnGc3EPdttZehX9Ufo5OzQGVNOJtMWaPkhFBk\n"
    "qYlgTHD7x93WUdcncPWO44Nf1S/E+qMvRJCuHUPJhBMAC9k4h2/ZfFyQZxL8b0u6\n"
    "cmDxTPMZ5XtdLvuNsEBxeoGMMrR9yb+kWt15U3SspOK3QA/s4ISBUwJhhVrQoQDs\n"
    "yw6QWd3yx7aTnrwzyRxuZwpDnrVwaqK4bQ9VPA00jWCMBFkWIKG0jBkFMky3PyGp\n"
    "rV7nmLvC8zwLSOqqGyMK778DtZ2Z8wxz9P1HJoDs881yIHkgl4C2iWU0FKN2a+KJ\n"
    "W6sz8EBCfNB8jBDwitWuMJjPYFMJqBNbG/03Uwv81Bpn2Dxsc1YVIDpnyNKmvqAX\n"
    "PVYhDVHFmCiVVOSudsizaWYLbQ967gygr9G1Bxv872Z39+EeZHeaCwIDAQABo0Iw\n"
    "QDAdBgNVHQ4EFgQUVVIq5WSinue5ckSjzKa62dgF7PQwHwYDVR0jBBgwFoAUk0E6\n"
    "7n/wdKVL6mF0pMJ6N23kutswDQYJKoZIhvcNAQELBQADggIBAJge82tnpQx+Hsmu\n"
    "TWtYwbFQCQmN9cWpk6QlR7C9bnfCcFXu4oIzzC/mYP5ER7JWHSIxluE11flPaWeQ\n"
    "+F5ay9lzMmmkgL7DvtFnCvD8sF2zzWJ1VQ88Cqpdba+wVeMSkxEYxCXcKv8s283F\n"
    "+gO53QVWZAyrKUOx160E8aeNeVG0NPEhC6I2EHHktdscn5gVXlNrU4VejKoOxZiS\n"
    "UDySlU6S0osw+pFkr3CtRYz0ho0tC39UWxPHvcmlie8T1WGcKkKzDKzVMTZxbNs1\n"
    "NnaXXIWX7j4kh4S8jrTkfXy+/T41EJ00ZQMvgy5p1qGAZiItlvg7OTYSpo6Nxl2C\n"
    "yeeUrMLe7Wtt5TzRun2i10KhcoefDgAvTFI5F2A1r/ycBTC17ukDwHJ0ol6iVvRV\n"
    "jjDc/sdD4Qgxb9FJAICbalypucYqGOZljfntctsT69ny25ShhMaTKtlQqJk0bAlU\n"
    "+JsEwcYwj6DpmINaDkGjFcBr4hosgHyGhkkH0CFoeoLYdS8wEBT0o243+xxouaz4\n"
    "q8qOKa2WVYbK8bv/vksQJlhfTZfik4q5tTuMC10FTItvYAbYKP0xDKXHejv8ZP7T\n"
    "IBcKG1ZUECQaiPaI75zyZoqkmg8TeHPX0mApYkzcG2QaFFsyeREy2Iji5eNy5t40\n"
    "V2wsqVh+F0f/gaeGgT34h2FOriyE\n"
    "-----END CERTIFICATE-----\n";

constexpr std::string_view kCLIENT_KEY_CONTENT =
    "-----BEGIN PRIVATE KEY-----\n"
    "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\n"
    "exqnVXngZ0T1FlADEiBdV2qJR+hbMA==\n"
    "-----END PRIVATE KEY-----\n";

/**
 * RAII helper for managing temporary TLS certificates in tests.
 *
 * Creates a temporary directory and writes test certificates to it.
 * Automatically cleans up the directory when destroyed.
 */
class TemporaryTLSCertificates
{
public:
    static constexpr std::string_view kCA_CERT_FILENAME = "ca.pem";
    static constexpr std::string_view kSERVER_CERT_FILENAME = "server_cert.pem";
    static constexpr std::string_view kSERVER_KEY_FILENAME = "server_key.pem";
    static constexpr std::string_view kCLIENT_CERT_FILENAME = "client_cert.pem";
    static constexpr std::string_view kCLIENT_KEY_FILENAME = "client_key.pem";
    static constexpr std::string_view kCERTS_DIR_PREFIX = "grpc_tls_test_";

    TemporaryTLSCertificates()
    {
        auto tmpDir = std::filesystem::temp_directory_path();
        auto uniqueDirName =
            boost::filesystem::unique_path(std::string(kCERTS_DIR_PREFIX) + "%%%%%%%%");
        tempDir_ = tmpDir / uniqueDirName.string();
        std::filesystem::create_directories(tempDir_);

        writeFile(tempDir_ / kCA_CERT_FILENAME, kCA_CERT_CONTENT);
        writeFile(tempDir_ / kSERVER_CERT_FILENAME, kSERVER_CERT_CONTENT);
        writeFile(tempDir_ / kSERVER_KEY_FILENAME, kSERVER_KEY_CONTENT);
        writeFile(tempDir_ / kCLIENT_CERT_FILENAME, kCLIENT_CERT_CONTENT);
        writeFile(tempDir_ / kCLIENT_KEY_FILENAME, kCLIENT_KEY_CONTENT);
    }

    virtual ~TemporaryTLSCertificates()
    {
        std::error_code ec;
        std::filesystem::remove_all(tempDir_, ec);
    }

    TemporaryTLSCertificates(TemporaryTLSCertificates const&) = delete;
    TemporaryTLSCertificates&
    operator=(TemporaryTLSCertificates const&) = delete;
    TemporaryTLSCertificates(TemporaryTLSCertificates&&) = delete;
    TemporaryTLSCertificates&
    operator=(TemporaryTLSCertificates&&) = delete;

    [[nodiscard]] std::filesystem::path
    getCACertPath() const
    {
        return tempDir_ / kCA_CERT_FILENAME;
    }

    [[nodiscard]] std::filesystem::path
    getServerCertPath() const
    {
        return tempDir_ / kSERVER_CERT_FILENAME;
    }

    [[nodiscard]] std::filesystem::path
    getServerKeyPath() const
    {
        return tempDir_ / kSERVER_KEY_FILENAME;
    }

    [[nodiscard]] std::filesystem::path
    getClientCertPath() const
    {
        return tempDir_ / kCLIENT_CERT_FILENAME;
    }

    [[nodiscard]] std::filesystem::path
    getClientKeyPath() const
    {
        return tempDir_ / kCLIENT_KEY_FILENAME;
    }

    [[nodiscard]] std::filesystem::path
    getTempDir() const
    {
        return tempDir_;
    }

private:
    void
    writeFile(std::filesystem::path const& path, std::string_view content)
    {
        std::ofstream file(path);
        if (!file)
            throw std::runtime_error("Failed to create file: " + path.string());
        file << content;
        if (!file)
            throw std::runtime_error("Failed to write file: " + path.string());
    }

    std::filesystem::path tempDir_;
};

}  // namespace

namespace xrpl {
namespace test {
/**
 * Helper function to make a simple gRPC call to test connectivity.
 * Returns true if the call succeeded, false otherwise.
 */
bool
makeTestGRPCCall(std::unique_ptr<org::xrpl::rpc::v1::XRPLedgerAPIService::Stub> const& stub)
{
    grpc::ClientContext context;
    org::xrpl::rpc::v1::GetLedgerRequest request;
    org::xrpl::rpc::v1::GetLedgerResponse response;

    // Set a short deadline to avoid hanging on failed connections
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));

    grpc::Status status = stub->GetLedger(&context, request, &response);
    return status.ok();
}

class GRPCServerTLS_test : public beast::unit_test::suite, public TemporaryTLSCertificates
{
public:
    void
    testWithoutTLS()
    {
        testcase("GRPCServer without TLS");

        using namespace jtx;

        // Create config without TLS settings
        auto cfg = envconfig(addGrpcConfig);
        Env env(*this, std::move(cfg));

        // Verify the server actually started by checking the port
        auto const grpcPort = env.app().config()[SECTION_PORT_GRPC].get<unsigned int>("port");
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort > 0);

        // Test 1: Plaintext client should connect successfully
        std::string serverAddress = "localhost:" + std::to_string(*grpcPort);
        auto plaintextStub = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials()));
        BEAST_EXPECT(makeTestGRPCCall(plaintextStub));
    }

    void
    testWithValidTLS()
    {
        testcase("GRPCServer with valid TLS configuration (no mutual TLS)");

        using namespace jtx;

        // Test with just server cert and key (no client verification)
        auto cfg = envconfig(
            addGrpcConfigWithTLS, getServerCertPath().string(), getServerKeyPath().string());
        Env env(*this, std::move(cfg));

        // Verify the server actually started by checking the port
        auto const grpcPort = env.app().config()[SECTION_PORT_GRPC].get<unsigned int>("port");
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort > 0);

        std::string serverAddress = "localhost:" + std::to_string(*grpcPort);

        // Test 1: Plaintext client should FAIL against TLS server
        auto plaintextStub = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials()));
        BEAST_EXPECT(!makeTestGRPCCall(plaintextStub));

        // Test 2: TLS client with server CA should succeed
        grpc::SslCredentialsOptions sslOpts;
        sslOpts.pem_root_certs = std::string(kCA_CERT_CONTENT);
        auto tlsStub = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::SslCredentials(sslOpts)));
        BEAST_EXPECT(makeTestGRPCCall(tlsStub));
    }

    void
    testWithMutualTLS()
    {
        testcase("GRPCServer with mutual TLS (client verification enabled)");

        using namespace jtx;

        // Test with server cert, key, and CA for client verification
        auto cfg = envconfig(
            addGrpcConfigWithTLSAndClientCA,
            getServerCertPath().string(),
            getServerKeyPath().string(),
            getCACertPath().string());
        Env env(*this, std::move(cfg));

        // Verify the server actually started by checking the port
        auto const grpcPort = env.app().config()[SECTION_PORT_GRPC].get<unsigned int>("port");
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort > 0);

        auto const serverAddress = "localhost:" + std::to_string(*grpcPort);

        // Test 1: TLS client WITHOUT client certificate should FAIL (mTLS requires client cert)
        grpc::SslCredentialsOptions sslOptsNoClient;
        sslOptsNoClient.pem_root_certs = std::string(kCA_CERT_CONTENT);
        auto tlsStubNoClient = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::SslCredentials(sslOptsNoClient)));
        BEAST_EXPECT(!makeTestGRPCCall(tlsStubNoClient));

        // Test 2: TLS client WITH client certificate should succeed
        grpc::SslCredentialsOptions sslOptsWithClient;
        sslOptsWithClient.pem_root_certs = std::string(kCA_CERT_CONTENT);
        sslOptsWithClient.pem_cert_chain = std::string(kCLIENT_CERT_CONTENT);
        sslOptsWithClient.pem_private_key = std::string(kCLIENT_KEY_CONTENT);
        auto tlsStubWithClient = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::SslCredentials(sslOptsWithClient)));
        BEAST_EXPECT(makeTestGRPCCall(tlsStubWithClient));
    }

    void
    testWithMissingKey()
    {
        testcase("GRPCServer with cert but no key");

        using namespace jtx;

        // Create config with only cert (missing key)
        auto cfg = envconfig();
        (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
        (*cfg)[SECTION_PORT_GRPC].set("port", "0");
        (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", getServerCertPath().string());
        // Intentionally omit ssl_key

        try
        {
            Env env(*this, std::move(cfg));
            fail("Should have thrown exception for incomplete TLS config");
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(
                std::string(e.what()).find("Incomplete TLS configuration") != std::string::npos);
        }
    }

    void
    testWithMissingCert()
    {
        testcase("GRPCServer with key but no cert");

        using namespace jtx;

        // Create config with only key (missing cert)
        auto cfg = envconfig();
        (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
        (*cfg)[SECTION_PORT_GRPC].set("port", "0");
        (*cfg)[SECTION_PORT_GRPC].set("ssl_key", getServerKeyPath().string());
        // Intentionally omit ssl_cert

        try
        {
            Env env(*this, std::move(cfg));
            fail("Should have thrown exception for incomplete TLS config");
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(
                std::string(e.what()).find("Incomplete TLS configuration") != std::string::npos);
        }
    }

    void
    testWithClientCAButNoTLS()
    {
        testcase("GRPCServer with ssl_client_ca but without both ssl_cert and ssl_key");

        using namespace jtx;

        // Test 1: ssl_client_ca specified without any TLS config
        {
            auto cfg = envconfig();
            (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
            (*cfg)[SECTION_PORT_GRPC].set("port", "0");
            (*cfg)[SECTION_PORT_GRPC].set("ssl_client_ca", getCACertPath().string());
            // Intentionally omit both ssl_cert and ssl_key

            try
            {
                Env env(*this, std::move(cfg));
                fail("Should have thrown exception for ssl_client_ca without TLS config");
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()).find(
                        "ssl_client_ca requires both ssl_cert and ssl_key") != std::string::npos);
            }
        }

        // Test 2: ssl_client_ca with only ssl_cert (missing ssl_key)
        {
            auto cfg = envconfig();
            (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
            (*cfg)[SECTION_PORT_GRPC].set("port", "0");
            (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", getServerCertPath().string());
            (*cfg)[SECTION_PORT_GRPC].set("ssl_client_ca", getCACertPath().string());
            // Intentionally omit ssl_key

            try
            {
                Env env(*this, std::move(cfg));
                fail("Should have thrown exception for ssl_client_ca with only ssl_cert");
            }
            catch (std::runtime_error const& e)
            {
                // This should fail with "Incomplete TLS configuration" first
                // because ssl_cert is specified without ssl_key
                BEAST_EXPECT(
                    std::string(e.what()).find("Incomplete TLS configuration") !=
                    std::string::npos);
            }
        }

        // Test 3: ssl_client_ca with only ssl_key (missing ssl_cert)
        {
            auto cfg = envconfig();
            (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
            (*cfg)[SECTION_PORT_GRPC].set("port", "0");
            (*cfg)[SECTION_PORT_GRPC].set("ssl_key", getServerKeyPath().string());
            (*cfg)[SECTION_PORT_GRPC].set("ssl_client_ca", getCACertPath().string());
            // Intentionally omit ssl_cert

            try
            {
                Env env(*this, std::move(cfg));
                fail("Should have thrown exception for ssl_client_ca with only ssl_key");
            }
            catch (std::runtime_error const& e)
            {
                // This should fail with "Incomplete TLS configuration" first
                // because ssl_key is specified without ssl_cert
                BEAST_EXPECT(
                    std::string(e.what()).find("Incomplete TLS configuration") !=
                    std::string::npos);
            }
        }
    }

    void
    testWithCertChainButNoTLS()
    {
        testcase("GRPCServer with ssl_cert_chain but without both ssl_cert and ssl_key");

        using namespace jtx;

        // Test 1: ssl_cert_chain specified without any TLS config
        {
            auto cfg = envconfig();
            (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
            (*cfg)[SECTION_PORT_GRPC].set("port", "0");
            (*cfg)[SECTION_PORT_GRPC].set("ssl_cert_chain", getCACertPath().string());
            // Intentionally omit both ssl_cert and ssl_key

            try
            {
                Env env(*this, std::move(cfg));
                fail("Should have thrown exception for ssl_cert_chain without TLS config");
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()).find(
                        "ssl_cert_chain requires both ssl_cert and ssl_key") != std::string::npos);
            }
        }

        // Test 2: ssl_cert_chain with only ssl_cert (missing ssl_key)
        {
            auto cfg = envconfig();
            (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
            (*cfg)[SECTION_PORT_GRPC].set("port", "0");
            (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", getServerCertPath().string());
            (*cfg)[SECTION_PORT_GRPC].set("ssl_cert_chain", getCACertPath().string());
            // Intentionally omit ssl_key

            try
            {
                Env env(*this, std::move(cfg));
                fail("Should have thrown exception for ssl_cert_chain with only ssl_cert");
            }
            catch (std::runtime_error const& e)
            {
                // This should fail with "Incomplete TLS configuration" first
                // because ssl_cert is specified without ssl_key
                BEAST_EXPECT(
                    std::string(e.what()).find("Incomplete TLS configuration") !=
                    std::string::npos);
            }
        }
    }

    void
    testWithCertChain()
    {
        testcase("GRPCServer with ssl_cert_chain for intermediate CA certificates");

        using namespace jtx;

        // Test with server cert, key, and cert chain (intermediate CA)
        // In this test, we use the CA cert as a stand-in for an intermediate CA cert
        auto cfg = envconfig(
            addGrpcConfigWithTLSAndCertChain,
            getServerCertPath().string(),
            getServerKeyPath().string(),
            getCACertPath().string());
        Env env(*this, std::move(cfg));

        // Verify the server actually started by checking the port
        auto const grpcPort = env.app().config()[SECTION_PORT_GRPC].get<unsigned int>("port");
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort > 0);

        auto const serverAddress = "localhost:" + std::to_string(*grpcPort);

        // Test: TLS client should be able to connect (no client cert required)
        grpc::SslCredentialsOptions sslOpts;
        sslOpts.pem_root_certs = std::string(kCA_CERT_CONTENT);
        auto tlsStub = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::SslCredentials(sslOpts)));
        BEAST_EXPECT(makeTestGRPCCall(tlsStub));

        // Insecure client should fail
        auto insecureStub = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials()));
        BEAST_EXPECT(!makeTestGRPCCall(insecureStub));
    }

    void
    testWithInvalidCertFile()
    {
        testcase("GRPCServer with invalid/non-existent certificate file");

        using namespace jtx;

        auto cfg = envconfig();
        (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
        (*cfg)[SECTION_PORT_GRPC].set("port", "0");
        (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", "/nonexistent/path/to/cert.pem");
        (*cfg)[SECTION_PORT_GRPC].set("ssl_key", getServerKeyPath().string());

        Env env(*this, std::move(cfg));

        // Server should fail to start - verify port is 0
        auto const grpcPort = env.app().config()[SECTION_PORT_GRPC].get<unsigned int>("port");
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort == 0);  // Server should not have started
    }

    void
    testWithInvalidKeyFile()
    {
        testcase("GRPCServer with invalid/non-existent key file");

        using namespace jtx;

        auto cfg = envconfig();
        (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
        (*cfg)[SECTION_PORT_GRPC].set("port", "0");
        (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", getServerCertPath().string());
        (*cfg)[SECTION_PORT_GRPC].set("ssl_key", "/nonexistent/path/to/key.pem");

        Env env(*this, std::move(cfg));

        // Server should fail to start - verify port is 0
        auto const grpcPort = env.app().config()[SECTION_PORT_GRPC].get<unsigned int>("port");
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort == 0);  // Server should not have started
    }

    void
    testWithInvalidCertChainFile()
    {
        testcase("GRPCServer with invalid/non-existent cert chain file");

        using namespace jtx;

        auto cfg = envconfig();
        (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
        (*cfg)[SECTION_PORT_GRPC].set("port", "0");
        (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", getServerCertPath().string());
        (*cfg)[SECTION_PORT_GRPC].set("ssl_key", getServerKeyPath().string());
        (*cfg)[SECTION_PORT_GRPC].set("ssl_cert_chain", "/nonexistent/path/to/chain.pem");

        Env env(*this, std::move(cfg));

        // Server should fail to start - verify port is 0
        auto const grpcPort = env.app().config()[SECTION_PORT_GRPC].get<unsigned int>("port");
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort == 0);  // Server should not have started
    }

    void
    testWithInvalidClientCAFile()
    {
        testcase("GRPCServer with invalid/non-existent client CA file");

        using namespace jtx;

        auto cfg = envconfig();
        (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
        (*cfg)[SECTION_PORT_GRPC].set("port", "0");
        (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", getServerCertPath().string());
        (*cfg)[SECTION_PORT_GRPC].set("ssl_key", getServerKeyPath().string());
        (*cfg)[SECTION_PORT_GRPC].set("ssl_client_ca", "/nonexistent/path/to/ca.pem");

        Env env(*this, std::move(cfg));

        // Server should fail to start - verify port is 0
        auto const grpcPort = env.app().config()[SECTION_PORT_GRPC].get<unsigned int>("port");
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort == 0);  // Server should not have started
    }

    void
    testWithEmptyClientCAFile()
    {
        testcase("GRPCServer with empty client CA file");

        using namespace jtx;

        // Create an empty file for client CA
        auto emptyCAPath = getTempDir() / "empty_ca.pem";
        std::ofstream emptyFile(emptyCAPath);
        emptyFile.close();

        auto cfg = envconfig();
        (*cfg)[SECTION_PORT_GRPC].set("ip", "127.0.0.1");
        (*cfg)[SECTION_PORT_GRPC].set("port", "0");
        (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", getServerCertPath().string());
        (*cfg)[SECTION_PORT_GRPC].set("ssl_key", getServerKeyPath().string());
        (*cfg)[SECTION_PORT_GRPC].set("ssl_client_ca", emptyCAPath.string());

        Env env(*this, std::move(cfg));

        // Server should fail to start due to empty CA file
        auto const grpcPort = env.app().config()[SECTION_PORT_GRPC].get<unsigned int>("port");
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort == 0);  // Server should not have started
    }

    void
    testWithBothCertChainAndClientCA()
    {
        testcase("GRPCServer with both cert chain and client CA (full mTLS with intermediates)");

        using namespace jtx;

        // Test with all TLS features enabled: cert, key, cert_chain, and client_ca
        auto cfg = envconfig();
        (*cfg)[SECTION_PORT_GRPC].set("ip", getEnvLocalhostAddr());
        (*cfg)[SECTION_PORT_GRPC].set("port", "0");
        (*cfg)[SECTION_PORT_GRPC].set("ssl_cert", getServerCertPath().string());
        (*cfg)[SECTION_PORT_GRPC].set("ssl_key", getServerKeyPath().string());
        (*cfg)[SECTION_PORT_GRPC].set(
            "ssl_cert_chain", getCACertPath().string());  // Using CA as intermediate
        (*cfg)[SECTION_PORT_GRPC].set("ssl_client_ca", getCACertPath().string());

        Env env(*this, std::move(cfg));

        // Verify the server started successfully
        auto const grpcPort = env.app().config()[SECTION_PORT_GRPC].get<unsigned int>("port");
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort > 0);

        auto const serverAddress = "localhost:" + std::to_string(*grpcPort);

        // Test 1: TLS client WITHOUT client certificate should FAIL (mTLS requires client cert)
        grpc::SslCredentialsOptions sslOptsNoClient;
        sslOptsNoClient.pem_root_certs = std::string(kCA_CERT_CONTENT);
        auto tlsStubNoClient = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::SslCredentials(sslOptsNoClient)));
        BEAST_EXPECT(!makeTestGRPCCall(tlsStubNoClient));

        // Test 2: TLS client WITH client certificate should succeed
        grpc::SslCredentialsOptions sslOptsWithClient;
        sslOptsWithClient.pem_root_certs = std::string(kCA_CERT_CONTENT);
        sslOptsWithClient.pem_cert_chain = std::string(kCLIENT_CERT_CONTENT);
        sslOptsWithClient.pem_private_key = std::string(kCLIENT_KEY_CONTENT);
        auto tlsStubWithClient = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::SslCredentials(sslOptsWithClient)));
        BEAST_EXPECT(makeTestGRPCCall(tlsStubWithClient));
    }

    void
    run() override
    {
        testWithoutTLS();
        testWithValidTLS();
        testWithMutualTLS();
        testWithMissingKey();
        testWithMissingCert();
        testWithClientCAButNoTLS();
        testWithCertChainButNoTLS();
        testWithCertChain();
        testWithInvalidCertFile();
        testWithInvalidKeyFile();
        testWithInvalidCertChainFile();
        testWithInvalidClientCAFile();
        testWithEmptyClientCAFile();
        testWithBothCertChainAndClientCA();
    }
};

BEAST_DEFINE_TESTSUITE(GRPCServerTLS, app, xrpl);

}  // namespace test
}  // namespace xrpl
