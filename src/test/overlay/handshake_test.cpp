#include <test/jtx/Env.h>

#include <xrpld/overlay/detail/Handshake.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/tokens.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace xrpl::test {

class Handshake_test : public beast::unit_test::Suite
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
            sharedValue.data()[i] = randInt<std::uint8_t>(0, 255);

        // Build handshake headers
        boost::beast::http::fields headers;
        auto const localIP = boost::asio::ip::make_address("127.0.0.1");
        auto const remoteIP = boost::asio::ip::make_address("192.168.1.100");

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
        auto const pkField = headers.find("Public-Key");
        BEAST_EXPECT(pkField != headers.end());
        BEAST_EXPECT(!pkField->value().empty());

        // Verify the signature is in base64 format
        auto const sigField = headers.find("Session-Signature");
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
            sharedValue.data()[i] = randInt<std::uint8_t>(0, 255);

        boost::beast::http::fields headers;
        auto const localIP = boost::asio::ip::make_address("127.0.0.1");
        auto const remoteIP = boost::asio::ip::make_address("192.168.1.100");

        buildHandshake(headers, sharedValue, std::nullopt, localIP, remoteIP, env.app());

        // Verify signature is base64 encoded and has reasonable size
        auto const sigField = headers.find("Session-Signature");
        BEAST_EXPECT(sigField != headers.end());

        auto const sigDecoded = base64Decode(sigField->value());
        // the default secp256k1 node identity signs with a DER signature of 64 to 72 bytes
        BEAST_EXPECT(sigDecoded.size() >= 64);
        BEAST_EXPECT(sigDecoded.size() <= 72);
    }

    void
    testMissingRequiredHeaders()
    {
        testcase("Reject missing required headers");

        using namespace jtx;
        Env env(*this);

        uint256 sharedValue;
        for (size_t i = 0; i < sharedValue.size(); ++i)
            sharedValue.data()[i] = randInt<std::uint8_t>(0, 255);

        auto const localIP = boost::asio::ip::make_address("127.0.0.1");
        auto const remoteIP = boost::asio::ip::make_address("192.168.1.100");

        // Test missing Public-Key
        {
            boost::beast::http::fields headers;
            headers.insert("Session-Signature", "dummy");

            try
            {
                verifyHandshake(headers, sharedValue, std::nullopt, localIP, remoteIP, env.app());
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
                verifyHandshake(headers, sharedValue, std::nullopt, localIP, remoteIP, env.app());
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
    testDefaultNodeIdentity()
    {
        testcase("Default node identity is secp256k1");

        using namespace jtx;
        Env env(*this);

        auto const& nodeKey = env.app().nodeIdentity().first;
        BEAST_EXPECT(publicKeyType(nodeKey) == KeyType::Secp256k1);

        uint256 sharedValue;
        for (size_t i = 0; i < sharedValue.size(); ++i)
            sharedValue.data()[i] = randInt<std::uint8_t>(0, 255);

        boost::beast::http::fields headers;
        auto const localIP = boost::asio::ip::make_address("127.0.0.1");
        auto const remoteIP = boost::asio::ip::make_address("192.168.1.100");

        buildHandshake(headers, sharedValue, std::nullopt, localIP, remoteIP, env.app());

        auto const pkField = headers.find("Public-Key");
        BEAST_EXPECT(pkField != headers.end());
        auto pkOpt = parseBase58<PublicKey>(TokenType::NodePublic, pkField->value());
        BEAST_EXPECT(pkOpt && *pkOpt == nodeKey);
    }

    void
    testDilithiumGate()
    {
        testcase("Dilithium node key needs featureQuantum");

        using namespace jtx;
        for (bool const quantum : {false, true})
        {
            auto const all = testableAmendments();
            Env env(*this, quantum ? all : all - featureQuantum);

            uint256 sharedValue;
            for (size_t i = 0; i < sharedValue.size(); ++i)
                sharedValue.data()[i] = randInt<std::uint8_t>(0, 255);

            boost::beast::http::fields headers;
            auto const localIP = boost::asio::ip::make_address("127.0.0.1");
            auto const remoteIP = boost::asio::ip::make_address("192.168.1.100");
            buildHandshake(headers, sharedValue, std::nullopt, localIP, remoteIP, env.app());

            // the peer presents a Dilithium node key and its session signature
            auto const keys = randomKeyPair(KeyType::Dilithium);
            auto const sig = signDigest(keys.first, keys.second, sharedValue);
            headers.set("Public-Key", toBase58(TokenType::NodePublic, keys.first));
            headers.set("Session-Signature", base64Encode(sig.data(), sig.size()));

            std::optional<PublicKey> accepted;
            std::string error;
            try
            {
                accepted = verifyHandshake(
                    headers, sharedValue, std::nullopt, localIP, remoteIP, env.app());
            }
            catch (std::runtime_error const& e)
            {
                error = e.what();
            }
            if (quantum)
            {
                BEAST_EXPECT(accepted == keys.first);
            }
            else
            {
                BEAST_EXPECT(error == "Unsupported public key type");
            }
        }
    }

    void
    testNodeKeyTypes()
    {
        testcase("Node key types and session signature");

        using namespace jtx;
        Env env(*this, testableAmendments() - featureQuantum);

        UInt256 sharedValue;
        for (size_t i = 0; i < sharedValue.size(); ++i)
            sharedValue.data()[i] = randInt<std::uint8_t>(0, 255);
        auto const localIP = boost::asio::ip::make_address("127.0.0.1");
        auto const remoteIP = boost::asio::ip::make_address("192.168.1.100");

        // the error verifyHandshake throws for a peer presenting `pk` and `sig`, empty if accepted
        auto const errorFor = [&](PublicKey const& pk, Buffer const& sig) {
            boost::beast::http::fields headers;
            buildHandshake(headers, sharedValue, std::nullopt, localIP, remoteIP, env.app());
            headers.set("Public-Key", toBase58(TokenType::NodePublic, pk));
            headers.set("Session-Signature", base64Encode(sig.data(), sig.size()));
            try
            {
                auto const accepted = verifyHandshake(
                    headers, sharedValue, std::nullopt, localIP, remoteIP, env.app());
                BEAST_EXPECT(accepted == pk);
            }
            catch (std::runtime_error const& e)
            {
                return std::string(e.what());
            }
            return std::string();
        };

        auto const secp = randomKeyPair(KeyType::Secp256k1);
        BEAST_EXPECT(
            errorFor(secp.first, signDigest(secp.first, secp.second, sharedValue)).empty());

        // a signature over another value does not prove the session
        UInt256 const other = ~sharedValue;
        BEAST_EXPECT(
            errorFor(secp.first, signDigest(secp.first, secp.second, other)) ==
            "Failed to verify session");

        auto const ed = randomKeyPair(KeyType::Ed25519);
        BEAST_EXPECT(
            errorFor(
                ed.first,
                sign(ed.first, ed.second, Slice{sharedValue.data(), sharedValue.size()})) ==
            "Unsupported public key type");
    }

    void
    run() override
    {
        testFeatureParsing();
        testBuildHandshake();
        testSignatureFormat();
        testMissingRequiredHeaders();
        testDefaultNodeIdentity();
        testDilithiumGate();
        testNodeKeyTypes();
    }
};

BEAST_DEFINE_TESTSUITE(Handshake, overlay, xrpl);

}  // namespace xrpl::test
