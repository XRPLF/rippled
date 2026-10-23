#include <test/jtx/Env.h>

#include <xrpld/app/misc/ValidatorList.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/tokens.h>

#include <optional>
#include <string>
#include <vector>

namespace xrpl {
namespace test {

/**
 * Test suite for Dilithium-based Validator Lists
 *
 * This tests the validator list structure used in the quantum-resistant
 * XRPL implementation. Dilithium signatures are much larger than Ed25519:
 * - Public keys: ~2624 hex chars (vs 66 for Ed25519)
 * - Signatures: ~4840 hex chars (vs 128 for Ed25519)
 * - Manifests: ~10004 hex chars
 *
 * Example structure (with abbreviated keys):
 * {
 *   "blob": "eyJzZXF1ZW5jZSI6IDEsICJlZmZlY3RpdmUiOiAxNzY2NDQ2NDI5...",
 *   "manifest": "",
 *   "public_key": "BD748E50DB6DA9DDB7ABF960B31A7028E867BFDE...",  // 2624 chars
 *   "signature": "A1B2C3D4E5F6...",                              // 4840 chars
 *   "version": 1
 * }
 *
 * The blob decodes to:
 * {
 *   "sequence": 1,
 *   "effective": 1766446429,
 *   "expiration": 1797986029,
 *   "validators": [
 *     {
 *       "validation_public_key": "...",  // 2624 chars
 *       "manifest": "..."                // 10004 chars base64
 *     },
 *     ...
 *   ]
 * }
 */
class ValidatorList_Dilithium_test : public beast::unit_test::Suite
{
private:
    void
    testValidatorListStructure()
    {
        testcase("Validator List Structure");

        // Verify Dilithium key sizes (meta-test)
        auto const dilithiumKey = randomNode();
        auto const hexKey = strHex(dilithiumKey);

        log << "Dilithium public key hex length: " << hexKey.size() << std::endl;
        BEAST_EXPECT(hexKey.size() == 2624);  // Dilithium public keys are 2624 hex chars

        pass();
    }

    PublicKey
    randomNode()
    {
        return derivePublicKey(KeyType::Dilithium, randomSecretKey(KeyType::Dilithium));
    }

    void
    testLoadDilithiumValidatorKeys()
    {
        testcase("Load Dilithium Validator Keys from [validators]");

        using namespace jtx;
        Env env{*this};

        // [validators] entries are base58 node public keys, 1799 characters for a Dilithium key
        std::vector<std::string> configKeys;
        std::vector<PublicKey> expectedKeys;
        for (int i = 0; i < 3; ++i)
        {
            auto const pk = randomNode();
            expectedKeys.push_back(pk);
            configKeys.push_back(toBase58(TokenType::NodePublic, pk));
            BEAST_EXPECT(configKeys.back().size() == 1799);
        }

        std::vector<std::string> const noPublishers;
        BEAST_EXPECT(
            env.app().getValidators().load(std::nullopt, configKeys, noPublishers, std::nullopt));
        for (auto const& key : expectedKeys)
            BEAST_EXPECT(env.app().getValidators().listed(key));
    }

    void
    run() override
    {
        testValidatorListStructure();
        testLoadDilithiumValidatorKeys();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorList_Dilithium, app, xrpl);

}  // namespace test
}  // namespace xrpl
