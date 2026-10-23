#include <xrpl/basics/Log.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/digest.h>

#include <string>
#include <unordered_set>

namespace xrpl {
namespace test {

class SignatureVerification_test : public beast::unit_test::Suite
{
public:
    void
    testSignatureVerification(KeyType keyType)
    {
        testcase(std::string("Transaction signature verification: ") + to_string(keyType));
        auto const keypair = randomKeyPair(keyType);

        STTx tx(ttACCOUNT_SET, [&keypair](auto& obj) {
            obj.setAccountID(sfAccount, calcAccountID(keypair.first));
            obj.setFieldVL(sfMessageKey, keypair.first.slice());
            obj.setFieldVL(sfSigningPubKey, keypair.first.slice());
        });
        tx.sign(keypair.first, keypair.second);

        std::unordered_set<uint256, beast::Uhash<>> const presets{featureQuantum};
        Rules const rules{presets};
        for (int i = 0; i < 10; ++i)
            BEAST_EXPECT(tx.checkSign(rules));
    }

    void
    run() override
    {
        testSignatureVerification(KeyType::Secp256k1);
        testSignatureVerification(KeyType::Ed25519);
        testSignatureVerification(KeyType::Dilithium);
    }
};

BEAST_DEFINE_TESTSUITE(SignatureVerification, app, xrpl);

}  // namespace test
}  // namespace xrpl
