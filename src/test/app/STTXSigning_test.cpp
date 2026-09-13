#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/basics/Log.h>
#include <xrpl/beast/unit_test.h>
#include <chrono>
#include <iostream>
#include <unordered_set>

namespace xrpl {
namespace test {

class SignatureVerification_test : public beast::unit_test::Suite
{
public:
    void
    testSignatureSpeed(KeyType keyType)
    {
        // testcase("Signature Verification Speed Test");
        const int iterations = 1000;
        auto const keypair = randomKeyPair(keyType);

        // KeyType switch
        std::cout << "--------------------------" << std::endl;
        if (keyType == KeyType::Secp256k1)
        {
            std::cout << "Using secp256k1 key type." << std::endl;
        }
        else if (keyType == KeyType::Ed25519)
        {
            std::cout << "Using ed25519 key type." << std::endl;
        }
        else if (keyType == KeyType::Dilithium)
        {
            std::cout << "Using dilithium key type." << std::endl;
        }

        // Create a transaction and sign it
        STTx tx(ttACCOUNT_SET, [&keypair](auto& obj) {
            obj.setAccountID(sfAccount, calcAccountID(keypair.first));
            obj.setFieldVL(sfMessageKey, keypair.first.slice());
            obj.setFieldVL(sfSigningPubKey, keypair.first.slice());
        });

        tx.sign(keypair.first, keypair.second);
        std::unordered_set<uint256, beast::Uhash<>> const presets;
        Rules const defaultRules{presets};
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            auto result = tx.checkSign(defaultRules);
            if (!result)
            {
                std::cout << "Signature verification failed on iteration " << i << std::endl;
                break;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double timePerVerification = static_cast<double>(duration.count()) / iterations;
        std::cout << "Total time for " << iterations << " verifications: "
            << duration.count() / 1000 << " ms" << std::endl;
        std::cout << "Time per verification: " << timePerVerification << " µs" << std::endl;
        std::cout << "Verifications per second: " << (1000000.0 / timePerVerification) << std::endl;
        BEAST_EXPECT(duration.count() / 1000 < 500);
    }

    void
    run() override
    {
        testSignatureSpeed(KeyType::Secp256k1);
        testSignatureSpeed(KeyType::Ed25519);
        testSignatureSpeed(KeyType::Dilithium);
    }
};

BEAST_DEFINE_TESTSUITE(SignatureVerification, app, xrpl);

}  // namespace test
}  // namespace xrpl
