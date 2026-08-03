#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/proto/org/xrpl/rpc/v1/get_ledger.pb.h>
#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.grpc.pb.h>

#include <boost/filesystem/operations.hpp>

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/status.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr std::string_view kCaCertContent =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "fAbAYqu0rfMFHUYjzIVnu8WRCC56qYHO5tU=\n"
    "-----END CERTIFICATE-----\n";

constexpr std::string_view kServerCertContent =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

constexpr std::string_view kServerKeyContent =
    "-----BEGIN PRIVATE KEY-----\n"
    "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\n"
    "S/RYUSUkZ4VvqFUfo7wT8x18urb87w==\n"
    "-----END PRIVATE KEY-----\n";

constexpr std::string_view kClientCertContent =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "cTe8jkzRqYdUfAoV\n"
    "-----END CERTIFICATE-----\n";

constexpr std::string_view kClientKeyContent =
    "-----BEGIN PRIVATE KEY-----\n"
    "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\n"
    "+6f4hsbp29hg+BUZ+UPdk7QyCKpZD1A=\n"
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
    static constexpr std::string_view kCaCertFilename = "ca.pem";
    static constexpr std::string_view kServerCertFilename = "server_cert.pem";
    static constexpr std::string_view kServerKeyFilename = "server_key.pem";
    static constexpr std::string_view kClientCertFilename = "client_cert.pem";
    static constexpr std::string_view kClientKeyFilename = "client_key.pem";
    static constexpr std::string_view kCertsDirPrefix = "grpc_tls_test_";

    TemporaryTLSCertificates()
    {
        auto tmpDir = std::filesystem::temp_directory_path();
        auto uniqueDirName =
            boost::filesystem::unique_path(std::string(kCertsDirPrefix) + "%%%%%%%%");
        tempDir_ = tmpDir / uniqueDirName.string();
        std::filesystem::create_directories(tempDir_);

        writeFile(tempDir_ / kCaCertFilename, kCaCertContent);
        writeFile(tempDir_ / kServerCertFilename, kServerCertContent);
        writeFile(tempDir_ / kServerKeyFilename, kServerKeyContent);
        writeFile(tempDir_ / kClientCertFilename, kClientCertContent);
        writeFile(tempDir_ / kClientKeyFilename, kClientKeyContent);
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
        return tempDir_ / kCaCertFilename;
    }

    [[nodiscard]] std::filesystem::path
    getServerCertPath() const
    {
        return tempDir_ / kServerCertFilename;
    }

    [[nodiscard]] std::filesystem::path
    getServerKeyPath() const
    {
        return tempDir_ / kServerKeyFilename;
    }

    [[nodiscard]] std::filesystem::path
    getClientCertPath() const
    {
        return tempDir_ / kClientCertFilename;
    }

    [[nodiscard]] std::filesystem::path
    getClientKeyPath() const
    {
        return tempDir_ / kClientKeyFilename;
    }

    [[nodiscard]] std::filesystem::path
    getTempDir() const
    {
        return tempDir_;
    }

private:
    static void
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

namespace xrpl::test {
/**
 * Helper function to make a simple gRPC call to test connectivity.
 * Returns true if the call succeeded, false otherwise.
 */
bool
makeTestGRPCCall(std::unique_ptr<org::xrpl::rpc::v1::XRPLedgerAPIService::Stub> const& stub)
{
    grpc::ClientContext context;
    org::xrpl::rpc::v1::GetLedgerRequest const request;
    org::xrpl::rpc::v1::GetLedgerResponse response;

    // Set a short deadline to avoid hanging on failed connections
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));

    grpc::Status const status = stub->GetLedger(&context, request, &response);
    return status.ok();
}

