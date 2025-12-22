#include <test/jtx/Env.h>

#include <xrpld/overlay/detail/Handshake.h>

#include <xrpl/basics/base64.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <boost/asio/ip/address.hpp>
#include <boost/beast/http/fields.hpp>

namespace xrpl {
namespace test {

class Handshake_test : public beast::unit_test::suite
{
public:
    void
    testFeatureParsing()
    {
        testcase("X-Protocol-Ctl feature parsing");

        boost::beast::http::fields headers;
        headers.insert(
            "X-Protocol-Ctl",
            "feature1=v1,v2,v3; feature2=v4; feature3=10; feature4=1; "
            "feature5=v6");
        BEAST_EXPECT(!featureEnabled(headers, "feature1"));
        BEAST_EXPECT(!isFeatureValue(headers, "feature1", "2"));
        BEAST_EXPECT(isFeatureValue(headers, "feature1", "v1"));
        BEAST_EXPECT(isFeatureValue(headers, "feature1", "v2"));
        BEAST_EXPECT(isFeatureValue(headers, "feature1", "v3"));
        BEAST_EXPECT(isFeatureValue(headers, "feature2", "v4"));
        BEAST_EXPECT(!isFeatureValue(headers, "feature3", "1"));
        BEAST_EXPECT(isFeatureValue(headers, "feature3", "10"));
        BEAST_EXPECT(!isFeatureValue(headers, "feature4", "10"));
        BEAST_EXPECT(isFeatureValue(headers, "feature4", "1"));
        BEAST_EXPECT(!featureEnabled(headers, "v6"));
    }

    void
    testBuildHandshake()
    {
        testcase("Build handshake creates required headers");

        using namespace jtx;
        Env env(*this);

        // Create a shared value (simulating SSL session data)
        uint256 sharedValue;
        for (size_t i = 0; i < sharedValue.size(); ++i)
            sharedValue.data()[i] = rand_int<std::uint8_t>(0, 255);

        // Build handshake headers
        boost::beast::http::fields headers;
        auto localIP = boost::asio::ip::make_address("127.0.0.1");
        auto remoteIP = boost::asio::ip::make_address("192.168.1.100");

        buildHandshake(
            headers,
            sharedValue,
            std::nullopt,  // networkID
            localIP,
            remoteIP,
            env.app());

        // Verify the handshake headers contain required fields
        BEAST_EXPECT(headers.count("Public-Key") == 1);
        BEAST_EXPECT(headers.count("Session-Signature") == 1);
        BEAST_EXPECT(headers.count("Network-Time") == 1);

        // Verify the public key is in base58 format
        auto pkField = headers.find("Public-Key");
        BEAST_EXPECT(pkField != headers.end());
        BEAST_EXPECT(!pkField->value().empty());

        // Verify the signature is in base64 format
        auto sigField = headers.find("Session-Signature");
        BEAST_EXPECT(sigField != headers.end());
        BEAST_EXPECT(!sigField->value().empty());
    }

    void
    testSignatureFormat()
    {
        testcase("Verify signature is properly formatted");

        using namespace jtx;
        Env env(*this);

        uint256 sharedValue;
        for (size_t i = 0; i < sharedValue.size(); ++i)
            sharedValue.data()[i] = rand_int<std::uint8_t>(0, 255);

        boost::beast::http::fields headers;
        auto localIP = boost::asio::ip::make_address("127.0.0.1");
        auto remoteIP = boost::asio::ip::make_address("192.168.1.100");

        buildHandshake(
            headers,
            sharedValue,
            std::nullopt,
            localIP,
            remoteIP,
            env.app());

        // Verify signature is base64 encoded and has reasonable size
        auto sigField = headers.find("Session-Signature");
        BEAST_EXPECT(sigField != headers.end());

        auto sigDecoded = base64_decode(sigField->value());
        // Dilithium2 signatures should be 2420 bytes
        BEAST_EXPECT(sigDecoded.size() > 2000);
        BEAST_EXPECT(sigDecoded.size() < 3000);
    }

    void
    testMissingRequiredHeaders()
    {
        testcase("Reject missing required headers");

        using namespace jtx;
        Env env(*this);

        uint256 sharedValue;
        for (size_t i = 0; i < sharedValue.size(); ++i)
            sharedValue.data()[i] = rand_int<std::uint8_t>(0, 255);

        auto localIP = boost::asio::ip::make_address("127.0.0.1");
        auto remoteIP = boost::asio::ip::make_address("192.168.1.100");

        // Test missing Public-Key
        {
            boost::beast::http::fields headers;
            headers.insert("Session-Signature", "dummy");

            try
            {
                verifyHandshake(
                    headers,
                    sharedValue,
                    std::nullopt,
                    localIP,
                    remoteIP,
                    env.app());
                fail("Should have thrown for missing Public-Key");
            }
            catch (std::runtime_error const&)
            {
                // Expected
                pass();
            }
        }

        // Test missing Session-Signature
        {
            boost::beast::http::fields headers;
            headers.insert("Public-Key", "dummy");

            try
            {
                verifyHandshake(
                    headers,
                    sharedValue,
                    std::nullopt,
                    localIP,
                    remoteIP,
                    env.app());
                fail("Should have thrown for missing Session-Signature");
            }
            catch (std::runtime_error const&)
            {
                // Expected - any exception is fine
                pass();
            }
        }
    }

    void
    testDilithiumKeySupport()
    {
        testcase("Verify dilithium keys work in handshake");

        using namespace jtx;
        Env env(*this);

        // Verify the node identity is using dilithium
        auto const& nodeKey = env.app().nodeIdentity().first;
        auto keyType = publicKeyType(nodeKey);

        BEAST_EXPECT(keyType.has_value());
        BEAST_EXPECT(*keyType == KeyType::dilithium);

        // Build handshake with dilithium keys
        uint256 sharedValue;
        for (size_t i = 0; i < sharedValue.size(); ++i)
            sharedValue.data()[i] = rand_int<std::uint8_t>(0, 255);

        boost::beast::http::fields headers;
        auto localIP = boost::asio::ip::make_address("127.0.0.1");
        auto remoteIP = boost::asio::ip::make_address("192.168.1.100");

        buildHandshake(
            headers,
            sharedValue,
            std::nullopt,
            localIP,
            remoteIP,
            env.app());

        // Verify the public key in headers is dilithium
        auto pkField = headers.find("Public-Key");
        BEAST_EXPECT(pkField != headers.end());

        // Parse the public key to verify it's dilithium
        auto pkOpt = parseBase58<PublicKey>(TokenType::NodePublic, pkField->value());
        BEAST_EXPECT(pkOpt.has_value());
        if (pkOpt)
        {
            BEAST_EXPECT(publicKeyType(*pkOpt) == KeyType::dilithium);
        }
    }

    void
    run() override
    {
        testFeatureParsing();
        testBuildHandshake();
        testSignatureFormat();
        testMissingRequiredHeaders();
        testDilithiumKeySupport();
    }
};

BEAST_DEFINE_TESTSUITE(Handshake, overlay, xrpl);

}  // namespace test
}  // namespace xrpl
