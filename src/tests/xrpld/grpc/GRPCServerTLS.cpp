#include <gtest/gtest.h>

#include "TLSCertificateFixture.h"

#include <filesystem>

namespace xrpl {
namespace test {

/**
 * Fixture that provides TLS certificates for tests.
 */
class GRPCServerTLSFixture : public ::testing::Test, public TemporaryTLSCertificates
{
};

TEST(GRPCServerTLS, HelloWorld)
{
    // Simple hello world test to verify the test infrastructure works
    EXPECT_TRUE(true);
    EXPECT_EQ(1 + 1, 2);
}

TEST_F(GRPCServerTLSFixture, CertificateFixtureCreatesFiles)
{
    // Test that the fixture creates certificate files in a temp directory
    // Using the inherited fixture methods directly
    auto caCert = getCACertPath();
    auto serverCert = getServerCertPath();
    auto serverKey = getServerKeyPath();

    // Verify all certificate files exist
    EXPECT_TRUE(std::filesystem::exists(caCert)) << "CA certificate not found at: " << caCert;
    EXPECT_TRUE(std::filesystem::exists(serverCert)) << "Server certificate not found at: " << serverCert;
    EXPECT_TRUE(std::filesystem::exists(serverKey)) << "Server key not found at: " << serverKey;

    // Verify they're in a temp directory
    auto tempDirPath = getTempDir();
    EXPECT_TRUE(std::filesystem::exists(tempDirPath)) << "Temp directory not found at: " << tempDirPath;
    EXPECT_TRUE(tempDirPath.string().find("grpc_tls_test_") != std::string::npos)
        << "Temp directory doesn't have expected name pattern: " << tempDirPath;
}

}  // namespace test
}  // namespace xrpl
