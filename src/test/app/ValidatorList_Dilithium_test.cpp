#include <test/jtx.h>

#include <xrpld/app/misc/Manifest.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Sign.h>

#include <fstream>
#include <unistd.h>

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
class ValidatorList_Dilithium_test : public beast::unit_test::suite
{
private:
    /**
     * Load and parse the validator list JSON file
     */
    std::optional<Json::Value>
    loadValidatorList(std::string const& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return std::nullopt;

        Json::Value vl;
        Json::Reader reader;
        if (!reader.parse(file, vl))
            return std::nullopt;

        return vl;
    }

    /**
     * Decode and parse the base64-encoded blob
     */
    std::optional<Json::Value>
    decodeBlob(std::string const& blob)
    {
        auto decoded = base64_decode(blob);
        if (decoded.empty())
            return std::nullopt;

        Json::Value data;
        Json::Reader reader;
        if (!reader.parse(decoded, data))
            return std::nullopt;

        return data;
    }

    void
    testWorkingDirectory()
    {
        testcase("Working Directory");

        // Get current working directory to help debug path issues
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != nullptr)
        {
            log << "Current working directory: " << cwd << std::endl;
            pass();
        }
        else
        {
            fail("Could not determine current working directory");
        }
    }

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
        return derivePublicKey(KeyType::dilithium, randomSecretKey(KeyType::dilithium));
    }

    void
    testActualValidatorList()
    {
        testcase("Load and Test Actual VL");

        using namespace jtx;
        Env env{*this};

        // Set the test environment time to current real time
        // This allows us to test a live vl.json with real timestamps
        std::time_t nowUnix = std::time(nullptr);
        NetClock::time_point nowRipple{NetClock::duration{nowUnix - epoch_offset.count()}};
        env.timeKeeper().set(nowRipple);
        log << "Set test time to now: " << env.timeKeeper().now().time_since_epoch().count()
            << " (Ripple), " << nowUnix << " (Unix)" << std::endl;

        // Use the PRODUCTION cluster VL file (absolute path)
        std::string vlPath = "/Users/darkmatter/projects/ledger-works/xrpld-network-gen/workspace/dilithium-full-cluster/vl/vl.json";
        auto vlOpt = loadValidatorList(vlPath);

        if (!vlOpt)
        {
            fail("Could not find vl.json at: " + vlPath);
            return;
        }
        log << "✓ Loaded vl.json from: " << vlPath << std::endl;

        auto& vl = *vlOpt;

        // Validate Dilithium sizes
        BEAST_EXPECT(vl.isMember("public_key"));
        BEAST_EXPECT(vl.isMember("signature"));
        BEAST_EXPECT(vl.isMember("blob"));
        BEAST_EXPECT(vl.isMember("version"));

        if (vl.isMember("public_key"))
        {
            auto pubKeyLen = vl["public_key"].asString().size();
            log << "Publisher key length: " << pubKeyLen << std::endl;
            BEAST_EXPECT(pubKeyLen == 2624);
        }

        if (vl.isMember("signature"))
        {
            auto sigLen = vl["signature"].asString().size();
            log << "Signature length: " << sigLen << std::endl;
            BEAST_EXPECT(sigLen == 4840);
        }

        // Decode blob to check structure
        auto blobData = decodeBlob(vl["blob"].asString());
        BEAST_EXPECT(blobData.has_value());

        if (blobData)
        {
            log << "Blob sequence: " << (*blobData)["sequence"] << std::endl;
            log << "Validator count: " << (*blobData)["validators"].size() << std::endl;

            // Check if the timestamps in the blob are sane relative to real time
            if ((*blobData).isMember("effective") && (*blobData).isMember("expiration"))
            {
                auto effectiveRipple = (*blobData)["effective"].asUInt();
                auto expirationRipple = (*blobData)["expiration"].asUInt();

                log << "Blob effective (Ripple time): " << effectiveRipple << std::endl;
                log << "Blob expiration (Ripple time): " << expirationRipple << std::endl;

                // Convert to Unix time for human readability
                std::time_t effectiveUnix = effectiveRipple + epoch_offset.count();
                std::time_t expirationUnix = expirationRipple + epoch_offset.count();
                std::time_t nowUnix = std::time(nullptr);

                log << "Blob effective (Unix): " << effectiveUnix << " vs now: " << nowUnix << std::endl;
                log << "Blob expiration (Unix): " << expirationUnix << " vs now: " << nowUnix << std::endl;

                if (expirationUnix < nowUnix)
                {
                    fail("VL.json has EXPIRED timestamps!\n"
                         "Expiration: " + std::to_string(expirationUnix) + " < Now: " + std::to_string(nowUnix));
                }
                else if (effectiveUnix > nowUnix)
                {
                    log << "Warning: VL.json effective time is in the future" << std::endl;
                }
            }

            // Prepare the blob for applyLists
            std::string const& blob = vl["blob"].asString();
            std::string const& signature = vl["signature"].asString();
            std::string const manifest = vl.isMember("manifest") ? vl["manifest"].asString() : "";
            std::uint32_t version = vl["version"].asUInt();
            std::string const& publisherKeyHex = vl["public_key"].asString();

            // Configure the publisher key as trusted in the app's validators
            std::vector<std::string> publisherKeys = {publisherKeyHex};
            BEAST_EXPECT(env.app().validators().load({}, {}, publisherKeys));

            ValidatorBlobInfo blobInfo{blob, signature, manifest};

            // Apply the list using the PRODUCTION code path - env.app().validators()
            auto result = env.app().validators().applyLists(
                manifest,
                version,
                {blobInfo},
                "file://" + vlPath);

            log << "applyLists disposition: " << to_string(result.bestDisposition()) << std::endl;

            // Test the EXACT PRODUCTION code path: env.app().validators().expires()
            // This is what RCLConsensus checks at RCLConsensus.cpp:991:
            //   auto const when = app_.validators().expires();
            //   if (!when || *when < now)
            //       validating_ = false;  // Bow out of consensus!

            auto when = env.app().validators().expires();
            auto now = env.timeKeeper().now();

            log << "Current time (NetClock): " << now.time_since_epoch().count() << std::endl;

            // Test the EXACT condition from RCLConsensus
            if (!when || *when < now)
            {
                // This is the PRODUCTION FAILURE CASE - RCLConsensus bows out
                if (!when)
                {
                    fail("PRODUCTION FAILURE: expires() returned nullopt!\n"
                         "RCLConsensus.cpp:991 condition: if (!when) => validating_ = false\n"
                         "VL disposition: " + to_string(result.bestDisposition()) + "\n"
                         "This means the node will BOW OUT of consensus!");
                }
                else
                {
                    fail("PRODUCTION FAILURE: VL expired!\n"
                         "RCLConsensus.cpp:991 condition: *when < now => validating_ = false\n"
                         "expires() = " + std::to_string(when->time_since_epoch().count()) + "\n" +
                         "now() = " + std::to_string(now.time_since_epoch().count()) + "\n" +
                         "This means the node will BOW OUT of consensus!");
                }
            }
            else
            {
                // SUCCESS - RCLConsensus will continue validating
                log << "✓ Expiration time (NetClock): " << when->time_since_epoch().count() << std::endl;
                log << "✓ VL is valid and active - RCLConsensus will CONTINUE validating" << std::endl;
                pass();
            }

            // Test other production code paths
            auto count = env.app().validators().count();
            log << "Trusted validator count: " << count << std::endl;
            BEAST_EXPECT(count > 0);

            // Test getTrustedMasterKeys production code path
            auto trustedMasterKeys = env.app().validators().getTrustedMasterKeys();
            log << "Trusted master keys: " << trustedMasterKeys.size() << std::endl;
        }
    }

    void
    testPublisherKeyMatch()
    {
        testcase("Publisher Key Match");

        std::string keystorePath = "/Users/darkmatter/projects/ledger-works/xrpld-network-gen/workspace/keystore/vl/key.json";
        auto keystoreOpt = loadValidatorList(keystorePath);

        if (!keystoreOpt)
        {
            log << "⚠ Could not find keystore, skipping publisher key match test" << std::endl;
            pass();
            return;
        }

        auto& keystore = *keystoreOpt;
        if (!keystore.isMember("publicKey"))
        {
            log << "⚠ Keystore missing publicKey field" << std::endl;
            pass();
            return;
        }

        std::string expectedKey = keystore["publicKey"].asString();
        log << "Expected key from keystore: " << expectedKey.substr(0, 64) << "..." << std::endl;

        // Load actual VL
        // Use the PRODUCTION cluster VL file (absolute path)
        std::string vlPath = "/Users/darkmatter/projects/ledger-works/xrpld-network-gen/workspace/dilithium-full-cluster/vl/vl.json";
        auto vlOpt = loadValidatorList(vlPath);

        if (!vlOpt)
        {
            log << "⚠ Could not find vl.json, skipping test" << std::endl;
            pass();
            return;
        }

        auto& vl = *vlOpt;
        if (!vl.isMember("public_key"))
        {
            fail("vl.json missing public_key field");
            return;
        }

        std::string actualKey = vl["public_key"].asString();
        log << "Actual key from vl.json: " << actualKey.substr(0, 64) << "..." << std::endl;

        // Compare the keys
        if (actualKey == expectedKey)
        {
            log << "✓ Publisher key matches keystore" << std::endl;
            pass();
        }
        else
        {
            fail("Publisher key mismatch!\n"
                 "Expected: " + expectedKey.substr(0, 64) + "...\n" +
                 "Actual:   " + actualKey.substr(0, 64) + "...\n" +
                 "The vl.json publisher key does not match the keystore.");
        }
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

            log << "Generated validator key " << (i+1) << ": "
                << hexKey.substr(0, 64) << "..."
                << " (length: " << hexKey.size() << ")" << std::endl;
        }

        // Try to load these keys using the ValidatorList.load() method
        // This mimics loading from [validators] config section
        bool loaded = env.app().validators().load(
            std::nullopt,  // no local signing key
            hexKeys,       // validator keys from [validators] section
            {},            // no publisher keys
            std::nullopt   // no threshold
        );

        if (!loaded)
        {
            fail("FAILED to load Dilithium validator keys from [validators] section!\n"
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
            log << "✓ Successfully loaded " << hexKeys.size() << " Dilithium validator keys" << std::endl;

            // Verify the keys were actually loaded
            auto trustedKeys = env.app().validators().getTrustedMasterKeys();
            log << "Trusted master keys count: " << trustedKeys.size() << std::endl;

            // Check that our keys are in the trusted set
            int foundCount = 0;
            for (const auto& key : expectedKeys)
            {
                if (trustedKeys.count(key) > 0)
                    foundCount++;
            }

            log << "Found " << foundCount << " of " << expectedKeys.size()
                << " keys in trusted set" << std::endl;

            BEAST_EXPECT(foundCount == expectedKeys.size());
            pass();
        }
    }

    void
    testNodeConfigHasPublisherKey()
    {
        testcase("Node Config Has Publisher Key");

        // Load the publisher key from vl.json
        // Use the PRODUCTION cluster VL file (absolute path)
        std::string vlPath = "/Users/darkmatter/projects/ledger-works/xrpld-network-gen/workspace/dilithium-full-cluster/vl/vl.json";
        auto vlOpt = loadValidatorList(vlPath);

        if (!vlOpt)
        {
            log << "⚠ Could not find vl.json, skipping test" << std::endl;
            pass();
            return;
        }

        auto& vl = *vlOpt;
        if (!vl.isMember("public_key"))
        {
            log << "vl.json missing public_key field" << std::endl;
            pass();
            return;
        }

        std::string publisherKey = vl["public_key"].asString();
        log << "Publisher key from vl.json: " << publisherKey.substr(0, 64) << "..." << std::endl;

        // Check validators.txt files for the publisher key (production cluster)
        std::vector<std::string> configPaths = {
            "/Users/darkmatter/projects/ledger-works/xrpld-network-gen/workspace/dilithium-full-cluster/vnode1/config/validators.txt",
            "/Users/darkmatter/projects/ledger-works/xrpld-network-gen/workspace/dilithium-full-cluster/vnode2/config/validators.txt",
            "/Users/darkmatter/projects/ledger-works/xrpld-network-gen/workspace/dilithium-full-cluster/vnode3/config/validators.txt",
            "/Users/darkmatter/projects/ledger-works/xrpld-network-gen/workspace/dilithium-full-cluster/pnode1/config/validators.txt"
        };

        std::vector<std::string> configsWithKey;
        std::vector<std::string> configsMissingKey;
        std::vector<std::string> configsNotFound;

        for (const auto& path : configPaths)
        {
            std::ifstream file(path);
            if (!file.is_open())
            {
                configsNotFound.push_back(path);
                continue;
            }

            std::string line;
            bool inValidatorListKeys = false;
            bool hasKey = false;

            while (std::getline(file, line))
            {
                // Check for section header
                if (line.find("[validator_list_keys]") != std::string::npos)
                {
                    inValidatorListKeys = true;
                    continue;
                }

                // Check if we've moved to a new section
                if (inValidatorListKeys && !line.empty() && line[0] == '[')
                {
                    inValidatorListKeys = false;
                }

                // Check if this line contains our publisher key
                if (inValidatorListKeys && line.find(publisherKey) != std::string::npos)
                {
                    hasKey = true;
                    break;
                }
            }

            if (hasKey)
                configsWithKey.push_back(path);
            else
                configsMissingKey.push_back(path);
        }

        // Report findings
        if (!configsWithKey.empty())
        {
            log << "✓ Configs with correct Dilithium publisher key:" << std::endl;
            for (const auto& path : configsWithKey)
                log << "  " << path << std::endl;
        }

        if (!configsMissingKey.empty())
        {
            log << "✗ Configs MISSING or WRONG publisher key:" << std::endl;
            for (const auto& path : configsMissingKey)
                log << "  " << path << std::endl;
        }

        if (!configsNotFound.empty())
        {
            log << "⚠ Configs not found:" << std::endl;
            for (const auto& path : configsNotFound)
                log << "  " << path << std::endl;
        }

        if (!configsMissingKey.empty())
        {
            fail("Some node configs are missing the correct Dilithium publisher key!\n"
                 "This causes: 'UNL manifest is signed with an unrecognized master public key'\n"
                 "\n"
                 "The config files may have an old Ed25519 key instead of the new Dilithium key.\n"
                 "Check the failing configs and update [validator_list_keys] with:\n" +
                 publisherKey.substr(0, 64) + "...");
        }
        else if (configsWithKey.empty())
        {
            fail("Could not find ANY validator config files!");
        }
        else
        {
            pass();
        }
    }

public:
    void
    run() override
    {
        testWorkingDirectory();
        testValidatorListStructure();
        testLoadDilithiumValidatorKeys();  // NEW: Test loading Dilithium keys from [validators]
        testActualValidatorList();
        testPublisherKeyMatch();
        testNodeConfigHasPublisherKey();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorList_Dilithium, app, xrpl);

}  // namespace test
}  // namespace xrpl