class GRPCServerTLS_test : public beast::unit_test::Suite, public TemporaryTLSCertificates
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
        auto const grpcPort =
            env.app().config()[Sections::kPortGrpc].get<unsigned int>(Keys::kPort);
        BEAST_EXPECT(grpcPort.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access) grpcPort.has_value() checked above
        BEAST_EXPECT(*grpcPort > 0);

        // Test 1: Plaintext client should connect successfully
        std::string const serverAddress = "localhost:" + std::to_string(*grpcPort);
        // NOLINTEND(bugprone-unchecked-optional-access)
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
        auto const grpcPort =
            env.app().config()[Sections::kPortGrpc].get<unsigned int>(Keys::kPort);
        BEAST_EXPECT(grpcPort.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access) grpcPort.has_value() checked above
        BEAST_EXPECT(*grpcPort > 0);

        std::string const serverAddress = "localhost:" + std::to_string(*grpcPort);
        // NOLINTEND(bugprone-unchecked-optional-access)

        // Test 1: Plaintext client should FAIL against TLS server
        auto plaintextStub = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials()));
        BEAST_EXPECT(!makeTestGRPCCall(plaintextStub));

        // Test 2: TLS client with server CA should succeed
        grpc::SslCredentialsOptions sslOpts;
        sslOpts.pem_root_certs = std::string(kCaCertContent);
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
        auto const grpcPort =
            env.app().config()[Sections::kPortGrpc].get<unsigned int>(Keys::kPort);
        BEAST_EXPECT(grpcPort.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access) grpcPort.has_value() checked above
        BEAST_EXPECT(*grpcPort > 0);

        auto const serverAddress = "localhost:" + std::to_string(*grpcPort);
        // NOLINTEND(bugprone-unchecked-optional-access)

        // Test 1: TLS client WITHOUT client certificate should FAIL (mTLS requires client cert)
        grpc::SslCredentialsOptions sslOptsNoClient;
        sslOptsNoClient.pem_root_certs = std::string(kCaCertContent);
        auto tlsStubNoClient = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::SslCredentials(sslOptsNoClient)));
        BEAST_EXPECT(!makeTestGRPCCall(tlsStubNoClient));

        // Test 2: TLS client WITH client certificate should succeed
        grpc::SslCredentialsOptions sslOptsWithClient;
        sslOptsWithClient.pem_root_certs = std::string(kCaCertContent);
        sslOptsWithClient.pem_cert_chain = std::string(kClientCertContent);
        sslOptsWithClient.pem_private_key = std::string(kClientKeyContent);
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
        (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
        (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, getServerCertPath().string());
        // Intentionally omit ssl_key

        try
        {
            Env const env(*this, std::move(cfg));
            fail("Should have thrown exception for incomplete TLS config");
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(std::string(e.what()).contains("Incomplete TLS configuration"));
        }
    }

    void
    testWithMissingCert()
    {
        testcase("GRPCServer with key but no cert");

        using namespace jtx;

        // Create config with only key (missing cert)
        auto cfg = envconfig();
        (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
        (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, getServerKeyPath().string());
        // Intentionally omit ssl_cert

        try
        {
            Env const env(*this, std::move(cfg));
            fail("Should have thrown exception for incomplete TLS config");
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(std::string(e.what()).contains("Incomplete TLS configuration"));
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
            (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
            (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
            (*cfg)[Sections::kPortGrpc].set(Keys::kSslClientCa, getCACertPath().string());
            // Intentionally omit both ssl_cert and ssl_key

            try
            {
                Env const env(*this, std::move(cfg));
                fail("Should have thrown exception for ssl_client_ca without TLS config");
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()).contains(
                        "ssl_client_ca requires both ssl_cert and ssl_key"));
            }
        }

        // Test 2: ssl_client_ca with only ssl_cert (missing ssl_key)
        {
            auto cfg = envconfig();
            (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
            (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
            (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, getServerCertPath().string());
            (*cfg)[Sections::kPortGrpc].set(Keys::kSslClientCa, getCACertPath().string());
            // Intentionally omit ssl_key

            try
            {
                Env const env(*this, std::move(cfg));
                fail("Should have thrown exception for ssl_client_ca with only ssl_cert");
            }
            catch (std::runtime_error const& e)
            {
                // This should fail with "Incomplete TLS configuration" first
                // because ssl_cert is specified without ssl_key
                BEAST_EXPECT(std::string(e.what()).contains("Incomplete TLS configuration"));
            }
        }

        // Test 3: ssl_client_ca with only ssl_key (missing ssl_cert)
        {
            auto cfg = envconfig();
            (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
            (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
            (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, getServerKeyPath().string());
            (*cfg)[Sections::kPortGrpc].set(Keys::kSslClientCa, getCACertPath().string());
            // Intentionally omit ssl_cert

            try
            {
                Env const env(*this, std::move(cfg));
                fail("Should have thrown exception for ssl_client_ca with only ssl_key");
            }
            catch (std::runtime_error const& e)
            {
                // This should fail with "Incomplete TLS configuration" first
                // because ssl_key is specified without ssl_cert
                BEAST_EXPECT(std::string(e.what()).contains("Incomplete TLS configuration"));
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
            (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
            (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
            (*cfg)[Sections::kPortGrpc].set(Keys::kSslCertChain, getCACertPath().string());
            // Intentionally omit both ssl_cert and ssl_key

            try
            {
                Env const env(*this, std::move(cfg));
                fail("Should have thrown exception for ssl_cert_chain without TLS config");
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(
                    std::string(e.what()).contains(
                        "ssl_cert_chain requires both ssl_cert and ssl_key"));
            }
        }

        // Test 2: ssl_cert_chain with only ssl_cert (missing ssl_key)
        {
            auto cfg = envconfig();
            (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
            (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
            (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, getServerCertPath().string());
            (*cfg)[Sections::kPortGrpc].set(Keys::kSslCertChain, getCACertPath().string());
            // Intentionally omit ssl_key

            try
            {
                Env const env(*this, std::move(cfg));
                fail("Should have thrown exception for ssl_cert_chain with only ssl_cert");
            }
            catch (std::runtime_error const& e)
            {
                // This should fail with "Incomplete TLS configuration" first
                // because ssl_cert is specified without ssl_key
                BEAST_EXPECT(std::string(e.what()).contains("Incomplete TLS configuration"));
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
        auto const grpcPort =
            env.app().config()[Sections::kPortGrpc].get<unsigned int>(Keys::kPort);
        BEAST_EXPECT(grpcPort.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access) grpcPort.has_value() checked above
        BEAST_EXPECT(*grpcPort > 0);

        auto const serverAddress = "localhost:" + std::to_string(*grpcPort);
        // NOLINTEND(bugprone-unchecked-optional-access)

        // Test: TLS client should be able to connect (no client cert required)
        grpc::SslCredentialsOptions sslOpts;
        sslOpts.pem_root_certs = std::string(kCaCertContent);
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
        (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
        (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, "/nonexistent/path/to/cert.pem");
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, getServerKeyPath().string());

        Env env(*this, std::move(cfg));

        // Server should fail to start - verify port is 0
        auto const grpcPort =
            env.app().config()[Sections::kPortGrpc].get<unsigned int>(Keys::kPort);
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort == 0);  // NOLINT(bugprone-unchecked-optional-access)
    }

    void
    testWithInvalidKeyFile()
    {
        testcase("GRPCServer with invalid/non-existent key file");

        using namespace jtx;

        auto cfg = envconfig();
        (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
        (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, getServerCertPath().string());
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, "/nonexistent/path/to/key.pem");

        Env env(*this, std::move(cfg));

        // Server should fail to start - verify port is 0
        auto const grpcPort =
            env.app().config()[Sections::kPortGrpc].get<unsigned int>(Keys::kPort);
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort == 0);  // NOLINT(bugprone-unchecked-optional-access)
    }

    void
    testWithInvalidCertChainFile()
    {
        testcase("GRPCServer with invalid/non-existent cert chain file");

        using namespace jtx;

        auto cfg = envconfig();
        (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
        (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, getServerCertPath().string());
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, getServerKeyPath().string());
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslCertChain, "/nonexistent/path/to/chain.pem");

        Env env(*this, std::move(cfg));

        // Server should fail to start - verify port is 0
        auto const grpcPort =
            env.app().config()[Sections::kPortGrpc].get<unsigned int>(Keys::kPort);
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort == 0);  // NOLINT(bugprone-unchecked-optional-access)
    }

    void
    testWithInvalidClientCAFile()
    {
        testcase("GRPCServer with invalid/non-existent client CA file");

        using namespace jtx;

        auto cfg = envconfig();
        (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
        (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, getServerCertPath().string());
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, getServerKeyPath().string());
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslClientCa, "/nonexistent/path/to/ca.pem");

        Env env(*this, std::move(cfg));

        // Server should fail to start - verify port is 0
        auto const grpcPort =
            env.app().config()[Sections::kPortGrpc].get<unsigned int>(Keys::kPort);
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort == 0);  // NOLINT(bugprone-unchecked-optional-access)
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
        (*cfg)[Sections::kPortGrpc].set(Keys::kIp, "127.0.0.1");
        (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, getServerCertPath().string());
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, getServerKeyPath().string());
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslClientCa, emptyCAPath.string());

        Env env(*this, std::move(cfg));

        // Server should fail to start due to empty CA file
        auto const grpcPort =
            env.app().config()[Sections::kPortGrpc].get<unsigned int>(Keys::kPort);
        BEAST_EXPECT(grpcPort.has_value());
        BEAST_EXPECT(*grpcPort == 0);  // NOLINT(bugprone-unchecked-optional-access)
    }

    void
    testWithBothCertChainAndClientCA()
    {
        testcase("GRPCServer with both cert chain and client CA (full mTLS with intermediates)");

        using namespace jtx;

        // Test with all TLS features enabled: cert, key, cert_chain, and client_ca
        auto cfg = envconfig();
        (*cfg)[Sections::kPortGrpc].set(Keys::kIp, getEnvLocalhostAddr());
        (*cfg)[Sections::kPortGrpc].set(Keys::kPort, "0");
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslCert, getServerCertPath().string());
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslKey, getServerKeyPath().string());
        (*cfg)[Sections::kPortGrpc].set(
            Keys::kSslCertChain, getCACertPath().string());  // Using CA as intermediate
        (*cfg)[Sections::kPortGrpc].set(Keys::kSslClientCa, getCACertPath().string());

        Env env(*this, std::move(cfg));

        // Verify the server started successfully
        auto const grpcPort =
            env.app().config()[Sections::kPortGrpc].get<unsigned int>(Keys::kPort);
        BEAST_EXPECT(grpcPort.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access) grpcPort.has_value() checked above
        BEAST_EXPECT(*grpcPort > 0);

        auto const serverAddress = "localhost:" + std::to_string(*grpcPort);
        // NOLINTEND(bugprone-unchecked-optional-access)

        // Test 1: TLS client WITHOUT client certificate should FAIL (mTLS requires client cert)
        grpc::SslCredentialsOptions sslOptsNoClient;
        sslOptsNoClient.pem_root_certs = std::string(kCaCertContent);
        auto tlsStubNoClient = org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(
            grpc::CreateChannel(serverAddress, grpc::SslCredentials(sslOptsNoClient)));
        BEAST_EXPECT(!makeTestGRPCCall(tlsStubNoClient));

        // Test 2: TLS client WITH client certificate should succeed
        grpc::SslCredentialsOptions sslOptsWithClient;
        sslOptsWithClient.pem_root_certs = std::string(kCaCertContent);
        sslOptsWithClient.pem_cert_chain = std::string(kClientCertContent);
        sslOptsWithClient.pem_private_key = std::string(kClientKeyContent);
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

}  // namespace xrpl::test
