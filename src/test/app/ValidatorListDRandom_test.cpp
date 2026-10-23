#include <test/jtx/Env.h>

#include <xrpld/app/misc/ValidatorList.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

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

        // Generate 3 random Dilithium validator keys
        std::vector<std::string> hexKeys;
        std::vector<PublicKey> expectedKeys;

        for (int i = 0; i < 3; i++)
        {
            auto pk = randomNode();
            expectedKeys.push_back(pk);

            // Convert to hex (this is how Dilithium keys should be in config)
            auto hexKey = strHex(pk);
            hexKeys.push_back(hexKey);

            log << "Generated validator key " << (i + 1) << ": " << hexKey.substr(0, 64) << "..."
                << " (length: " << hexKey.size() << ")" << std::endl;
        }

        // Try to load these keys using the ValidatorList.load() method
        // This mimics loading from [validators] config section
        bool loaded = env.app().getValidators().load(
            std::nullopt,  // no local signing key
            hexKeys,       // validator keys from [validators] section
            {},            // no publisher keys
            std::nullopt   // no threshold
        );

        if (!loaded)
        {
            fail(
                "FAILED to load Dilithium validator keys from [validators] section!\n"
                "This is the bug causing: 'Invalid node identity'\n"
                "\n"
                "The code at ValidatorList.cpp:232 uses parseBase58() for validator keys,\n"
                "but Dilithium keys (2624 hex chars) should be parsed as HEX, not Base58.\n"
                "\n"
                "Publisher keys work because they use strUnHex() at line 148.\n"
                "Validator keys need the same treatment!");
        }
        else
        {
            log << "✓ Successfully loaded " << hexKeys.size() << " Dilithium validator keys"
                << std::endl;

            // Verify the keys were actually loaded
            auto trustedKeys = env.app().getValidators().getTrustedMasterKeys();
            log << "Trusted master keys count: " << trustedKeys.size() << std::endl;

            // Check that our keys are in the trusted set
            int foundCount = 0;
            for (auto const& key : expectedKeys)
            {
                if (trustedKeys.count(key) > 0)
                    foundCount++;
            }

            log << "Found " << foundCount << " of " << expectedKeys.size() << " keys in trusted set"
                << std::endl;

            BEAST_EXPECT(foundCount == expectedKeys.size());
            pass();
        }
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
